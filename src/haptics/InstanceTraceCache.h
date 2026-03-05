#pragma once
#include "haptics/HapticsTypes.h"
#include <atomic>
#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <unordered_map>

namespace dualpad::haptics
{
    struct TraceMeta
    {
        std::uint64_t instanceId{ 0 };
        std::uint32_t soundFormId{ 0 };
        std::uint32_t sourceFormId{ 0 };
        EventType preferredEvent{ EventType::Unknown };
        SemanticGroup semantic{ SemanticGroup::Unknown };
        float confidence{ 0.5f };
        std::uintptr_t initObjectPtr{ 0 };
        std::uint16_t flags{ 0 };
        std::uint64_t tsUs{ 0 };
    };

    class InstanceTraceCache
    {
    public:
        static InstanceTraceCache& GetSingleton();

        void Upsert(const TraceMeta& m);
        std::optional<TraceMeta> TryGet(std::uint64_t instanceId) const;
        void Erase(std::uint64_t instanceId);
        void Clear();

        struct Stats {
            std::uint64_t upserts{ 0 };
            std::uint64_t hits{ 0 };
            std::uint64_t misses{ 0 };
        };
        Stats GetStats() const;

    private:
        InstanceTraceCache() = default;

        mutable std::shared_mutex _mx;
        std::unordered_map<std::uint64_t, TraceMeta> _map;

        mutable std::atomic<std::uint64_t> _upserts{ 0 };
        mutable std::atomic<std::uint64_t> _hits{ 0 };
        mutable std::atomic<std::uint64_t> _misses{ 0 };
    };
}
