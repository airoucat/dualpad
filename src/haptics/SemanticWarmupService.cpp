#include "pch.h"
#include "haptics/SemanticWarmupService.h"

#include "haptics/FormSemanticCache.h"
#include "haptics/SemanticCacheIO.h"
#include "haptics/SemanticFingerprint.h"
#include "haptics/SemanticRules.h"
#include "haptics/SoundFormScanner.h"
#include <Windows.h>
#include <ShlObj.h>
#include <fstream>
#include <SKSE/SKSE.h>
#include <cstring>

namespace logger = SKSE::log;

namespace
{
    std::filesystem::path GetGameDataPath()
    {
        wchar_t buf[MAX_PATH]{};
        ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
        std::filesystem::path exe(buf);
        return exe.parent_path() / L"Data";
    }

    std::filesystem::path GetPluginsTxtPath()
    {
        PWSTR p = nullptr;
        if (FAILED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p)) || !p) {
            return {};
        }
        std::filesystem::path base(p);
        ::CoTaskMemFree(p);
        return base / L"Skyrim Special Edition" / L"plugins.txt";
    }

    std::vector<std::string> ReadActivePluginsFromTxt()
    {
        std::vector<std::string> out;
        const auto p = GetPluginsTxtPath();
        std::ifstream ifs(p);
        if (!ifs.is_open()) {
            return out;
        }

        std::string line;
        while (std::getline(ifs, line)) {
            // È¥µôÇ°ºó¿Õ°×
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
            std::size_t i = 0;
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
            if (i >= line.size()) continue;
            if (line[i] == '#') continue;

            if (line[i] == '*') {
                ++i; // active marker
            }
            auto name = line.substr(i);
            if (!name.empty()) {
                out.push_back(name);
            }
        }
        return out;
    }

    std::uint64_t FileWriteTick(const std::filesystem::path& p)
    {
        std::error_code ec;
        auto t = std::filesystem::last_write_time(p, ec);
        if (ec) return 0;
        return static_cast<std::uint64_t>(t.time_since_epoch().count());
    }
}

namespace dualpad::haptics
{
    SemanticWarmupService& SemanticWarmupService::GetSingleton()
    {
        static SemanticWarmupService s;
        return s;
    }

    std::filesystem::path SemanticWarmupService::CacheBinPath()
    {
        return "Data/SKSE/Plugins/DualPadHaptics/semantic_cache_v1.bin";
    }

    std::filesystem::path SemanticWarmupService::CacheMetaPath()
    {
        return "Data/SKSE/Plugins/DualPadHaptics/semantic_cache_v1.meta.json";
    }

    SemanticFingerprint SemanticWarmupService::BuildCurrentFingerprint() const
    {
        constexpr std::string_view kRuntime = "1.5.97";
        const auto rulesVer = SemanticRules::GetSingleton().GetRulesVersion();

        std::vector<SemanticPluginState> plugins;
        const auto names = ReadActivePluginsFromTxt();
        const auto dataDir = GetGameDataPath();

        plugins.reserve(names.size());
        for (std::uint32_t i = 0; i < names.size(); ++i) {
            SemanticPluginState s{};
            s.name = names[i];
            s.loadOrder = i;

            const auto f = dataDir / std::filesystem::path(names[i]);
            std::error_code ec;
            s.fileSize = std::filesystem::exists(f, ec) ? std::filesystem::file_size(f, ec) : 0;
            s.fileWriteTime = FileWriteTick(f);

            plugins.push_back(std::move(s));
        }

        return SemanticFingerprintBuilder::Build(kRuntime, rulesVer, std::move(plugins));
    }

    static std::string HeaderRuntimeToString(const char runtime[16])
    {
        std::size_t n = 0;
        while (n < 16 && runtime[n] != '\0') {
            ++n;
        }
        return std::string(runtime, runtime + n);
    }

    bool SemanticWarmupService::TryLoadCacheAndInstall(const SemanticFingerprint& fp) const
    {
        SemanticCacheFileHeader hdr{};
        std::vector<FormSemanticRecord> recs;

        if (!SemanticCacheIO::LoadBinary(CacheBinPath(), hdr, recs)) {
            return false;
        }

        const bool fpMatch = (hdr.fingerprintHash == fp.hash);
        const bool rulesMatch = (hdr.rulesVersion == fp.rulesVersion);
        const bool runtimeMatch = (HeaderRuntimeToString(hdr.runtime) == fp.gameRuntime);

        if (!fpMatch || !rulesMatch || !runtimeMatch) {
            logger::info("[Haptics][SemanticWarmup] cache mismatch fp={} rules={} runtime={}",
                fpMatch, rulesMatch, runtimeMatch);
            return false;
        }

        FormSemanticCache::GetSingleton().InstallFromRecords(recs, fp.hash, fp.rulesVersion);
        logger::info("[Haptics][SemanticWarmup] cache loaded records={}", recs.size());
        return true;
    }

    bool SemanticWarmupService::RebuildAndInstall(const SemanticFingerprint& fp) const
    {
        auto scanned = SoundFormScanner::GetSingleton().ScanAllSoundForms();

        std::vector<FormSemanticRecord> recs;
        recs.reserve(scanned.size());

        std::size_t unknown = 0;
        for (const auto& s : scanned) {
            FormSemanticRecord r{};
            r.formId = s.formId;
            r.meta = SemanticRules::GetSingleton().Classify(s.editorId, s.formType);
            if (r.meta.group == SemanticGroup::Unknown) {
                ++unknown;
            }
            recs.push_back(std::move(r));
        }

        FormSemanticCache::GetSingleton().InstallFromRecords(recs, fp.hash, fp.rulesVersion);

        SemanticCacheFileHeader hdr{};
        hdr.schemaVersion = 1;
        hdr.recordCount = static_cast<std::uint32_t>(recs.size());
        hdr.fingerprintHash = fp.hash;
        hdr.rulesVersion = fp.rulesVersion;
        std::memset(hdr.runtime, 0, sizeof(hdr.runtime));
        std::memcpy(hdr.runtime, fp.gameRuntime.c_str(),
            (std::min)(fp.gameRuntime.size(), sizeof(hdr.runtime) - 1));

        (void)SemanticCacheIO::SaveBinary(CacheBinPath(), hdr, recs);

        const float unknownRatio = recs.empty() ? 0.0f : (static_cast<float>(unknown) / static_cast<float>(recs.size()));
        (void)SemanticCacheIO::SaveMetaJson(CacheMetaPath(), fp, recs.size(), unknownRatio);

        logger::info("[Haptics][SemanticWarmup] rebuilt records={} unknown={} ({:.1f}%)",
            recs.size(), unknown, unknownRatio * 100.0f);

        return true;
    }

    bool SemanticWarmupService::Boot()
    {
        const auto fp = BuildCurrentFingerprint();
        logger::info("[Haptics][SemanticWarmup] cacheBin={}", CacheBinPath().string());
        logger::info("[Haptics][SemanticWarmup] cacheMeta={}", CacheMetaPath().string());
        if (TryLoadCacheAndInstall(fp)) {
            return true;
        }

        return RebuildAndInstall(fp);
    }
}