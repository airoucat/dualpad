#include "pch.h"

#include "input_v2/config/AtomicConfigReloader.h"

#include "input_v2/config/ActionManifestPublisher.h"
#include "input_v2/config/ManifestValidator.h"

#include <cctype>
#include <fstream>
#include <format>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string_view>

namespace logger = SKSE::log;

namespace dualpad::input_v2::config
{
    namespace
    {
        struct DiskLkgRecord
        {
            std::uint32_t schemaVersion{ 1 };
            std::uint64_t manifestEpoch{ 0 };
            std::string bindingsPath;
            std::string menuPolicyPath;
            bool bindingsMissing{ false };
            bool menuPolicyMissing{ false };
            std::string bindingsIni;
            std::string menuPolicyIni;
        };

        std::string ReadFileOrEmpty(const std::filesystem::path& path)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in.is_open()) {
                return {};
            }
            std::ostringstream ss;
            ss << in.rdbuf();
            return ss.str();
        }

        bool WriteFileText(const std::filesystem::path& path, std::string_view contents, std::string& outMessage)
        {
            try {
                std::filesystem::create_directories(path.parent_path());
                std::ofstream out(path, std::ios::binary | std::ios::trunc);
                if (!out.is_open()) {
                    outMessage = std::string("failed to open for write: ") + path.string();
                    return false;
                }
                out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
                outMessage = "ok";
                return true;
            } catch (const std::exception& e) {
                outMessage = std::string("write failed: ") + e.what();
                return false;
            }
        }

        std::string EscapeJsonString(std::string_view text)
        {
            std::ostringstream out;
            out << std::hex;
            for (unsigned char c : std::string(text)) {
                switch (c) {
                case '"': out << "\\\""; break;
                case '\\': out << "\\\\"; break;
                case '\b': out << "\\b"; break;
                case '\f': out << "\\f"; break;
                case '\n': out << "\\n"; break;
                case '\r': out << "\\r"; break;
                case '\t': out << "\\t"; break;
                default:
                    if (c <= 0x1F) {
                        out << "\\u" << std::setw(4) << std::setfill('0') << static_cast<int>(c);
                    } else {
                        out << static_cast<char>(c);
                    }
                }
            }
            return out.str();
        }

        std::string QuoteJson(std::string_view text)
        {
            return std::string("\"") + EscapeJsonString(text) + "\"";
        }

        bool WriteDiskLkgJson(const std::filesystem::path& lkgPath, const DiskLkgRecord& rec, std::string& outMessage)
        {
            std::ostringstream out;
            out << "{\n";
            out << "  \"schemaVersion\": " << rec.schemaVersion << ",\n";
            out << "  \"manifestEpoch\": " << rec.manifestEpoch << ",\n";
            out << "  \"bindingsPath\": " << QuoteJson(rec.bindingsPath) << ",\n";
            out << "  \"menuPolicyPath\": " << QuoteJson(rec.menuPolicyPath) << ",\n";
            out << "  \"bindingsMissing\": " << (rec.bindingsMissing ? "true" : "false") << ",\n";
            out << "  \"menuPolicyMissing\": " << (rec.menuPolicyMissing ? "true" : "false") << ",\n";
            out << "  \"bindingsIni\": " << QuoteJson(rec.bindingsIni) << ",\n";
            out << "  \"menuPolicyIni\": " << QuoteJson(rec.menuPolicyIni) << "\n";
            out << "}\n";
            return WriteFileText(lkgPath, out.str(), outMessage);
        }

        class JsonLiteReader
        {
        public:
            explicit JsonLiteReader(std::string_view text) :
                _p(text.data()),
                _end(text.data() + text.size())
            {
            }

            bool ParseObject(DiskLkgRecord& out, std::string& outError)
            {
                SkipWs();
                if (!Consume('{')) {
                    outError = "expected '{'";
                    return false;
                }
                SkipWs();
                if (TryConsume('}')) {
                    return true;
                }

                while (true) {
                    SkipWs();
                    std::string key;
                    if (!ParseString(key, outError)) {
                        return false;
                    }
                    SkipWs();
                    if (!Consume(':')) {
                        outError = "expected ':'";
                        return false;
                    }
                    SkipWs();

                    if (key == "schemaVersion") {
                        std::uint64_t v = 0;
                        if (!ParseU64(v, outError)) return false;
                        out.schemaVersion = static_cast<std::uint32_t>(v);
                    } else if (key == "manifestEpoch") {
                        std::uint64_t v = 0;
                        if (!ParseU64(v, outError)) return false;
                        out.manifestEpoch = v;
                    } else if (key == "bindingsPath") {
                        if (!ParseString(out.bindingsPath, outError)) return false;
                    } else if (key == "menuPolicyPath") {
                        if (!ParseString(out.menuPolicyPath, outError)) return false;
                    } else if (key == "bindingsMissing") {
                        if (!ParseBool(out.bindingsMissing, outError)) return false;
                    } else if (key == "menuPolicyMissing") {
                        if (!ParseBool(out.menuPolicyMissing, outError)) return false;
                    } else if (key == "bindingsIni") {
                        if (!ParseString(out.bindingsIni, outError)) return false;
                    } else if (key == "menuPolicyIni") {
                        if (!ParseString(out.menuPolicyIni, outError)) return false;
                    } else {
                        if (!SkipValue(outError)) return false;
                    }

                    SkipWs();
                    if (TryConsume('}')) {
                        return true;
                    }
                    if (!Consume(',')) {
                        outError = "expected ',' or '}'";
                        return false;
                    }
                }
            }

        private:
            const char* _p;
            const char* _end;

            void SkipWs()
            {
                while (_p < _end && std::isspace(static_cast<unsigned char>(*_p))) {
                    ++_p;
                }
            }

            bool Consume(char c)
            {
                if (_p >= _end || *_p != c) {
                    return false;
                }
                ++_p;
                return true;
            }

            bool TryConsume(char c)
            {
                if (_p < _end && *_p == c) {
                    ++_p;
                    return true;
                }
                return false;
            }

            bool ParseU64(std::uint64_t& out, std::string& outError)
            {
                SkipWs();
                if (_p >= _end || !std::isdigit(static_cast<unsigned char>(*_p))) {
                    outError = "expected number";
                    return false;
                }
                std::uint64_t value = 0;
                while (_p < _end && std::isdigit(static_cast<unsigned char>(*_p))) {
                    const auto digit = static_cast<std::uint64_t>(*_p - '0');
                    value = value * 10 + digit;
                    ++_p;
                }
                out = value;
                return true;
            }

            bool ParseBool(bool& out, std::string& outError)
            {
                SkipWs();
                if (Match("true")) {
                    out = true;
                    return true;
                }
                if (Match("false")) {
                    out = false;
                    return true;
                }
                outError = "expected boolean";
                return false;
            }

            bool Match(std::string_view token)
            {
                if (static_cast<std::size_t>(_end - _p) < token.size()) {
                    return false;
                }
                if (std::string_view(_p, token.size()) != token) {
                    return false;
                }
                _p += token.size();
                return true;
            }

            bool ParseString(std::string& out, std::string& outError)
            {
                SkipWs();
                if (!Consume('"')) {
                    outError = "expected string";
                    return false;
                }

                std::string value;
                while (_p < _end) {
                    const char c = *_p++;
                    if (c == '"') {
                        out = std::move(value);
                        return true;
                    }
                    if (c == '\\') {
                        if (_p >= _end) {
                            outError = "unexpected end of string escape";
                            return false;
                        }
                        const char esc = *_p++;
                        switch (esc) {
                        case '"': value.push_back('"'); break;
                        case '\\': value.push_back('\\'); break;
                        case '/': value.push_back('/'); break;
                        case 'b': value.push_back('\b'); break;
                        case 'f': value.push_back('\f'); break;
                        case 'n': value.push_back('\n'); break;
                        case 'r': value.push_back('\r'); break;
                        case 't': value.push_back('\t'); break;
                        case 'u':
                            // Minimal support: accept \u00XX for control characters we emit.
                            if (static_cast<std::size_t>(_end - _p) < 4) {
                                outError = "short \\u escape";
                                return false;
                            }
                            {
                                int code = 0;
                                for (int i = 0; i < 4; ++i) {
                                    const char h = _p[i];
                                    code <<= 4;
                                    if (h >= '0' && h <= '9') code |= (h - '0');
                                    else if (h >= 'a' && h <= 'f') code |= (h - 'a' + 10);
                                    else if (h >= 'A' && h <= 'F') code |= (h - 'A' + 10);
                                    else {
                                        outError = "invalid hex in \\u escape";
                                        return false;
                                    }
                                }
                                _p += 4;
                                value.push_back(static_cast<char>(code & 0xFF));
                            }
                            break;
                        default:
                            outError = "unsupported escape sequence";
                            return false;
                        }
                        continue;
                    }
                    value.push_back(c);
                }

                outError = "unterminated string";
                return false;
            }

            bool SkipValue(std::string& outError)
            {
                SkipWs();
                if (_p >= _end) {
                    outError = "unexpected end of input";
                    return false;
                }

                if (*_p == '"') {
                    std::string tmp;
                    return ParseString(tmp, outError);
                }
                if (std::isdigit(static_cast<unsigned char>(*_p))) {
                    std::uint64_t tmp = 0;
                    return ParseU64(tmp, outError);
                }
                if (Match("true") || Match("false") || Match("null")) {
                    return true;
                }

                outError = "unsupported json value type";
                return false;
            }
        };

        std::string WarningSummary(const std::vector<std::string>& warnings)
        {
            if (warnings.empty()) {
                return {};
            }

            std::ostringstream out;
            out << "warnings: ";
            for (std::size_t i = 0; i < warnings.size(); ++i) {
                if (i != 0) {
                    out << " | ";
                }
                out << warnings[i];
            }
            return out.str();
        }
    }

    AtomicConfigReloader& AtomicConfigReloader::GetSingleton()
    {
        static AtomicConfigReloader instance;
        return instance;
    }

    void AtomicConfigReloader::ResetForTests()
    {
        std::scoped_lock lock(_mutex);
        _bindingsPath.clear();
        _menuPolicyPath.clear();
        _activeBundle.reset();
        _lastKnownGoodBundle.reset();
        _currentEpoch = 0;
    }

    std::filesystem::path AtomicConfigReloader::ResolveBindingsPath(const std::filesystem::path& overridePath) const
    {
        if (!overridePath.empty()) {
            return overridePath;
        }
        return LegacyIniImporter::DefaultBindingsPath();
    }

    std::filesystem::path AtomicConfigReloader::ResolveMenuPolicyPath(const std::filesystem::path& overridePath) const
    {
        if (!overridePath.empty()) {
            return overridePath;
        }
        return LegacyIniImporter::DefaultMenuPolicyPath();
    }

    std::filesystem::path AtomicConfigReloader::ResolveDiskLkgPath(
        const std::filesystem::path& bindingsPath,
        const std::filesystem::path& menuPolicyPath) const
    {
        // Phase 1 rule: disk LKG lives next to deployed config files.
        // Prefer the bindings directory when possible.
        auto dir = bindingsPath.parent_path();
        if (dir.empty()) {
            dir = menuPolicyPath.parent_path();
        }
        if (dir.empty()) {
            dir = ".";
        }
        return dir / kDiskLkgFilename;
    }

    std::shared_ptr<const CompiledConfigBundle> AtomicConfigReloader::GetActiveBundleSnapshot() const
    {
        std::scoped_lock lock(_mutex);
        return _activeBundle;
    }

    std::optional<std::uint64_t> AtomicConfigReloader::GetActiveEpoch() const
    {
        std::scoped_lock lock(_mutex);
        if (!_activeBundle) {
            return std::nullopt;
        }
        return _activeBundle->manifestEpoch;
    }

    LoadOrRecoverResult AtomicConfigReloader::LoadOrRecover(
        const std::filesystem::path& bindingsPath,
        const std::filesystem::path& menuPolicyPath)
    {
        return LoadOrRecoverImpl(bindingsPath, menuPolicyPath, true);
    }

    LoadOrRecoverResult AtomicConfigReloader::Reload()
    {
        std::filesystem::path bindings;
        std::filesystem::path policy;
        {
            std::scoped_lock lock(_mutex);
            bindings = _bindingsPath;
            policy = _menuPolicyPath;
        }

        return LoadOrRecoverImpl(bindings, policy, false);
    }

    LoadOrRecoverResult AtomicConfigReloader::LoadOrRecoverImpl(
        const std::filesystem::path& bindingsPath,
        const std::filesystem::path& menuPolicyPath,
        bool isStartup)
    {
        const auto resolvedBindings = ResolveBindingsPath(bindingsPath);
        const auto resolvedPolicy = ResolveMenuPolicyPath(menuPolicyPath);
        const auto diskLkgPath = ResolveDiskLkgPath(resolvedBindings, resolvedPolicy);

        {
            std::scoped_lock lock(_mutex);
            _bindingsPath = resolvedBindings;
            _menuPolicyPath = resolvedPolicy;
        }

        // Determine candidate epoch in a lock-protected way, but compile outside locks.
        std::uint64_t candidateEpoch = 1;
        {
            std::scoped_lock lock(_mutex);
            candidateEpoch = (_currentEpoch == 0) ? 1 : (_currentEpoch + 1);
        }

        logger::info(
            "[DualPad][PH1][Reloader] scratch compile start (startup={} epoch={} bindings='{}' policy='{}')",
            isStartup,
            candidateEpoch,
            resolvedBindings.string(),
            resolvedPolicy.string());

        const auto scratch = ScratchCompileBundle(resolvedBindings, resolvedPolicy, candidateEpoch);
        if (scratch.ok && scratch.bundle) {
            std::string promoteMessage;
            if (!Promote(scratch.bundle, candidateEpoch, promoteMessage)) {
                LoadOrRecoverResult r{};
                r.ok = false;
                r.message = std::move(promoteMessage);
                return r;
            }

            // Best-effort disk persistence.
            std::string persistMessage;
            if (!TryWriteDiskLkg(diskLkgPath, *scratch.bundle, persistMessage)) {
                logger::warn("[DualPad][PH1][Reloader] LKG persist warning: {}", persistMessage);
            }

            LoadOrRecoverResult r{};
            r.ok = true;
            r.message = scratch.message;
            return r;
        }

        logger::warn("[DualPad][PH1][Reloader] scratch compile failed: {}", scratch.message);

        // If we already have an active bundle, keep it.
        {
            std::scoped_lock lock(_mutex);
            if (_activeBundle) {
                LoadOrRecoverResult r{};
                r.ok = false;
                r.message = std::string("compile failed; keeping existing active bundle: ") + scratch.message;
                return r;
            }
        }

        // Startup recovery path: prefer disk LKG.
        if (isStartup) {
            std::string loadMessage;
            const auto loaded = TryLoadDiskLkg(diskLkgPath, loadMessage);
            if (loaded.ok && loaded.bundle) {
                logger::warn(
                    "[DualPad][PH1][Reloader] recovered from disk last-known-good bundle: {}",
                    diskLkgPath.string());

                std::string promoteMessage;
                if (!Promote(loaded.bundle, loaded.bundle->manifestEpoch, promoteMessage)) {
                    LoadOrRecoverResult r{};
                    r.ok = false;
                    r.message = std::move(promoteMessage);
                    return r;
                }

                LoadOrRecoverResult r{};
                r.ok = true;
                r.recoveredFromDiskLkg = true;
                r.message = "recovered-from-lkg";
                return r;
            }

            // Only "missing config files" is allowed to fall back to built-in defaults.
            const bool bindingsMissing = !std::filesystem::exists(resolvedBindings);
            const bool policyMissing = !std::filesystem::exists(resolvedPolicy);
            if (bindingsMissing && policyMissing) {
                logger::warn(
                    "[DualPad][PH1][Reloader] config files missing; attempting built-in defaults compile");
                const auto defaultsScratch = ScratchCompileBundle(resolvedBindings, resolvedPolicy, candidateEpoch);
                if (defaultsScratch.ok && defaultsScratch.bundle) {
                    std::string promoteMessage;
                    if (!Promote(defaultsScratch.bundle, candidateEpoch, promoteMessage)) {
                        LoadOrRecoverResult r{};
                        r.ok = false;
                        r.message = std::move(promoteMessage);
                        return r;
                    }
                    LoadOrRecoverResult r{};
                    r.ok = true;
                    r.message = "ok-defaults";
                    return r;
                }
            }

            LoadOrRecoverResult r{};
            r.ok = false;
            r.message = std::string("startup compile failed and no last-known-good available: ") + scratch.message;
            return r;
        }

        LoadOrRecoverResult r{};
        r.ok = false;
        r.message = scratch.message;
        return r;
    }

    AtomicConfigReloader::ScratchCompile AtomicConfigReloader::ScratchCompileBundle(
        const std::filesystem::path& bindingsPath,
        const std::filesystem::path& menuPolicyPath,
        std::uint64_t candidateEpoch) const
    {
        ScratchCompile result{};
        const auto importResult = LegacyIniImporter::Import(bindingsPath, menuPolicyPath);
        if (!importResult.ok) {
            result.ok = false;
            result.message = std::string("import failed: ") + importResult.message;
            return result;
        }
        for (const auto& warning : importResult.bundle.warnings) {
            logger::warn("[DualPad][PH1][Importer] {}", warning);
        }

        const auto importedValidation = ManifestValidator::ValidateImportedAst(importResult.bundle);
        if (!importedValidation.ok) {
            result.ok = false;
            result.message = std::string("import validation failed: ") + importedValidation.message;
            return result;
        }

        auto bundle = std::make_shared<CompiledConfigBundle>();
        bundle->manifestEpoch = candidateEpoch;
        bundle->imported = importResult.bundle;

        const auto catalogRes = context::ContextCatalog::Compile(importResult.bundle.menuPolicy, candidateEpoch);
        if (!catalogRes.ok) {
            result.ok = false;
            result.message = std::string("catalog compile failed: ") + catalogRes.message;
            return result;
        }
        bundle->catalog = catalogRes.catalog;

        const auto manifestRes = actions::ActionManifest::Compile(bundle->catalog, importResult.bundle.bindings, candidateEpoch);
        if (!manifestRes.ok) {
            result.ok = false;
            result.message = std::string("manifest compile failed: ") + manifestRes.message;
            return result;
        }
        bundle->manifest = manifestRes.manifest;

        const auto compiledValidation = ManifestValidator::ValidateCompiledBundle(bundle->catalog, bundle->manifest);
        if (!compiledValidation.ok) {
            result.ok = false;
            result.message = std::string("compiled validation failed: ") + compiledValidation.message;
            return result;
        }

        result.ok = true;
        result.message = importResult.bundle.warnings.empty() ? "ok" : std::string("ok; ") + WarningSummary(importResult.bundle.warnings);
        result.bundle = std::move(bundle);
        return result;
    }

    bool AtomicConfigReloader::Promote(
        const std::shared_ptr<CompiledConfigBundle>& compiled,
        std::uint64_t promotedEpoch,
        std::string& outMessage)
    {
        if (!compiled) {
            outMessage = "Promote called with null bundle";
            return false;
        }

        if (compiled->manifestEpoch != promotedEpoch ||
            compiled->catalog.manifestEpoch != promotedEpoch ||
            compiled->manifest.manifestEpoch != promotedEpoch ||
            compiled->manifest.legacyBindingProjection.manifestEpoch != promotedEpoch) {
            outMessage = std::format(
                "Promote epoch mismatch: arg={} bundle={} catalog={} manifest={} projection={}",
                promotedEpoch,
                compiled->manifestEpoch,
                compiled->catalog.manifestEpoch,
                compiled->manifest.manifestEpoch,
                compiled->manifest.legacyBindingProjection.manifestEpoch);
            return false;
        }

        // Publish first so no compatibility facade can observe the new active bundle
        // before the ManifestEpochChanged producer seam has accepted the epoch.
        if (!ActionManifestPublisher::GetSingleton().PublishPromotedBundle(*compiled, promotedEpoch)) {
            outMessage = std::format("PublishPromotedBundle failed for manifest epoch {}", promotedEpoch);
            return false;
        }

        // Promote must be an atomic pointer swap; keep lock scope minimal.
        {
            std::scoped_lock lock(_mutex);
            _activeBundle = compiled;
            _lastKnownGoodBundle = compiled;
            _currentEpoch = promotedEpoch;
        }

        outMessage = "ok";
        logger::info("[DualPad][PH1][Reloader] promoted manifest epoch {}", promotedEpoch);
        return true;
    }

    bool AtomicConfigReloader::TryWriteDiskLkg(
        const std::filesystem::path& lkgPath,
        const CompiledConfigBundle& bundle,
        std::string& outMessage) const
    {
        DiskLkgRecord rec{};
        rec.schemaVersion = 1;
        rec.manifestEpoch = bundle.manifestEpoch;
        rec.bindingsPath = bundle.imported.bindingsPath.string();
        rec.menuPolicyPath = bundle.imported.menuPolicyPath.string();
        rec.bindingsMissing = bundle.imported.bindingsMissing;
        rec.menuPolicyMissing = bundle.imported.menuPolicyMissing;

        if (!rec.bindingsMissing) {
            rec.bindingsIni = ReadFileOrEmpty(bundle.imported.bindingsPath);
        }
        if (!rec.menuPolicyMissing) {
            rec.menuPolicyIni = ReadFileOrEmpty(bundle.imported.menuPolicyPath);
        }

        return WriteDiskLkgJson(lkgPath, rec, outMessage);
    }

    AtomicConfigReloader::ScratchCompile AtomicConfigReloader::TryLoadDiskLkg(
        const std::filesystem::path& lkgPath,
        std::string& outMessage) const
    {
        ScratchCompile result{};
        if (!std::filesystem::exists(lkgPath)) {
            result.ok = false;
            outMessage = std::string("LKG missing: ") + lkgPath.string();
            result.message = outMessage;
            return result;
        }

        const auto jsonText = ReadFileOrEmpty(lkgPath);
        if (jsonText.empty()) {
            result.ok = false;
            result.message = std::string("failed to read LKG file: ") + lkgPath.string();
            outMessage = result.message;
            return result;
        }

        DiskLkgRecord rec{};
        std::string parseError;
        JsonLiteReader reader(jsonText);
        if (!reader.ParseObject(rec, parseError)) {
            result.ok = false;
            result.message = std::string("failed to parse LKG json: ") + parseError;
            outMessage = result.message;
            return result;
        }
        if (rec.schemaVersion != 1) {
            result.ok = false;
            result.message = std::format("unsupported LKG schemaVersion {}", rec.schemaVersion);
            outMessage = result.message;
            return result;
        }
        if (rec.manifestEpoch == 0) {
            result.ok = false;
            result.message = "LKG manifestEpoch missing/zero";
            outMessage = result.message;
            return result;
        }

        // Re-import from recovered INI text into the normal compiler pipeline,
        // so Phase 1 invariants remain enforced on startup recovery.
        const auto tempBindings = lkgPath.parent_path() / "DualPad.Manifest.lkg.bindings.ini";
        const auto tempPolicy = lkgPath.parent_path() / "DualPad.Manifest.lkg.menu_policy.ini";

        std::string tmpMessage;
        if (rec.bindingsMissing) {
            std::error_code ec;
            std::filesystem::remove(tempBindings, ec);
        } else {
            if (!WriteFileText(tempBindings, rec.bindingsIni, tmpMessage)) {
                result.ok = false;
                result.message = std::string("failed to write temp bindings ini: ") + tmpMessage;
                outMessage = result.message;
                return result;
            }
        }
        if (rec.menuPolicyMissing) {
            std::error_code ec;
            std::filesystem::remove(tempPolicy, ec);
        } else {
            if (!WriteFileText(tempPolicy, rec.menuPolicyIni, tmpMessage)) {
                result.ok = false;
                result.message = std::string("failed to write temp menu policy ini: ") + tmpMessage;
                outMessage = result.message;
                return result;
            }
        }

        const auto importRes = LegacyIniImporter::Import(tempBindings, tempPolicy);
        if (!importRes.ok) {
            result.ok = false;
            result.message = std::string("LKG import failed: ") + importRes.message;
            outMessage = result.message;
            return result;
        }

        auto bundle = std::make_shared<CompiledConfigBundle>();
        bundle->manifestEpoch = rec.manifestEpoch;
        bundle->imported = importRes.bundle;
        // Preserve the originally recorded paths for diagnostics.
        if (!rec.bindingsPath.empty()) {
            bundle->imported.bindingsPath = rec.bindingsPath;
        }
        if (!rec.menuPolicyPath.empty()) {
            bundle->imported.menuPolicyPath = rec.menuPolicyPath;
        }
        bundle->imported.bindingsMissing = rec.bindingsMissing;
        bundle->imported.menuPolicyMissing = rec.menuPolicyMissing;

        const auto importedValidation = ManifestValidator::ValidateImportedAst(bundle->imported);
        if (!importedValidation.ok) {
            result.ok = false;
            result.message = std::string("LKG import validation failed: ") + importedValidation.message;
            outMessage = result.message;
            return result;
        }

        const auto catalogRes = context::ContextCatalog::Compile(bundle->imported.menuPolicy, bundle->manifestEpoch);
        if (!catalogRes.ok) {
            result.ok = false;
            result.message = std::string("LKG catalog compile failed: ") + catalogRes.message;
            outMessage = result.message;
            return result;
        }
        bundle->catalog = catalogRes.catalog;

        const auto manifestRes = actions::ActionManifest::Compile(bundle->catalog, bundle->imported.bindings, bundle->manifestEpoch);
        if (!manifestRes.ok) {
            result.ok = false;
            result.message = std::string("LKG manifest compile failed: ") + manifestRes.message;
            outMessage = result.message;
            return result;
        }
        bundle->manifest = manifestRes.manifest;

        const auto compiledValidation = ManifestValidator::ValidateCompiledBundle(bundle->catalog, bundle->manifest);
        if (!compiledValidation.ok) {
            result.ok = false;
            result.message = std::string("LKG compiled validation failed: ") + compiledValidation.message;
            outMessage = result.message;
            return result;
        }

        result.ok = true;
        result.message = "ok";
        outMessage = "ok";
        result.bundle = std::move(bundle);
        return result;
    }
}
