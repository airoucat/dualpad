#pragma once

#include "haptics/HapticsTypes.h"
#include "haptics/FootstepTagNormalizer.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dualpad::haptics
{
    class FootstepTruthSessionShadow
    {
    public:
        struct Stats
        {
            std::uint64_t truthsObserved{ 0 };
            std::uint64_t instancesObserved{ 0 };
            std::uint64_t featuresObserved{ 0 };
            std::uint64_t sessionsPatched{ 0 };
            std::uint64_t sessionsExpired{ 0 };
            std::uint64_t sessionsExpiredNoCandidate{ 0 };
            std::uint64_t sessionsExpiredNoFeature{ 0 };
            std::uint64_t candidateAssignments{ 0 };
            std::uint64_t patchedExactInstance{ 0 };
            std::uint64_t patchedVoiceFallback{ 0 };
            std::uint32_t candidateP50{ 0 };
            std::uint32_t candidateP95{ 0 };
            std::uint32_t truthToPatchDeltaP50Us{ 0 };
            std::uint32_t truthToPatchDeltaP95Us{ 0 };
            std::uint32_t claimToPatchDeltaP50Us{ 0 };
            std::uint32_t claimToPatchDeltaP95Us{ 0 };
            std::uint64_t livePatchesQueued{ 0 };
            std::uint64_t livePatchesApplied{ 0 };
            std::uint64_t livePatchesExpired{ 0 };
            std::uint32_t samples{ 0 };
            std::uint32_t activeSessions{ 0 };
        };

        static FootstepTruthSessionShadow& GetSingleton();

        void Reset();
        void ObserveTruthToken(
            std::uint64_t truthUs,
            std::string_view canonicalTag,
            FootstepTruthGait gait,
            FootstepTruthSide side);
        void ObserveFootstepInstance(
            std::uint64_t instanceId,
            std::uintptr_t voicePtr,
            std::uint32_t generation,
            std::uint64_t observedUs,
            bool viaSubmit);
        void ObserveAudioFeature(const AudioFeatureMsg& msg);
        void Tick(std::uint64_t nowUs);
        struct LivePatch
        {
            std::uint64_t truthUs{ 0 };
            std::uint64_t expireUs{ 0 };
            std::uint64_t targetEndUs{ 0 };
            float ampScale{ 1.0f };
            float panSigned{ 0.0f };
            float attackScale{ 1.0f };
            float bodyScale{ 1.0f };
            float tailScale{ 1.0f };
            float resonance{ 0.0f };
            float textureBlend{ 0.0f };
            std::uint32_t patchLeaseUs{ 0 };
            std::uint16_t scorePermille{ 0 };
            FootstepTruthGait gait{ FootstepTruthGait::Unknown };
            std::uint64_t revision{ 0 };
            bool provisional{ false };
            bool applied{ false };
        };
        std::optional<LivePatch> TryGetLivePatchForTruth(
            std::uint64_t truthUs,
            std::uint64_t minRevision,
            std::uint64_t nowUs);
        bool HasLivePatchForTruth(
            std::uint64_t truthUs,
            std::uint64_t minRevision,
            std::uint64_t nowUs) const;
        void NoteLivePatchApplied(std::uint64_t truthUs, std::uint64_t revision);
        Stats GetStats() const;

    private:
        struct AudioSlot
        {
            std::atomic<std::uint64_t> version{ 0 };
            AudioFeatureMsg feature{};
        };

        struct Candidate
        {
            std::uint64_t instanceId{ 0 };
            std::uintptr_t voicePtr{ 0 };
            std::uint32_t generation{ 0 };
            std::uint64_t observedUs{ 0 };
            bool viaSubmit{ false };
        };

        struct Session
        {
            std::uint64_t truthUs{ 0 };
            std::string tag{};
            FootstepTruthGait gait{ FootstepTruthGait::Unknown };
            FootstepTruthSide side{ FootstepTruthSide::Unknown };
            std::uint64_t expireUs{ 0 };
            std::array<Candidate, 4> candidates{};
            std::uint8_t candidateCount{ 0 };
            bool everHadCandidate{ false };
            bool patched{ false };
        };

        FootstepTruthSessionShadow() = default;

        void ExpireLocked(std::uint64_t nowUs);
        void ProcessNewFeatures(std::uint64_t nowUs);
        void ExpireLivePatchesLocked(std::uint64_t nowUs);
        void UpsertProvisionalLivePatchLocked(
            std::uint64_t truthUs,
            FootstepTruthGait gait,
            std::uint64_t expireUs);
        bool InsertCandidateLocked(Session& session, const Candidate& candidate);
        static std::uint32_t PercentileOf(std::vector<std::uint32_t> values, float p);

        static constexpr std::size_t kAudioRingCapacity = 512;
        static constexpr std::size_t kMaxSessions = 64;
        static constexpr std::size_t kMaxSamples = 1024;

        std::array<AudioSlot, kAudioRingCapacity> _audioRing{};
        std::atomic<std::uint64_t> _audioWriteSeq{ 0 };
        std::uint64_t _audioReadSeq{ 0 };

        mutable std::mutex _mutex;
        std::vector<Session> _sessions{};
        std::vector<LivePatch> _readyLivePatches{};
        std::vector<std::uint32_t> _candidateSamples{};
        std::vector<std::uint32_t> _truthToPatchDeltaSamples{};
        std::vector<std::uint32_t> _claimToPatchDeltaSamples{};

        std::atomic<std::uint64_t> _truthsObserved{ 0 };
        std::atomic<std::uint64_t> _instancesObserved{ 0 };
        std::atomic<std::uint64_t> _featuresObserved{ 0 };
        std::atomic<std::uint64_t> _sessionsPatched{ 0 };
        std::atomic<std::uint64_t> _sessionsExpired{ 0 };
        std::atomic<std::uint64_t> _sessionsExpiredNoCandidate{ 0 };
        std::atomic<std::uint64_t> _sessionsExpiredNoFeature{ 0 };
        std::atomic<std::uint64_t> _candidateAssignments{ 0 };
        std::atomic<std::uint64_t> _patchedExactInstance{ 0 };
        std::atomic<std::uint64_t> _patchedVoiceFallback{ 0 };
        std::atomic<std::uint64_t> _livePatchesQueued{ 0 };
        std::atomic<std::uint64_t> _livePatchesApplied{ 0 };
        std::atomic<std::uint64_t> _livePatchesExpired{ 0 };
        std::atomic<std::uint64_t> _nextLivePatchRevision{ 1 };
    };
}
