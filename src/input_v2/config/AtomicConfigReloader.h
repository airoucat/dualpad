#pragma once

#include "input_v2/actions/ActionManifest.h"
#include "input_v2/config/LegacyIniImporter.h"
#include "input_v2/context/ContextCatalog.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace dualpad::input_v2::config
{
    struct CompiledConfigBundle
    {
        std::uint64_t manifestEpoch{ 0 };

        // Imported inputs (for diagnostics and for persisting last-known-good).
        LegacyImportBundle imported;

        // Compiled outputs (Phase 1 truth).
        context::CompiledContextCatalog catalog;
        actions::CompiledActionManifest manifest;
    };

    struct LoadOrRecoverResult
    {
        bool ok{ false };
        std::string message;
        bool recoveredFromDiskLkg{ false };
    };

    // Phase 1: compiles legacy INI inputs into an immutable compiled bundle and atomically promotes it.
    //
    // Hard constraints:
    // - Promote() only manages active bundle pointer swap / epoch / last-known-good.
    // - ActionManifestPublisher::PublishPromotedBundle(...) is the only ManifestEpochChanged producer seam.
    class AtomicConfigReloader
    {
    public:
        static AtomicConfigReloader& GetSingleton();

        LoadOrRecoverResult LoadOrRecover(
            const std::filesystem::path& bindingsPath = {},
            const std::filesystem::path& menuPolicyPath = {});

        LoadOrRecoverResult Reload();

        std::shared_ptr<const CompiledConfigBundle> GetActiveBundleSnapshot() const;
        std::optional<std::uint64_t> GetActiveEpoch() const;

        // Test-only helper: clears active/LKG state. Avoid calling at runtime.
        void ResetForTests();

    private:
        AtomicConfigReloader() = default;

        static constexpr const char* kDiskLkgFilename = "DualPad.Manifest.lkg.json";

        LoadOrRecoverResult LoadOrRecoverImpl(
            const std::filesystem::path& bindingsPath,
            const std::filesystem::path& menuPolicyPath,
            bool isStartup);

        std::filesystem::path ResolveBindingsPath(const std::filesystem::path& overridePath) const;
        std::filesystem::path ResolveMenuPolicyPath(const std::filesystem::path& overridePath) const;
        std::filesystem::path ResolveDiskLkgPath(
            const std::filesystem::path& bindingsPath,
            const std::filesystem::path& menuPolicyPath) const;

        struct ScratchCompile
        {
            bool ok{ false };
            std::string message;
            std::shared_ptr<CompiledConfigBundle> bundle;
        };

        ScratchCompile ScratchCompileBundle(
            const std::filesystem::path& bindingsPath,
            const std::filesystem::path& menuPolicyPath,
            std::uint64_t candidateEpoch) const;

        bool Promote(const std::shared_ptr<CompiledConfigBundle>& compiled, std::uint64_t promotedEpoch, std::string& outMessage);

        bool TryWriteDiskLkg(
            const std::filesystem::path& lkgPath,
            const CompiledConfigBundle& bundle,
            std::string& outMessage) const;
        ScratchCompile TryLoadDiskLkg(
            const std::filesystem::path& lkgPath,
            std::string& outMessage) const;

        mutable std::mutex _mutex;
        std::filesystem::path _bindingsPath;
        std::filesystem::path _menuPolicyPath;

        std::shared_ptr<const CompiledConfigBundle> _activeBundle;
        std::shared_ptr<const CompiledConfigBundle> _lastKnownGoodBundle;
        std::uint64_t _currentEpoch{ 0 };
    };
}

