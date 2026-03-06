#pragma once

#include "haptics/HapticsTypes.h"
#include "haptics/FootstepTruthBridge.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dualpad::haptics
{
    class FootstepAudioMatcher
    {
    public:
        enum class MemoryBucket : std::uint8_t
        {
            Unknown = 0,
            FootLeft,
            FootRight,
            JumpUp,
            JumpDown
        };

        struct Stats
        {
            std::uint64_t featuresObserved{ 0 };
            std::uint64_t truthsObserved{ 0 };
            std::uint64_t truthsMatched{ 0 };
            std::uint64_t livePatchesQueued{ 0 };
            std::uint64_t livePatchesApplied{ 0 };
            std::uint64_t livePatchesExpired{ 0 };
            std::uint64_t truthBridgeBound{ 0 };
            std::uint64_t truthBridgeMatched{ 0 };
            std::uint64_t truthBridgeNoFeature{ 0 };
            std::uint64_t recentMemoryHits{ 0 };
            std::uint64_t recentMemoryMisses{ 0 };
            std::uint64_t recentMemorySamples{ 0 };
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
        bool PrimeRecentMemoryPatchForTruth(std::uint64_t truthUs, std::uint64_t nowUs);
        bool PrimeRecentMemoryPatchForTruth(std::uint64_t truthUs, std::string_view tag, std::uint64_t nowUs);
        struct LivePatch
        {
            std::uint64_t truthUs{ 0 };
            std::uint64_t expireUs{ 0 };
            std::uint64_t targetEndUs{ 0 };
            float ampScale{ 1.0f };
            float panSigned{ 0.0f };
            std::uint32_t patchLeaseUs{ 0 };
            std::uint16_t scorePermille{ 0 };
            bool fromRecentMemory{ false };
            MemoryBucket bucket{ MemoryBucket::Unknown };
            std::uint32_t truthGapUs{ 0 };
        };
        std::optional<LivePatch> TryConsumeLivePatchForTruth(std::uint64_t truthUs, std::uint64_t nowUs);
        Stats GetStats();

    private:
        struct PendingTruth
        {
            std::uint64_t truthUs{ 0 };
            std::string tag{};
        };

        struct PendingPatch
        {
            std::uint64_t truthUs{ 0 };
            std::string tag{};
            FootstepTruthBridge::Binding binding{};
            std::uint64_t expireUs{ 0 };
        };

        struct TruthHint
        {
            std::uint64_t truthUs{ 0 };
            MemoryBucket bucket{ MemoryBucket::Unknown };
            std::uint32_t truthGapUs{ 0 };
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
            float matchPanSigned{ 0.0f };
            float matchEnergyScore{ 0.0f };
            std::uint32_t windowCandidates{ 0 };
            std::uint32_t semanticCandidates{ 0 };
            std::uint32_t bindingMissCandidates{ 0 };
            std::uint32_t traceMissCandidates{ 0 };
            float bestScore{ 0.0f };
            std::uint64_t bestDeltaUs{ 0 };
        };

        struct RecentModifierSample
        {
            std::uint64_t observedUs{ 0 };
            std::uint64_t truthUs{ 0 };
            float ampScale{ 1.0f };
            float panSigned{ 0.0f };
            std::uint32_t targetEndDeltaUs{ 0 };
            std::uint16_t scorePermille{ 0 };
            MemoryBucket bucket{ MemoryBucket::Unknown };
            std::uint32_t truthGapUs{ 0 };
        };

        FootstepAudioMatcher() = default;

        void ProcessMatureTruths(std::uint64_t nowUs);
        void PromoteTruthsToPatches(std::uint64_t nowUs);
        void ResolvePendingPatches(std::uint64_t nowUs);
        MatchResult TryResolvePatch(const PendingPatch& patch) const;
        MatchResult MatchFallbackTruth(std::uint64_t truthUs) const;
        void CommitMatchSamples(const MatchResult& match);
        void QueueLivePatch(const PendingPatch& patch, const MatchResult& match, std::uint64_t nowUs);
        std::optional<LivePatch> BuildRecentMemoryPatchLocked(
            std::uint64_t truthUs,
            MemoryBucket bucket,
            std::uint32_t truthGapUs,
            std::uint64_t nowUs);
        void RememberRecentModifierLocked(const LivePatch& patch, std::uint64_t nowUs);
        void ExpireRecentModifierLocked(std::uint64_t nowUs);
        static MemoryBucket ClassifyMemoryBucket(std::string_view tag);
        static bool IsWalkBucket(MemoryBucket bucket);
        static MemoryBucket OppositeWalkBucket(MemoryBucket bucket);
        static std::uint32_t PercentileOf(std::vector<std::uint32_t> values, float p);

        static constexpr std::size_t kAudioRingCapacity = 512;
        static constexpr std::size_t kSampleCap = 1024;

        std::array<AudioSlot, kAudioRingCapacity> _audioRing{};
        std::atomic<std::uint64_t> _audioWriteSeq{ 0 };

        std::atomic<std::uint64_t> _featuresObserved{ 0 };
        std::atomic<std::uint64_t> _truthsObserved{ 0 };
        std::atomic<std::uint64_t> _truthsMatched{ 0 };
        std::atomic<std::uint64_t> _livePatchesQueued{ 0 };
        std::atomic<std::uint64_t> _livePatchesApplied{ 0 };
        std::atomic<std::uint64_t> _livePatchesExpired{ 0 };
        std::atomic<std::uint64_t> _truthBridgeBound{ 0 };
        std::atomic<std::uint64_t> _truthBridgeMatched{ 0 };
        std::atomic<std::uint64_t> _truthBridgeNoFeature{ 0 };
        std::atomic<std::uint64_t> _recentMemoryHits{ 0 };
        std::atomic<std::uint64_t> _recentMemoryMisses{ 0 };
        std::atomic<std::uint64_t> _recentMemorySamples{ 0 };
        std::atomic<std::uint64_t> _truthNoWindow{ 0 };
        std::atomic<std::uint64_t> _truthNoSemantic{ 0 };
        std::atomic<std::uint64_t> _truthLowScore{ 0 };
        std::atomic<std::uint64_t> _windowCandidates{ 0 };
        std::atomic<std::uint64_t> _semanticCandidates{ 0 };
        std::atomic<std::uint64_t> _bindingMissCandidates{ 0 };
        std::atomic<std::uint64_t> _traceMissCandidates{ 0 };

        mutable std::mutex _pendingMutex;
        std::deque<PendingTruth> _pendingTruths{};
        std::deque<PendingPatch> _pendingPatches{};
        std::deque<LivePatch> _readyLivePatches{};
        std::deque<RecentModifierSample> _recentModifierSamples{};
        std::deque<TruthHint> _truthHints{};
        std::array<std::uint64_t, 5> _lastTruthUsByBucket{};

        mutable std::mutex _sampleMutex;
        std::vector<std::uint32_t> _matchDeltasUs{};
        std::vector<std::uint32_t> _matchDurationsUs{};
        std::vector<std::uint32_t> _matchScoresPermille{};
        std::vector<std::uint32_t> _matchPanAbsPermille{};
    };
}
