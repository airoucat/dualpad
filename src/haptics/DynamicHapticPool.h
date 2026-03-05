#pragma once

#include "haptics/HapticsTypes.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace dualpad::haptics
{
    class DynamicHapticPool
    {
    public:
        struct Stats
        {
            std::uint64_t observeCalls{ 0 };
            std::uint64_t admitted{ 0 };
            std::uint64_t rejectedNoKey{ 0 };
            std::uint64_t rejectedLowConfidence{ 0 };
            std::uint64_t shadowCalls{ 0 };
            std::uint64_t shadowHits{ 0 };
            std::uint64_t shadowMisses{ 0 };
            std::uint64_t resolveCalls{ 0 };
            std::uint64_t resolveHits{ 0 };
            std::uint64_t resolveMisses{ 0 };
            std::uint64_t evicted{ 0 };
            std::uint64_t currentSize{ 0 };
        };

        static DynamicHapticPool& GetSingleton();

        void Configure(
            bool enabled,
            std::uint32_t topK,
            float minConfidence,
            float outputCap);

        void ObserveL1(const HapticSourceMsg& source, float matchScore);
        bool ShadowCanResolve(const HapticSourceMsg& input);
        bool TryResolve(const HapticSourceMsg& input, HapticSourceMsg& output);

        Stats GetStats() const;
        void ResetStats();
        void Clear();

    private:
        DynamicHapticPool() = default;

        struct Entry
        {
            std::uint32_t sourceFormId{ 0 };
            EventType eventType{ EventType::Unknown };
            float left{ 0.0f };
            float right{ 0.0f };
            float confidence{ 0.0f };
            int priority{ 50 };
            std::uint32_t ttlMs{ 80 };
            float score{ 0.0f };
            std::uint32_t hits{ 0 };
            std::uint64_t lastSeenUs{ 0 };
        };

        static float Clamp01(float v);
        static std::uint64_t MakeKey(const HapticSourceMsg& source);

        void EvictOneLocked();

        mutable std::mutex _mutex;
        std::unordered_map<std::uint64_t, Entry> _entries;

        bool _enabled{ true };
        std::uint32_t _topK{ 64 };
        float _minConfidence{ 0.80f };
        float _outputCap{ 0.75f };

        std::atomic<std::uint64_t> _observeCalls{ 0 };
        std::atomic<std::uint64_t> _admitted{ 0 };
        std::atomic<std::uint64_t> _rejectedNoKey{ 0 };
        std::atomic<std::uint64_t> _rejectedLowConfidence{ 0 };
        std::atomic<std::uint64_t> _shadowCalls{ 0 };
        std::atomic<std::uint64_t> _shadowHits{ 0 };
        std::atomic<std::uint64_t> _shadowMisses{ 0 };
        std::atomic<std::uint64_t> _resolveCalls{ 0 };
        std::atomic<std::uint64_t> _resolveHits{ 0 };
        std::atomic<std::uint64_t> _resolveMisses{ 0 };
        std::atomic<std::uint64_t> _evicted{ 0 };
    };
}
