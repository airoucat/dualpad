#include "pch.h"
#include "haptics/FormSemanticCache.h"

#include "haptics/HapticsConfig.h"

#include <SKSE/SKSE.h>
#include <Windows.h>
#include <ShlObj.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>

namespace logger = SKSE::log;

namespace dualpad::haptics
{
    namespace
    {
        constexpr std::uint32_t kDiskVersion = 1;

#pragma pack(push, 1)
        struct CacheDiskHeader
        {
            char magic[4]{ 'D', 'P', 'S', 'C' };
            std::uint32_t version{ kDiskVersion };
            std::uint32_t entryCount{ 0 };
            std::uint32_t ruleVersion{ 0 };
            std::uint64_t fingerprintHash{ 0 };
            char gameVersion[32]{};
        };

        struct CacheDiskEntry
        {
            std::uint32_t formID{ 0 };
            std::uint8_t group{ 0 };
            std::uint16_t confidenceQ{ 0 };
            std::uint16_t weightQ{ 0 };
            std::uint32_t texturePresetID{ 0 };
            std::uint8_t flags{ 0 };
        };
#pragma pack(pop)

        constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
        constexpr std::uint64_t kFnvPrime = 1099511628211ull;

        inline bool IsHeaderValid(const CacheDiskHeader& h)
        {
            return h.magic[0] == 'D' && h.magic[1] == 'P' && h.magic[2] == 'S' && h.magic[3] == 'C' &&
                h.version == kDiskVersion;
        }

        inline std::uint16_t Quantize01(float v)
        {
            v = std::clamp(v, 0.0f, 1.0f);
            return static_cast<std::uint16_t>(v * 1000.0f + 0.5f);
        }

        inline float Dequantize01(std::uint16_t v)
        {
            return static_cast<float>(v) / 1000.0f;
        }

        inline bool EnsureParentDir(const std::filesystem::path& p)
        {
            std::error_code ec;
            if (const auto parent = p.parent_path(); !parent.empty()) {
                std::filesystem::create_directories(parent, ec);
            }
            return !ec;
        }

        inline std::string HeaderVersionToString(const char version[32])
        {
            std::size_t n = 0;
            while (n < 32 && version[n] != '\0') {
                ++n;
            }
            return std::string(version, version + n);
        }

        inline std::filesystem::path GetGameDataPath()
        {
            wchar_t buf[MAX_PATH]{};
            if (::GetModuleFileNameW(nullptr, buf, MAX_PATH) == 0) {
                return {};
            }

            const std::filesystem::path exe(buf);
            return exe.parent_path() / L"Data";
        }

        inline std::filesystem::path GetPluginsTxtPath()
        {
            PWSTR path = nullptr;
            if (FAILED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path)) || !path) {
                return {};
            }

