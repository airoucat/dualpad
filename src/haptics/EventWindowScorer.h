#pragma once

#include "haptics/HapticsTypes.h"
#include "haptics/EventShortWindowCache.h"
#include "haptics/SubmitFeatureCache.h"
#include "haptics/HapticTemplateCache.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace dualpad::haptics
{
    class EventWindowScorer
    {
    public:
        struct Stats
        {
            std::uint64_t audioFeaturesPulled{ 0 };
            std::uint64_t sourcesProduced{ 0 };

            std::uint64_t eventsPulled{ 0 };
            std::uint64_t matched{ 0 };
            std::uint64_t unmatched{ 0 };
            std::uint64_t passthroughSources{ 0 };

            std::uint32_t activeWindows{ 0 };

            std::uint64_t totalEvents{ 0 };
            std::uint64_t totalMatched{ 0 };
            std::uint64_t totalUnmatched{ 0 };
        };

        static EventWindowScorer& GetSingleton();

        void Initialize();
        void Shutdown();

        std::vector<HapticSourceMsg> Update();

        Stats GetStats() const;
        void ResetStats();

    private:
        EventWindowScorer();

        struct PendingEvent
        {
            EventToken token{};
            std::uint64_t deadlineUs{ 0 };
        };

        struct RuntimeParams
        {
            std::uint32_t correctionWindowUs{ 30'000 };
            std::uint32_t lookbackUs{ 8'000 };
            float acceptScore{ 0.38f };

            float wTiming{ 0.62f };
            float wEnergy{ 0.30f };
            float wPan{ 0.08f };
            float timingTauUs{ 18'000.0f };

            float immediateGain{ 1.0f };
            float correctionGain{ 1.0f };

            bool audioDrivenPreferAudioOnly{ true };
            bool fallbackBaseWhenNoMatch{ true };
            bool enableAmbientPassthrough{ false };
        };

        static EventToken ToEventToken(const EventMsg& e, std::uint64_t id);
        static AudioChunkFeature ToAudioChunk(const AudioFeatureMsg& a);

        float BaseAmpFor(EventType t, float intensity) const;
        std::uint32_t BaseTtlFor(EventType t) const;
        int PriorityFor(EventType t) const;

        HapticSourceMsg MakeImmediateSource(const EventToken& e) const;
        HapticSourceMsg MakeCorrectionSource(const EventToken& e, const AudioChunkFeature& a, float score) const;

        float Score(const EventToken& e, const AudioChunkFeature& a) const;
        void ReloadRuntimeParamsFromConfig();

        EventShortWindowCache& _eventCache;
        SubmitFeatureCache& _submitCache;
        HapticTemplateCache& _templateCache;

        mutable std::mutex _pendingMtx;
        std::vector<PendingEvent> _pending;

        RuntimeParams _rp{};

        std::atomic<bool> _initialized{ false };
        std::atomic<std::uint64_t> _nextEventId{ 1 };

        mutable std::atomic<std::uint64_t> _audioFeaturesPulled{ 0 };
        mutable std::atomic<std::uint64_t> _sourcesProduced{ 0 };
        mutable std::atomic<std::uint64_t> _eventsPulled{ 0 };
        mutable std::atomic<std::uint64_t> _matched{ 0 };
        mutable std::atomic<std::uint64_t> _unmatched{ 0 };
        mutable std::atomic<std::uint64_t> _passthroughSources{ 0 };
    };
}