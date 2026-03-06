#pragma once

#include "haptics/HapticsTypes.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace dualpad::haptics
{
    class FootstepAudioMatcher
    {
    public:
        struct Stats
        {
            std::uint64_t featuresObserved{ 0 };
            std::uint64_t truthsObserved{ 0 };
            std::uint64_t truthsMatched{ 0 };
            std::uint64_t truthBridgeBound{ 0 };
            std::uint64_t truthBridgeMatched{ 0 };
            std::uint64_t truthBridgeNoFeature{ 0 };
            std::uint64_t truthNoWindow{ 0 };
            std::uint64_t truthNoSemantic{ 0 };
            std::uint64_t truthLowScore{ 0 };
            std::uint64_t windowCandidates{ 0 };
            std::uint64_t semanticCandidates{ 0 };
            std::uint64_t bindingMissCandidates{ 0 };
            std::uint64_t traceMissCandidates{ 0 };
            std::uint32_t matchDeltaP50Us{ 0 };
            std::uint32_t matchDeltaP95Us{ 0 };
            std::uint32_t matchDurationP50Us{ 0 };
            std::uint32_t matchDurationP95Us{ 0 };
            std::uint32_t matchScoreP50Permille{ 0 };
            std::uint32_t matchScoreP95Permille{ 0 };
            std::uint32_t matchPanAbsP50Permille{ 0 };
            std::uint32_t matchPanAbsP95Permille{ 0 };
            std::uint32_t samples{ 0 };
            std::uint32_t pendingTruths{ 0 };
        };

        static FootstepAudioMatcher& GetSingleton();

        void Reset();
        void Tick(std::uint64_t nowUs);
        void ObserveAudioFeature(const AudioFeatureMsg& msg);
        void ObserveTruthEvent(std::uint64_t truthUs, std::string_view tag);
        Stats GetStats();

    private:
        struct PendingTruth
        {
            std::uint64_t truthUs{ 0 };
            std::string tag{};
        };

        struct AudioSlot
        {
            std::atomic<std::uint64_t> version{ 0 };
            AudioFeatureMsg feature{};
        };

        struct MatchResult
        {
            bool matched{ false };
            bool bridgeBound{ false };
            bool bridgeMatched{ false };
            bool bridgeNoFeature{ false };
            std::uint32_t matchDeltaUs{ 0 };
            std::uint32_t matchDurationUs{ 0 };
            std::uint16_t matchScorePermille{ 0 };
            std::uint16_t matchPanAbsPermille{ 0 };
            std::uint32_t windowCandidates{ 0 };
            std::uint32_t semanticCandidates{ 0 };
            std::uint32_t bindingMissCandidates{ 0 };
            std::uint32_t traceMissCandidates{ 0 };
            float bestScore{ 0.0f };
            std::uint64_t bestDeltaUs{ 0 };
        };

        FootstepAudioMatcher() = default;

        void ProcessMatureTruths(std::uint64_t nowUs);
        MatchResult MatchTruth(std::uint64_t truthUs, std::string_view tag) const;
        static std::uint32_t PercentileOf(std::vector<std::uint32_t> values, float p);

        static constexpr std::size_t kAudioRingCapacity = 512;
        static constexpr std::size_t kSampleCap = 1024;

        std::array<AudioSlot, kAudioRingCapacity> _audioRing{};
        std::atomic<std::uint64_t> _audioWriteSeq{ 0 };

        std::atomic<std::uint64_t> _featuresObserved{ 0 };
        std::atomic<std::uint64_t> _truthsObserved{ 0 };
        std::atomic<std::uint64_t> _truthsMatched{ 0 };
        std::atomic<std::uint64_t> _truthBridgeBound{ 0 };
        std::atomic<std::uint64_t> _truthBridgeMatched{ 0 };
        std::atomic<std::uint64_t> _truthBridgeNoFeature{ 0 };
        std::atomic<std::uint64_t> _truthNoWindow{ 0 };
        std::atomic<std::uint64_t> _truthNoSemantic{ 0 };
        std::atomic<std::uint64_t> _truthLowScore{ 0 };
        std::atomic<std::uint64_t> _windowCandidates{ 0 };
        std::atomic<std::uint64_t> _semanticCandidates{ 0 };
        std::atomic<std::uint64_t> _bindingMissCandidates{ 0 };
        std::atomic<std::uint64_t> _traceMissCandidates{ 0 };

        mutable std::mutex _pendingMutex;
        std::deque<PendingTruth> _pendingTruths{};

        mutable std::mutex _sampleMutex;
        std::vector<std::uint32_t> _matchDeltasUs{};
        std::vector<std::uint32_t> _matchDurationsUs{};
        std::vector<std::uint32_t> _matchScoresPermille{};
        std::vector<std::uint32_t> _matchPanAbsPermille{};
    };
}