            const std::filesystem::path base(path);
            ::CoTaskMemFree(path);
            return base / L"Skyrim Special Edition" / L"plugins.txt";
        }

        std::vector<std::string> ReadActivePlugins()
        {
            std::vector<std::string> out;

            const auto path = GetPluginsTxtPath();
            std::ifstream ifs(path);
            if (!ifs.is_open()) {
                return out;
            }

            std::string line;
            while (std::getline(ifs, line)) {
                while (!line.empty() && (line.back() == '\r' || line.back() == '\n' ||
                    line.back() == ' ' || line.back() == '\t')) {
                    line.pop_back();
                }

                std::size_t i = 0;
                while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
                    ++i;
                }
                if (i >= line.size() || line[i] == '#') {
                    continue;
                }
                if (line[i] == '*') {
                    ++i;
                }

                const auto name = line.substr(i);
                if (!name.empty()) {
                    out.push_back(name);
                }
            }

            return out;
        }

        inline std::uint64_t FileWriteStamp(const std::filesystem::path& p)
        {
            std::error_code ec;
            const auto t = std::filesystem::last_write_time(p, ec);
            return ec ? 0ull : static_cast<std::uint64_t>(t.time_since_epoch().count());
        }

        inline void FnvMix(std::uint64_t& h, const void* data, std::size_t len)
        {
            const auto* p = reinterpret_cast<const std::uint8_t*>(data);
            for (std::size_t i = 0; i < len; ++i) {
                h ^= p[i];
                h *= kFnvPrime;
            }
        }

        inline void FnvMixStr(std::uint64_t& h, const std::string& s)
        {
            const auto n = static_cast<std::uint32_t>(s.size());
            FnvMix(h, &n, sizeof(n));
            if (!s.empty()) {
                FnvMix(h, s.data(), s.size());
            }
        }

        template <class TForm>
        void ScanType(
            RE::TESDataHandler* dh,
            SemanticRuleEngine& rules,
            std::unordered_map<std::uint32_t, FormSemanticMeta>& out,
            std::size_t& unknownCount)
        {
            auto& forms = dh->GetFormArray<TForm>();
            for (auto* f : forms) {
                if (!f) {
                    continue;
                }

                const auto formID = f->GetFormID();
                if (formID == 0) {
                    continue;
                }

                const char* editorID = f->GetFormEditorID();
                const auto meta = rules.ClassifyEditorID(
                    editorID ? std::string_view(editorID) : std::string_view{},
                    f->GetFormType(),
                    formID);

                if (meta.group == SemanticGroup::Unknown) {
                    ++unknownCount;
                }

                out[formID] = meta;
            }
        }
    }

    FormSemanticCache& FormSemanticCache::GetSingleton()
    {
        static FormSemanticCache s;
        return s;
    }

    bool FormSemanticCache::Initialize()
    {
        bool expected = false;
        if (!_initialized.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            return true;
        }

        auto& cfg = HapticsConfig::GetSingleton();
        if (!cfg.enableFormSemanticCache) {
            auto empty = std::make_shared<Snapshot>();
            InstallSnapshot(std::shared_ptr<const Snapshot>(empty));
            logger::info("[Haptics][FormSemanticCache] disabled by config");
            return true;
        }

        if (!SemanticRuleEngine::GetSingleton().LoadRules(cfg.semanticRulesPath)) {
            logger::warn("[Haptics][FormSemanticCache] rules load failed, using in-memory defaults");
        }

        if (!cfg.semanticForceRebuild && LoadCache(cfg.semanticCachePath) && ValidateFingerprint()) {
            logger::info(
                "[Haptics][FormSemanticCache] loaded cache path={} entries={} cache_version={} fingerprint={:016X}",
                cfg.semanticCachePath,
                Size(),
                kCacheVersion,
                GetFingerprint().hash);
            return true;
        }

        BuildCache();
        if (!SaveCache(cfg.semanticCachePath)) {
            logger::warn("[Haptics][FormSemanticCache] save failed: {}", cfg.semanticCachePath);
        }
        return true;
    }

    void FormSemanticCache::BuildCache()
    {
        auto snapshot = std::make_shared<Snapshot>();
        snapshot->table.reserve(8192);

        auto& rules = SemanticRuleEngine::GetSingleton();
        snapshot->fingerprint = BuildCurrentFingerprint(rules.GetRuleVersion());

        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh) {
            InstallSnapshot(std::shared_ptr<const Snapshot>(snapshot));
            _rebuilds.fetch_add(1, std::memory_order_relaxed);
            logger::warn("[Haptics][FormSemanticCache] TESDataHandler is null, install empty snapshot");
            return;
        }

        std::size_t unknownCount = 0;
        ScanType<RE::TESSound>(dh, rules, snapshot->table, unknownCount);
        ScanType<RE::BGSSoundDescriptorForm>(dh, rules, snapshot->table, unknownCount);
        ScanType<RE::BGSSoundCategory>(dh, rules, snapshot->table, unknownCount);
        ScanType<RE::BGSMusicType>(dh, rules, snapshot->table, unknownCount);
        ScanType<RE::BGSMusicTrackFormWrapper>(dh, rules, snapshot->table, unknownCount);
        ScanType<RE::BGSAcousticSpace>(dh, rules, snapshot->table, unknownCount);
        ScanType<RE::BGSFootstep>(dh, rules, snapshot->table, unknownCount);
        ScanType<RE::BGSFootstepSet>(dh, rules, snapshot->table, unknownCount);
        ScanType<RE::BGSImpactData>(dh, rules, snapshot->table, unknownCount);
        ScanType<RE::BGSImpactDataSet>(dh, rules, snapshot->table, unknownCount);

        InstallSnapshot(std::shared_ptr<const Snapshot>(snapshot));
        _rebuilds.fetch_add(1, std::memory_order_relaxed);

        const float unknownRatio = snapshot->table.empty() ?
            0.0f :
            (static_cast<float>(unknownCount) / static_cast<float>(snapshot->table.size()));

        logger::info(
            "[Haptics][FormSemanticCache] built entries={} unknown={} ({:.1f}%) cache_version={} fingerprint={:016X}",
            snapshot->table.size(),
            unknownCount,
            unknownRatio * 100.0f,
            kCacheVersion,
            snapshot->fingerprint.hash);
    }

    bool FormSemanticCache::TryGet(std::uint32_t formID, FormSemanticMeta& outMeta) const
    {
        _lookups.fetch_add(1, std::memory_order_relaxed);

        const auto snapshot = std::atomic_load_explicit(&_snapshot, std::memory_order_acquire);
        if (!snapshot || formID == 0) {
            _misses.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        const auto it = snapshot->table.find(formID);
        if (it == snapshot->table.end()) {
            _misses.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        outMeta = it->second;
        _hits.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    bool FormSemanticCache::SaveCache(const std::filesystem::path& path) const
    {
        const auto snapshot = std::atomic_load_explicit(&_snapshot, std::memory_order_acquire);
        if (!snapshot || path.empty()) {
            return false;
        }

        if (!EnsureParentDir(path)) {
            logger::warn("[Haptics][FormSemanticCache] create dir failed: {}", path.string());
            return false;
        }

        CacheDiskHeader header{};
        header.version = kCacheVersion;
        header.entryCount = static_cast<std::uint32_t>(snapshot->table.size());
        header.ruleVersion = snapshot->fingerprint.ruleVersion;
        header.fingerprintHash = snapshot->fingerprint.hash;
        std::memset(header.gameVersion, 0, sizeof(header.gameVersion));
        std::memcpy(
            header.gameVersion,
            snapshot->fingerprint.gameVersion.c_str(),
            (std::min)(snapshot->fingerprint.gameVersion.size(), sizeof(header.gameVersion) - 1));

        std::filesystem::path tmp = path;
        tmp += ".tmp";

        {
            std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
            if (!ofs.is_open()) {
                logger::warn("[Haptics][FormSemanticCache] open tmp failed: {}", tmp.string());
                return false;
            }

            ofs.write(reinterpret_cast<const char*>(&header), sizeof(header));
            if (!ofs.good()) {
                logger::warn("[Haptics][FormSemanticCache] write header failed");
                return false;
            }

            for (const auto& [formID, meta] : snapshot->table) {
                CacheDiskEntry entry{};
                entry.formID = formID;
                entry.group = static_cast<std::uint8_t>(meta.group);
                entry.confidenceQ = Quantize01(meta.confidence);
                entry.weightQ = Quantize01(meta.baseWeight);
                entry.texturePresetID = meta.texturePresetId;
                entry.flags = meta.flags;

                ofs.write(reinterpret_cast<const char*>(&entry), sizeof(entry));
                if (!ofs.good()) {
                    logger::warn("[Haptics][FormSemanticCache] write entry failed");
                    return false;
                }
            }
        }

        std::error_code ec;
        std::filesystem::remove(path, ec);  // ignore
        ec.clear();
        std::filesystem::rename(tmp, path, ec);
        if (ec) {
            logger::warn("[Haptics][FormSemanticCache] rename failed: {} -> {} ({})",
                tmp.string(), path.string(), ec.message());
            return false;
        }

        return true;
    }

    bool FormSemanticCache::LoadCache(const std::filesystem::path& path)
    {
        if (path.empty()) {
            return false;
        }

        std::ifstream ifs(path, std::ios::binary);
        if (!ifs.is_open()) {
            return false;
        }

        CacheDiskHeader header{};
        ifs.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (!ifs.good() || !IsHeaderValid(header)) {
            logger::warn("[Haptics][FormSemanticCache] invalid cache header: {}", path.string());
            return false;
        }

        auto snapshot = std::make_shared<Snapshot>();
        snapshot->table.reserve(header.entryCount * 2 + 64);
        snapshot->fingerprint.gameVersion = HeaderVersionToString(header.gameVersion);
        snapshot->fingerprint.ruleVersion = header.ruleVersion;
        snapshot->fingerprint.hash = header.fingerprintHash;

        for (std::uint32_t i = 0; i < header.entryCount; ++i) {
            CacheDiskEntry entry{};
            ifs.read(reinterpret_cast<char*>(&entry), sizeof(entry));
            if (!ifs.good()) {
                logger::warn("[Haptics][FormSemanticCache] truncated cache file: {}", path.string());
                return false;
            }

            if (entry.formID == 0) {
                continue;
            }

            FormSemanticMeta meta{};
            meta.group = static_cast<SemanticGroup>(entry.group);
            meta.confidence = Dequantize01(entry.confidenceQ);
            meta.baseWeight = Dequantize01(entry.weightQ);
            meta.texturePresetId = entry.texturePresetID;
            meta.flags = entry.flags;
            snapshot->table[entry.formID] = meta;
        }

        InstallSnapshot(std::shared_ptr<const Snapshot>(snapshot));
        _loads.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    bool FormSemanticCache::ValidateFingerprint() const
    {
        const auto snapshot = std::atomic_load_explicit(&_snapshot, std::memory_order_acquire);
        if (!snapshot) {
            return false;
        }

        const auto current = BuildCurrentFingerprint(SemanticRuleEngine::GetSingleton().GetRuleVersion());
        const bool gameMatch = (snapshot->fingerprint.gameVersion == current.gameVersion);
        const bool ruleMatch = (snapshot->fingerprint.ruleVersion == current.ruleVersion);
        const bool hashMatch = (snapshot->fingerprint.hash == current.hash);

        if (!gameMatch || !ruleMatch || !hashMatch) {
            logger::warn(
                "[Haptics][FormSemanticCache] fingerprint mismatch game={} rule={} hash={} cache={:016X} current={:016X}",
                gameMatch, ruleMatch, hashMatch, snapshot->fingerprint.hash, current.hash);
            return false;
        }

        return true;
    }

    std::size_t FormSemanticCache::Size() const
    {
        const auto snapshot = std::atomic_load_explicit(&_snapshot, std::memory_order_acquire);
        return snapshot ? snapshot->table.size() : 0;
    }

    FormSemanticCache::Stats FormSemanticCache::GetStats() const
    {
        Stats s{};
        s.lookups = _lookups.load(std::memory_order_relaxed);
        s.hits = _hits.load(std::memory_order_relaxed);
        s.misses = _misses.load(std::memory_order_relaxed);
        s.loads = _loads.load(std::memory_order_relaxed);
        s.rebuilds = _rebuilds.load(std::memory_order_relaxed);
        return s;
    }

    void FormSemanticCache::ResetStats()
    {
        _lookups.store(0, std::memory_order_relaxed);
        _hits.store(0, std::memory_order_relaxed);
        _misses.store(0, std::memory_order_relaxed);
        _loads.store(0, std::memory_order_relaxed);
        _rebuilds.store(0, std::memory_order_relaxed);
    }

    CacheFingerprint FormSemanticCache::GetFingerprint() const
    {
        const auto snapshot = std::atomic_load_explicit(&_snapshot, std::memory_order_acquire);
        return snapshot ? snapshot->fingerprint : CacheFingerprint{};
    }

    CacheFingerprint FormSemanticCache::BuildCurrentFingerprint(std::uint32_t ruleVersion)
    {
        CacheFingerprint fp{};
        fp.gameVersion = "SE " + REL::Module::get().version().string("."sv);
        fp.ruleVersion = ruleVersion;

        const auto dataDir = GetGameDataPath();
        const auto plugins = ReadActivePlugins();
        fp.plugins.reserve(plugins.size());
        for (std::uint32_t i = 0; i < plugins.size(); ++i) {
            CachePluginState plugin{};
            plugin.name = plugins[i];
            plugin.loadOrder = i;

            const auto path = dataDir / std::filesystem::path(plugins[i]);
            std::error_code ec;
            plugin.size = std::filesystem::exists(path, ec) ? std::filesystem::file_size(path, ec) : 0;
            plugin.timestamp = FileWriteStamp(path);

            fp.plugins.push_back(std::move(plugin));
        }

        fp.hash = HashFingerprint(fp);
        return fp;
    }

    std::uint64_t FormSemanticCache::HashFingerprint(const CacheFingerprint& fp)
    {
        std::uint64_t h = kFnvOffset;
        FnvMixStr(h, fp.gameVersion);
        FnvMix(h, &fp.ruleVersion, sizeof(fp.ruleVersion));

        auto sorted = fp.plugins;
        std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
            if (a.loadOrder != b.loadOrder) {
                return a.loadOrder < b.loadOrder;
            }
            return a.name < b.name;
            });

        const auto count = static_cast<std::uint32_t>(sorted.size());
        FnvMix(h, &count, sizeof(count));

        for (const auto& p : sorted) {
            FnvMixStr(h, p.name);
            FnvMix(h, &p.loadOrder, sizeof(p.loadOrder));
            FnvMix(h, &p.timestamp, sizeof(p.timestamp));
            FnvMix(h, &p.size, sizeof(p.size));
        }

        return h;
    }

    void FormSemanticCache::InstallSnapshot(std::shared_ptr<const Snapshot> snapshot)
    {
        if (!snapshot) {
            return;
        }
        std::atomic_store_explicit(&_snapshot, std::move(snapshot), std::memory_order_release);
    }
}
