#pragma once
#include "haptics/HapticsTypes.h"
#include <atomic>
#include <vector>

namespace dualpad::haptics
{
    enum class DecisionLayer : std::uint8_t
    {
        L1Trace = 1,
        L2Match = 2,
        L3Fallback = 3
    };

    enum class DecisionReason : std::uint8_t
    {
        L1FormSemantic,
        L1TraceHit,
        L1TraceMiss,
        L2HighScore,
        L2MidScore,
        L2LowScorePass,
        L3LowScoreFallback,
        L1Disabled,
        NoCandidate,
        L1TraceMissUnbound,
        L1TraceMissExpired,
    };

    struct DecisionResult
    {
        HapticSourceMsg source{};
        DecisionLayer layer{ DecisionLayer::L2Match };
        DecisionReason reason{ DecisionReason::NoCandidate };
        float matchScore{ 0.0f };
        bool traceHit{ false };
    };

    class DecisionEngine
    {
    public:
        static DecisionEngine& GetSingleton();

        void Initialize();
        void Shutdown();

        std::vector<DecisionResult> Update();

        struct Stats
        {
            std::uint64_t l1Count{ 0 };
            std::uint64_t l2Count{ 0 };
            std::uint64_t l3Count{ 0 };
            std::uint64_t noCandidate{ 0 };
            std::uint64_t lowScoreFallback{ 0 };
            std::uint64_t traceBindHit{ 0 };
            std::uint64_t tickNoAudio{ 0 };
            std::uint64_t audioPresentNoMatch{ 0 };
            std::uint64_t traceBindMissUnbound{ 0 };
            std::uint64_t traceBindMissExpired{ 0 };
            std::uint64_t l1FormSemanticHit{ 0 };
            std::uint64_t l1FormSemanticMiss{ 0 };
        };
        Stats GetStats() const;
        void ResetStats();

    private:
        DecisionEngine() = default;

        std::atomic<bool> _initialized{ false };

        std::atomic<std::uint64_t> _l1Count{ 0 };
        std::atomic<std::uint64_t> _l2Count{ 0 };
        std::atomic<std::uint64_t> _l3Count{ 0 };
        std::atomic<std::uint64_t> _noCandidate{ 0 };
        std::atomic<std::uint64_t> _lowScoreFallback{ 0 };
        std::atomic<std::uint64_t> _traceBindHit{ 0 };
        std::atomic<std::uint64_t> _traceBindMiss{ 0 };
        std::atomic<std::uint64_t> _tickNoAudio{ 0 };
        std::atomic<std::uint64_t> _audioPresentNoMatch{ 0 };
        std::atomic<std::uint64_t> _traceBindMissUnbound{ 0 };
        std::atomic<std::uint64_t> _traceBindMissExpired{ 0 };
        std::atomic<std::uint64_t> _l1FormSemanticHit{ 0 };
        std::atomic<std::uint64_t> _l1FormSemanticMiss{ 0 };
    };
}
