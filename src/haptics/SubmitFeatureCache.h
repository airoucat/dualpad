#pragma once

#include "haptics/HapticsTypes.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace dualpad::haptics
{
    class SubmitFeatureCache
    {
    public:
        struct Config
        {
            std::size_t capacity{ 256 };            // 推荐 128~512
            std::uint32_t windowUs{ 350'000 };      // 默认 350ms
            std::uint32_t voiceProfileTtlUs{ 10'000'000 }; // 10s
        };

        struct VoiceProfile
        {
            VoiceKey voice{};
            std::uint64_t seenCount{ 0 };
            float avgRms{ 0.0f };
            float avgPeak{ 0.0f };
            float avgDurationUs{ 0.0f };
            float avgLrImbalance{ 0.0f };  // |L-R|/(L+R+eps)
            std::uint64_t lastSeenUs{ 0 };
        };

        static SubmitFeatureCache& GetSingleton();

        void Initialize(const Config& cfg = {});
        void Shutdown();

        // 写入 submit 特征
        bool Push(const AudioChunkFeature& f);

        // 按时间范围查候选
        std::vector<AudioChunkFeature> QueryByTime(
            std::uint64_t tRefUs,
            std::uint32_t lookbackUs = 300'000,
            std::uint32_t lookaheadUs = 30'000,
            std::size_t maxOut = 32) const;

        // 按 voice + 时间范围查候选
        std::vector<AudioChunkFeature> QueryByVoiceAndTime(
            VoiceKey voice,
            std::uint64_t tRefUs,
            std::uint32_t lookbackUs = 300'000,
            std::uint32_t lookaheadUs = 30'000,
            std::size_t maxOut = 32) const;

        bool GetVoiceProfile(VoiceKey voice, VoiceProfile& out) const;

        std::size_t SizeApprox() const;
        std::size_t Capacity() const;

        // 统计
        std::uint64_t GetPushCount() const { return _pushCount.load(std::memory_order_relaxed); }
        std::uint64_t GetOverwriteCount() const { return _overwriteCount.load(std::memory_order_relaxed); }
        std::uint64_t GetPruneCount() const { return _pruneCount.load(std::memory_order_relaxed); }
        std::uint64_t GetQueryCount() const { return _queryCount.load(std::memory_order_relaxed); }
        std::uint64_t GetQueryHitCount() const { return _queryHitCount.load(std::memory_order_relaxed); }

        void ResetStats();

    private:
        SubmitFeatureCache() = default;
        SubmitFeatureCache(const SubmitFeatureCache&) = delete;
        SubmitFeatureCache& operator=(const SubmitFeatureCache&) = delete;

        struct VoiceKeyHash
        {
            std::size_t operator()(const VoiceKey& k) const noexcept
            {
                return std::hash<std::uintptr_t>{}(k.voicePtr) ^
                    (std::hash<std::uint32_t>{}(k.generation) << 1);
            }
        };

        struct VoiceProfileInternal
        {
            std::uint64_t seenCount{ 0 };
            float avgRms{ 0.0f };
            float avgPeak{ 0.0f };
            float avgDurationUs{ 0.0f };
            float avgLrImbalance{ 0.0f };
            std::uint64_t lastSeenUs{ 0 };
        };

        void PruneExpiredLocked(std::uint64_t nowUs);
        void UpdateVoiceProfileLocked(const AudioChunkFeature& f);
        void PruneVoiceProfilesLocked(std::uint64_t nowUs);

        mutable std::mutex _mtx;
        std::vector<AudioChunkFeature> _buf; // ring
        std::size_t _head{ 0 };
        std::size_t _size{ 0 };
        Config _cfg{};

        std::unordered_map<VoiceKey, VoiceProfileInternal, VoiceKeyHash> _voiceProfiles;
        std::uint64_t _pushSinceLastVoicePrune{ 0 };

        std::atomic<bool> _initialized{ false };

        mutable std::atomic<std::uint64_t> _pushCount{ 0 };
        mutable std::atomic<std::uint64_t> _overwriteCount{ 0 };
        mutable std::atomic<std::uint64_t> _pruneCount{ 0 };
        mutable std::atomic<std::uint64_t> _queryCount{ 0 };
        mutable std::atomic<std::uint64_t> _queryHitCount{ 0 };
    };
}  // namespace dualpad::haptics