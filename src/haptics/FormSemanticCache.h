#pragma once

#include "haptics/SemanticRuleEngine.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace dualpad::haptics
{
    struct CachePluginState
    {
        std::string name;              // plugin file name
        std::uint32_t loadOrder{ 0 };
        std::uint64_t timestamp{ 0 };  // file write time
        std::uint64_t size{ 0 };       // file size
    };

    struct CacheFingerprint
    {
        std::string gameVersion;       // e.g. "SE 1.5.97"
        std::uint32_t ruleVersion{ 0 };
        std::vector<CachePluginState> plugins;
        std::uint64_t hash{ 0 };
    };

    class FormSemanticCache
    {
    public:
        struct Stats
        {
            std::uint64_t lookups{ 0 };
            std::uint64_t hits{ 0 };
            std::uint64_t misses{ 0 };
            std::uint64_t loads{ 0 };
            std::uint64_t rebuilds{ 0 };
        };

        static FormSemanticCache& GetSingleton();

        // Boot path: try disk cache; rebuild on mismatch/failure.
        bool Initialize();

        // Build from current loaded forms + rule engine.
        void BuildCache();

        // Runtime O(1) lookup.
        bool TryGet(std::uint32_t formID, FormSemanticMeta& outMeta) const;

        bool SaveCache(const std::filesystem::path& path) const;
        bool LoadCache(const std::filesystem::path& path);
        bool ValidateFingerprint() const;

        std::size_t Size() const;
        Stats GetStats() const;
        void ResetStats();

        CacheFingerprint GetFingerprint() const;
        std::uint32_t GetCacheVersion() const { return kCacheVersion; }

    private:
        struct Snapshot
        {
            std::unordered_map<std::uint32_t, FormSemanticMeta> table;
            CacheFingerprint fingerprint;
        };

        static constexpr std::uint32_t kCacheVersion = 1;

        FormSemanticCache() = default;

        static CacheFingerprint BuildCurrentFingerprint(std::uint32_t ruleVersion);
        static std::uint64_t HashFingerprint(const CacheFingerprint& fp);
        void InstallSnapshot(std::shared_ptr<const Snapshot> snapshot);

        std::shared_ptr<const Snapshot> _snapshot;
        std::atomic<bool> _initialized{ false };

        mutable std::atomic<std::uint64_t> _lookups{ 0 };
        mutable std::atomic<std::uint64_t> _hits{ 0 };
        mutable std::atomic<std::uint64_t> _misses{ 0 };
        mutable std::atomic<std::uint64_t> _loads{ 0 };
        mutable std::atomic<std::uint64_t> _rebuilds{ 0 };
    };
}

