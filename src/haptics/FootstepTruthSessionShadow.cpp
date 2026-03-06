#include "pch.h"
#include "haptics/FootstepTruthSessionShadow.h"

#include "haptics/FootstepCandidateReservoir.h"
#include "haptics/HapticsConfig.h"

#include <SKSE/SKSE.h>
#include <algorithm>
#include <cmath>

namespace logger = SKSE::log;

namespace dualpad::haptics
{
    namespace
    {
        constexpr std::uint32_t kMaxProbeLinesPerSecond = 6;
        constexpr std::uint64_t kStrideCoalesceUs = 6000ull;
        constexpr std::uint64_t kSprintExtraLookbackUs = 15000ull;
        constexpr std::uint64_t kSprintExtraLookaheadUs = 120000ull;
        constexpr std::uint64_t kSprintExtraExpireUs = 140000ull;
        constexpr std::uint64_t kLivePatchTtlUs = 180000ull;

        bool ShouldEmitWindowedProbe(
            std::atomic<std::uint64_t>& windowUs,
            std::atomic<std::uint32_t>& windowLines,
            std::uint64_t tsUs,
            std::uint32_t maxLinesPerSec)
        {
            auto win = windowUs.load(std::memory_order_relaxed);
            if (win == 0 || tsUs < win || (tsUs - win) >= 1000000ull) {
                windowUs.store(tsUs, std::memory_order_relaxed);
                windowLines.store(0, std::memory_order_relaxed);
            }
            return windowLines.fetch_add(1, std::memory_order_relaxed) < maxLinesPerSec;
        }

        std::uint64_t AbsDiff(std::uint64_t a, std::uint64_t b)
        {
            return (a > b) ? (a - b) : (b - a);
        }

        std::uint64_t SessionExpireUs(std::uint64_t truthUs, FootstepTruthGait gait)
        {
            const auto& cfg = HapticsConfig::GetSingleton();
            auto expireUs = truthUs + static_cast<std::uint64_t>(cfg.footstepTruthBridgeLookaheadUs) + 180000ull;
            if (gait == FootstepTruthGait::Sprint) {
                expireUs += kSprintExtraExpireUs;
            }
            return expireUs;
        }

        std::uint64_t SessionLookbackUs(FootstepTruthGait gait)
        {
            const auto& cfg = HapticsConfig::GetSingleton();
            auto lookbackUs = static_cast<std::uint64_t>(cfg.footstepTruthBridgeLookbackUs);
            if (gait == FootstepTruthGait::Sprint) {
                lookbackUs += kSprintExtraLookbackUs;
            }
            return lookbackUs;
        }

        std::uint64_t SessionLookaheadUs(FootstepTruthGait gait)
        {
            const auto& cfg = HapticsConfig::GetSingleton();
            auto lookaheadUs = static_cast<std::uint64_t>(cfg.footstepTruthBridgeLookaheadUs);
            if (gait == FootstepTruthGait::Sprint) {
                lookaheadUs += kSprintExtraLookaheadUs;
            }
            return lookaheadUs;
        }
    }

    FootstepTruthSessionShadow& FootstepTruthSessionShadow::GetSingleton()
    {
        static FootstepTruthSessionShadow instance;
        return instance;
    }

    void FootstepTruthSessionShadow::Reset()
    {
        _audioWriteSeq.store(0, std::memory_order_relaxed);
        _audioReadSeq = 0;
        _truthsObserved.store(0, std::memory_order_relaxed);
        _instancesObserved.store(0, std::memory_order_relaxed);
        _featuresObserved.store(0, std::memory_order_relaxed);
        _sessionsPatched.store(0, std::memory_order_relaxed);
        _sessionsExpired.store(0, std::memory_order_relaxed);
        _sessionsExpiredNoCandidate.store(0, std::memory_order_relaxed);
        _sessionsExpiredNoFeature.store(0, std::memory_order_relaxed);
        _candidateAssignments.store(0, std::memory_order_relaxed);
        _patchedExactInstance.store(0, std::memory_order_relaxed);
        _patchedVoiceFallback.store(0, std::memory_order_relaxed);
        _livePatchesQueued.store(0, std::memory_order_relaxed);
        _livePatchesApplied.store(0, std::memory_order_relaxed);
        _livePatchesExpired.store(0, std::memory_order_relaxed);
        _nextLivePatchRevision.store(1, std::memory_order_relaxed);

        std::scoped_lock lock(_mutex);
        _sessions.clear();
        _readyLivePatches.clear();
        _candidateSamples.clear();
        _truthToPatchDeltaSamples.clear();
        _claimToPatchDeltaSamples.clear();
    }

    void FootstepTruthSessionShadow::ObserveTruthToken(
        std::uint64_t truthUs,
        std::string_view canonicalTag,
        FootstepTruthGait gait,
        FootstepTruthSide side)
    {
        const auto& cfg = HapticsConfig::GetSingleton();
        if (!cfg.enableFootstepTruthBridgeShadow || truthUs == 0 || canonicalTag.empty()) {
            return;
        }

        _truthsObserved.fetch_add(1, std::memory_order_relaxed);

        auto seededCandidates = FootstepCandidateReservoir::GetSingleton().SnapshotForTruth(truthUs, 4);
        const auto sessionExpireUs = SessionExpireUs(truthUs, gait);

        std::scoped_lock lock(_mutex);
        ExpireLocked(truthUs);

        if (gait != FootstepTruthGait::Unknown && side != FootstepTruthSide::Unknown) {
            for (auto it = _sessions.rbegin(); it != _sessions.rend(); ++it) {
                if (it->patched || it->side != side || !SameStrideFamily(it->gait, gait)) {
                    continue;
                }

                const auto deltaUs = AbsDiff(it->truthUs, truthUs);
                if (deltaUs > kStrideCoalesceUs) {
                    break;
                }

                if (it->gait == FootstepTruthGait::Walk && gait == FootstepTruthGait::Sprint) {
                    it->truthUs = truthUs;
                    it->tag.assign(canonicalTag);
                    it->gait = gait;
                    it->expireUs = sessionExpireUs;
                    if (!cfg.enableStateTrackFootstepTruthAttack) {
                        UpsertProvisionalLivePatchLocked(it->truthUs, it->gait, it->expireUs);
                    }
                }
                return;
            }
        }

        if (_sessions.size() >= kMaxSessions) {
            _sessions.erase(_sessions.begin());
        }

        _sessions.push_back(Session{
            .truthUs = truthUs,
            .tag = std::string(canonicalTag),
            .gait = gait,
            .side = side,
            .expireUs = sessionExpireUs
        });
        auto& session = _sessions.back();
        for (const auto& seeded : seededCandidates) {
            Candidate candidate{};
            candidate.instanceId = seeded.instanceId;
            candidate.voicePtr = seeded.voicePtr;
            candidate.generation = seeded.generation;
            candidate.observedUs = seeded.observedUs;
            candidate.viaSubmit = seeded.source == FootstepCandidateReservoir::Source::Submit;
            if (InsertCandidateLocked(session, candidate)) {
                _candidateAssignments.fetch_add(1, std::memory_order_relaxed);
            }
        }

        if (!cfg.enableStateTrackFootstepTruthAttack) {
            UpsertProvisionalLivePatchLocked(session.truthUs, session.gait, session.expireUs);
        }
    }

    void FootstepTruthSessionShadow::ObserveFootstepInstance(
        std::uint64_t instanceId,
        std::uintptr_t voicePtr,
        std::uint32_t generation,
        std::uint64_t observedUs,
        bool viaSubmit)
    {
        const auto& cfg = HapticsConfig::GetSingleton();
        if (!cfg.enableFootstepTruthBridgeShadow ||
            instanceId == 0 ||
            voicePtr == 0 ||
            observedUs == 0) {
            return;
        }

        _instancesObserved.fetch_add(1, std::memory_order_relaxed);

        std::scoped_lock lock(_mutex);
        ExpireLocked(observedUs);

        for (auto& session : _sessions) {
            if (session.patched) {
                continue;
            }

            const auto lookbackUs = SessionLookbackUs(session.gait);
            const auto lookaheadUs = SessionLookaheadUs(session.gait);
            const bool inWindow =
                (observedUs + lookbackUs) >= session.truthUs &&
                observedUs <= (session.truthUs + lookaheadUs);
            if (!inWindow) {
                continue;
            }

            bool duplicate = false;
            for (std::uint8_t i = 0; i < session.candidateCount; ++i) {
                if (session.candidates[i].instanceId == instanceId) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) {
                continue;
            }

            Candidate candidate{
                .instanceId = instanceId,
                .voicePtr = voicePtr,
                .generation = generation,
                .observedUs = observedUs,
                .viaSubmit = viaSubmit
            };

            if (InsertCandidateLocked(session, candidate)) {
                _candidateAssignments.fetch_add(1, std::memory_order_relaxed);
                UpsertProvisionalLivePatchLocked(session.truthUs, session.gait, session.expireUs);
            }
        }
    }

    void FootstepTruthSessionShadow::ObserveAudioFeature(const AudioFeatureMsg& msg)
    {
        const auto& cfg = HapticsConfig::GetSingleton();
        if (!cfg.enableFootstepTruthBridgeShadow) {
            return;
        }
        if (msg.qpcStart == 0 || msg.qpcEnd == 0 || msg.voiceId == 0) {
            return;
        }

        const auto writeSeq = _audioWriteSeq.fetch_add(1, std::memory_order_acq_rel) + 1ull;
        auto& slot = _audioRing[writeSeq % kAudioRingCapacity];
        const auto writingVersion = writeSeq * 2ull - 1ull;
        const auto stableVersion = writeSeq * 2ull;
        slot.version.store(writingVersion, std::memory_order_release);
        slot.feature = msg;
        slot.version.store(stableVersion, std::memory_order_release);
        _featuresObserved.fetch_add(1, std::memory_order_relaxed);
    }

    void FootstepTruthSessionShadow::Tick(std::uint64_t nowUs)
    {
        const auto& cfg = HapticsConfig::GetSingleton();
        if (!cfg.enableFootstepTruthBridgeShadow) {
            return;
        }
        if (nowUs == 0) {
            nowUs = ToQPC(Now());
        }

        ProcessNewFeatures(nowUs);
        std::scoped_lock lock(_mutex);
        ExpireLocked(nowUs);
    }

    std::optional<FootstepTruthSessionShadow::LivePatch> FootstepTruthSessionShadow::TryGetLivePatchForTruth(
        std::uint64_t truthUs,
        std::uint64_t minRevision,
        std::uint64_t nowUs)
    {
        if (truthUs == 0) {
            return std::nullopt;
        }

        std::scoped_lock lock(_mutex);
        ExpireLivePatchesLocked(nowUs);

        for (const auto& patch : _readyLivePatches) {
            if (patch.truthUs != truthUs) {
                continue;
            }
            if (patch.expireUs <= nowUs) {
                continue;
            }
            if (patch.revision <= minRevision) {
                return std::nullopt;
            }
            return patch;
        }

        return std::nullopt;
    }

    bool FootstepTruthSessionShadow::HasLivePatchForTruth(
        std::uint64_t truthUs,
        std::uint64_t minRevision,
        std::uint64_t nowUs) const
    {
        if (truthUs == 0) {
            return false;
        }

        std::scoped_lock lock(_mutex);
        const_cast<FootstepTruthSessionShadow*>(this)->ExpireLivePatchesLocked(nowUs);
        for (const auto& patch : _readyLivePatches) {
            if (patch.truthUs != truthUs) {
                continue;
            }
            if (patch.expireUs <= nowUs) {
                continue;
            }
            if (patch.revision <= minRevision) {
                return false;
            }
            return true;
        }
        return false;
    }

    void FootstepTruthSessionShadow::NoteLivePatchApplied(std::uint64_t truthUs, std::uint64_t revision)
    {
        if (truthUs == 0 || revision == 0) {
            return;
        }

        std::scoped_lock lock(_mutex);
        for (auto& patch : _readyLivePatches) {
            if (patch.truthUs != truthUs || patch.revision != revision || patch.applied) {
                continue;
            }
            patch.applied = true;
            _livePatchesApplied.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }

    FootstepTruthSessionShadow::Stats FootstepTruthSessionShadow::GetStats() const
    {
        const auto nowUs = ToQPC(Now());
        {
            std::scoped_lock lock(_mutex);
            const_cast<FootstepTruthSessionShadow*>(this)->ExpireLocked(nowUs);
        }

        Stats stats{};
        stats.truthsObserved = _truthsObserved.load(std::memory_order_relaxed);
        stats.instancesObserved = _instancesObserved.load(std::memory_order_relaxed);
        stats.featuresObserved = _featuresObserved.load(std::memory_order_relaxed);
        stats.sessionsPatched = _sessionsPatched.load(std::memory_order_relaxed);
        stats.sessionsExpired = _sessionsExpired.load(std::memory_order_relaxed);
        stats.sessionsExpiredNoCandidate = _sessionsExpiredNoCandidate.load(std::memory_order_relaxed);
        stats.sessionsExpiredNoFeature = _sessionsExpiredNoFeature.load(std::memory_order_relaxed);
        stats.candidateAssignments = _candidateAssignments.load(std::memory_order_relaxed);
        stats.patchedExactInstance = _patchedExactInstance.load(std::memory_order_relaxed);
        stats.patchedVoiceFallback = _patchedVoiceFallback.load(std::memory_order_relaxed);
        stats.livePatchesQueued = _livePatchesQueued.load(std::memory_order_relaxed);
        stats.livePatchesApplied = _livePatchesApplied.load(std::memory_order_relaxed);
        stats.livePatchesExpired = _livePatchesExpired.load(std::memory_order_relaxed);

        std::scoped_lock lock(_mutex);
        const_cast<FootstepTruthSessionShadow*>(this)->ExpireLivePatchesLocked(nowUs);
        stats.candidateP50 = PercentileOf(_candidateSamples, 0.50f);
        stats.candidateP95 = PercentileOf(_candidateSamples, 0.95f);
        stats.truthToPatchDeltaP50Us = PercentileOf(_truthToPatchDeltaSamples, 0.50f);
        stats.truthToPatchDeltaP95Us = PercentileOf(_truthToPatchDeltaSamples, 0.95f);
        stats.claimToPatchDeltaP50Us = PercentileOf(_claimToPatchDeltaSamples, 0.50f);
        stats.claimToPatchDeltaP95Us = PercentileOf(_claimToPatchDeltaSamples, 0.95f);
        stats.samples = static_cast<std::uint32_t>(_truthToPatchDeltaSamples.size());
        stats.activeSessions = static_cast<std::uint32_t>(_sessions.size());
        return stats;
    }

    void FootstepTruthSessionShadow::ExpireLocked(std::uint64_t nowUs)
    {
        static std::atomic<std::uint64_t> s_probeWindowUs{ 0 };
        static std::atomic<std::uint32_t> s_probeLines{ 0 };

        auto it = _sessions.begin();
        while (it != _sessions.end()) {
            if (it->patched) {
                it = _sessions.erase(it);
                continue;
            }
            if (nowUs < it->expireUs) {
                ++it;
                continue;
            }

            _sessionsExpired.fetch_add(1, std::memory_order_relaxed);
            if (!it->everHadCandidate) {
                _sessionsExpiredNoCandidate.fetch_add(1, std::memory_order_relaxed);
            } else {
                _sessionsExpiredNoFeature.fetch_add(1, std::memory_order_relaxed);
            }
            _candidateSamples.push_back(it->candidateCount);
            if (_candidateSamples.size() > kMaxSamples) {
                _candidateSamples.erase(_candidateSamples.begin());
            }

            if (ShouldEmitWindowedProbe(
                    s_probeWindowUs,
                    s_probeLines,
                    nowUs,
                    kMaxProbeLinesPerSecond)) {
                logger::info(
                    "[Haptics][FootSession] session_expired tag={} gait={} cand={} hadCand={} age={}us",
                    it->tag,
                    ToString(it->gait),
                    static_cast<unsigned>(it->candidateCount),
                    it->everHadCandidate ? 1 : 0,
                    nowUs - it->truthUs);
            }

            it = _sessions.erase(it);
        }
    }

    void FootstepTruthSessionShadow::ExpireLivePatchesLocked(std::uint64_t nowUs)
    {
        auto it = _readyLivePatches.begin();
        while (it != _readyLivePatches.end()) {
            if (it->expireUs > nowUs) {
                ++it;
                continue;
            }
            if (!it->applied) {
                _livePatchesExpired.fetch_add(1, std::memory_order_relaxed);
            }
            it = _readyLivePatches.erase(it);
        }
    }

    void FootstepTruthSessionShadow::UpsertProvisionalLivePatchLocked(
        std::uint64_t truthUs,
        FootstepTruthGait gait,
        std::uint64_t expireUs)
    {
        const auto& cfg = HapticsConfig::GetSingleton();
        if (!cfg.enableFootstepTruthSessionLivePatch || truthUs == 0) {
            return;
        }

        const auto sprintLeaseBoostUs =
            (gait == FootstepTruthGait::Sprint) ? 40000ull : 0ull;
        const auto patchLeaseUs = static_cast<std::uint32_t>(std::min<std::uint64_t>(
            static_cast<std::uint64_t>(cfg.stateTrackFootstepPatchLeaseUs) + sprintLeaseBoostUs,
            220000ull));
        const auto targetEndDeltaUs = (gait == FootstepTruthGait::Sprint) ?
            std::min<std::uint64_t>(160000ull, patchLeaseUs) :
            std::min<std::uint64_t>(110000ull, patchLeaseUs);
        const auto patchExpireUs = std::max<std::uint64_t>(
            expireUs,
            truthUs + std::min<std::uint64_t>(static_cast<std::uint64_t>(patchLeaseUs) + 40000ull, 240000ull));

        for (auto& existing : _readyLivePatches) {
            if (existing.truthUs != truthUs) {
                continue;
            }

            const auto newAmpScale = (gait == FootstepTruthGait::Sprint) ? 1.00f : 0.96f;
            const auto newScorePermille = static_cast<std::uint16_t>(
                (gait == FootstepTruthGait::Sprint) ? 560 : 520);
            const auto changed =
                existing.provisional != true ||
                existing.gait != gait ||
                existing.targetEndUs != (truthUs + targetEndDeltaUs) ||
                std::fabs(existing.ampScale - newAmpScale) > 0.0001f ||
                std::fabs(existing.panSigned) > 0.0001f ||
                existing.patchLeaseUs != patchLeaseUs ||
                existing.scorePermille != newScorePermille;

            existing.expireUs = std::max(existing.expireUs, patchExpireUs);
            existing.targetEndUs = truthUs + targetEndDeltaUs;
            existing.ampScale = newAmpScale;
            existing.panSigned = 0.0f;
            existing.patchLeaseUs = patchLeaseUs;
            existing.scorePermille = newScorePermille;
            existing.gait = gait;
            existing.provisional = true;
            if (changed || existing.applied) {
                existing.revision = _nextLivePatchRevision.fetch_add(1, std::memory_order_relaxed);
                existing.applied = false;
            }
            return;
        }

        LivePatch provisional{};
        provisional.truthUs = truthUs;
        provisional.expireUs = patchExpireUs;
        provisional.targetEndUs = truthUs + targetEndDeltaUs;
        provisional.ampScale = (gait == FootstepTruthGait::Sprint) ? 1.00f : 0.96f;
        provisional.panSigned = 0.0f;
        provisional.patchLeaseUs = patchLeaseUs;
        provisional.scorePermille = (gait == FootstepTruthGait::Sprint) ? 560 : 520;
        provisional.gait = gait;
        provisional.revision = _nextLivePatchRevision.fetch_add(1, std::memory_order_relaxed);
        provisional.provisional = true;
        provisional.applied = false;
        _readyLivePatches.push_back(provisional);
        _livePatchesQueued.fetch_add(1, std::memory_order_relaxed);
    }

    void FootstepTruthSessionShadow::ProcessNewFeatures(std::uint64_t nowUs)
    {
        static std::atomic<std::uint64_t> s_probeWindowUs{ 0 };
        static std::atomic<std::uint32_t> s_probeLines{ 0 };

        const auto latestSeq = _audioWriteSeq.load(std::memory_order_acquire);
        if (_audioReadSeq >= latestSeq) {
            return;
        }

        for (auto seq = _audioReadSeq + 1ull; seq <= latestSeq; ++seq) {
            const auto expectedVersion = seq * 2ull;
            const auto& slot = _audioRing[seq % kAudioRingCapacity];
            const auto v1 = slot.version.load(std::memory_order_acquire);
            if (v1 != expectedVersion) {
                continue;
            }
            const auto feature = slot.feature;
            const auto v2 = slot.version.load(std::memory_order_acquire);
            if (v2 != expectedVersion) {
                continue;
            }

            std::scoped_lock lock(_mutex);
            ExpireLocked(nowUs);

            Session* bestSession = nullptr;
            Candidate* bestCandidate = nullptr;
            bool exactInstance = false;
            std::uint64_t bestRank = std::numeric_limits<std::uint64_t>::max();

            for (auto& session : _sessions) {
                if (session.patched) {
                    continue;
                }
            if (feature.qpcStart > session.expireUs) {
                continue;
            }

                for (std::uint8_t i = 0; i < session.candidateCount; ++i) {
                    auto& candidate = session.candidates[i];
                    bool currentExact = false;
                    bool identityMatch = false;
                    if (candidate.instanceId != 0 && feature.instanceId == candidate.instanceId) {
                        identityMatch = true;
                        currentExact = true;
                    } else if (candidate.voicePtr != 0 &&
                               candidate.generation != 0 &&
                               feature.voiceId == static_cast<std::uint64_t>(candidate.voicePtr) &&
                               feature.voiceGeneration == candidate.generation) {
                        identityMatch = true;
                    }
                    if (!identityMatch) {
                        continue;
                    }

                    const auto rank = AbsDiff(feature.qpcStart, session.truthUs);
                    const bool better =
                        !bestSession ||
                        (currentExact && !exactInstance) ||
                        (currentExact == exactInstance && rank < bestRank);
                    if (!better) {
                        continue;
                    }

                    bestSession = &session;
                    bestCandidate = &candidate;
                    exactInstance = currentExact;
                    bestRank = rank;
                }
            }

            if (!bestSession || !bestCandidate) {
                continue;
            }

            const auto& cfg = HapticsConfig::GetSingleton();
            bestSession->patched = true;
            _sessionsPatched.fetch_add(1, std::memory_order_relaxed);
            if (exactInstance) {
                _patchedExactInstance.fetch_add(1, std::memory_order_relaxed);
            } else {
                _patchedVoiceFallback.fetch_add(1, std::memory_order_relaxed);
            }

            if (cfg.enableFootstepTruthSessionLivePatch) {
                const auto durationUs = (feature.qpcEnd > feature.qpcStart) ?
                    (feature.qpcEnd - feature.qpcStart) :
                    0ull;
                const auto matchDeltaUs = AbsDiff(feature.qpcStart, bestSession->truthUs);
                const auto energyTotal = std::max(0.0001f, feature.energyL + feature.energyR);
                const auto panSigned = std::clamp(
                    (feature.energyR - feature.energyL) / energyTotal,
                    -0.70f,
                    0.70f);
                const auto energyScore = std::clamp(
                    0.65f * feature.peak + 0.35f * feature.rms,
                    0.0f,
                    1.0f);
                const auto sprintLeaseBoostUs =
                    (bestSession->gait == FootstepTruthGait::Sprint) ? 40000ull : 0ull;
                const auto patchLeaseUs = static_cast<std::uint32_t>(std::min<std::uint64_t>(
                    static_cast<std::uint64_t>(cfg.stateTrackFootstepPatchLeaseUs) + sprintLeaseBoostUs,
                    220000ull));
                const auto targetEndDeltaUs = std::clamp<std::uint64_t>(
                    matchDeltaUs + durationUs,
                    32000ull,
                    static_cast<std::uint64_t>(patchLeaseUs));

                LivePatch live{};
                live.truthUs = bestSession->truthUs;
                live.expireUs = feature.qpcStart + kLivePatchTtlUs;
                live.targetEndUs = bestSession->truthUs + targetEndDeltaUs;
                live.ampScale = std::clamp(
                    0.92f +
                        0.26f * energyScore +
                        ((bestSession->gait == FootstepTruthGait::Sprint) ? 0.04f : 0.0f),
                    0.92f,
                    1.22f);
                live.panSigned = std::clamp(panSigned, -0.55f, 0.55f);
                live.patchLeaseUs = patchLeaseUs;
                live.scorePermille = static_cast<std::uint16_t>(std::clamp(
                    static_cast<int>(std::lround(
                        (0.72f + 0.20f * energyScore + (exactInstance ? 0.08f : 0.0f)) * 1000.0f)),
                    0,
                    1000));
                live.gait = bestSession->gait;
                live.revision = _nextLivePatchRevision.fetch_add(1, std::memory_order_relaxed);
                live.provisional = false;
                live.applied = false;

                ExpireLivePatchesLocked(feature.qpcStart);

                bool replaced = false;
                for (auto& existing : _readyLivePatches) {
                    if (existing.truthUs != live.truthUs) {
                        continue;
                    }
                    const bool changed =
                        existing.provisional != live.provisional ||
                        existing.gait != live.gait ||
                        existing.targetEndUs != live.targetEndUs ||
                        std::fabs(existing.ampScale - live.ampScale) > 0.0001f ||
                        std::fabs(existing.panSigned - live.panSigned) > 0.0001f ||
                        existing.patchLeaseUs != live.patchLeaseUs ||
                        existing.scorePermille != live.scorePermille ||
                        existing.expireUs != live.expireUs;
                    existing.expireUs = std::max(existing.expireUs, live.expireUs);
                    existing.targetEndUs = live.targetEndUs;
                    existing.ampScale = live.ampScale;
                    existing.panSigned = live.panSigned;
                    existing.patchLeaseUs = live.patchLeaseUs;
                    existing.scorePermille = live.scorePermille;
                    existing.gait = live.gait;
                    existing.provisional = false;
                    if (changed || existing.applied) {
                        existing.revision = _nextLivePatchRevision.fetch_add(1, std::memory_order_relaxed);
                        existing.applied = false;
                    }
                    replaced = true;
                    break;
                }
                if (!replaced) {
                    _readyLivePatches.push_back(live);
                    _livePatchesQueued.fetch_add(1, std::memory_order_relaxed);
                }
            }

            _candidateSamples.push_back(bestSession->candidateCount);
            _truthToPatchDeltaSamples.push_back(static_cast<std::uint32_t>(std::min<std::uint64_t>(
                AbsDiff(feature.qpcStart, bestSession->truthUs),
                std::numeric_limits<std::uint32_t>::max())));
            _claimToPatchDeltaSamples.push_back(static_cast<std::uint32_t>(std::min<std::uint64_t>(
                AbsDiff(feature.qpcStart, bestCandidate->observedUs),
                std::numeric_limits<std::uint32_t>::max())));
            if (_candidateSamples.size() > kMaxSamples) {
                _candidateSamples.erase(_candidateSamples.begin());
            }
            if (_truthToPatchDeltaSamples.size() > kMaxSamples) {
                _truthToPatchDeltaSamples.erase(_truthToPatchDeltaSamples.begin());
            }
            if (_claimToPatchDeltaSamples.size() > kMaxSamples) {
                _claimToPatchDeltaSamples.erase(_claimToPatchDeltaSamples.begin());
            }

            if (ShouldEmitWindowedProbe(
                    s_probeWindowUs,
                    s_probeLines,
                    feature.qpcStart,
                    kMaxProbeLinesPerSecond) &&
                bestRank >= 20000ull) {
                logger::info(
                    "[Haptics][FootSession] session_patch tag={} gait={} cand={} exact={} deltaTruth={}us deltaClaim={}us",
                    bestSession->tag,
                    ToString(bestSession->gait),
                    static_cast<unsigned>(bestSession->candidateCount),
                    exactInstance ? 1 : 0,
                    AbsDiff(feature.qpcStart, bestSession->truthUs),
                    AbsDiff(feature.qpcStart, bestCandidate->observedUs));
            }
        }

        _audioReadSeq = latestSeq;
    }

    bool FootstepTruthSessionShadow::InsertCandidateLocked(Session& session, const Candidate& candidate)
    {
        for (std::uint8_t i = 0; i < session.candidateCount; ++i) {
            if (session.candidates[i].instanceId != candidate.instanceId) {
                continue;
            }
            session.candidates[i].voicePtr = candidate.voicePtr;
            session.candidates[i].generation = candidate.generation;
            session.candidates[i].observedUs = candidate.observedUs;
            session.candidates[i].viaSubmit = session.candidates[i].viaSubmit || candidate.viaSubmit;
            session.everHadCandidate = true;
            return false;
        }

        if (session.candidateCount < session.candidates.size()) {
            session.candidates[session.candidateCount++] = candidate;
            session.everHadCandidate = true;
            return true;
        }

        std::size_t replaceIndex = 0;
        auto worstRank = 0ull;
        for (std::size_t i = 0; i < session.candidates.size(); ++i) {
            const auto rank = AbsDiff(session.candidates[i].observedUs, session.truthUs);
            if (rank >= worstRank) {
                worstRank = rank;
                replaceIndex = i;
            }
        }

        const auto incomingRank = AbsDiff(candidate.observedUs, session.truthUs);
        if (incomingRank < worstRank) {
            session.candidates[replaceIndex] = candidate;
            session.everHadCandidate = true;
            return true;
        }

        session.everHadCandidate = true;
        return false;
    }

    std::uint32_t FootstepTruthSessionShadow::PercentileOf(std::vector<std::uint32_t> values, float p)
    {
        if (values.empty()) {
            return 0;
        }
        p = std::clamp(p, 0.0f, 1.0f);
        std::sort(values.begin(), values.end());
        const auto idx = static_cast<std::size_t>(p * static_cast<float>(values.size() - 1));
        return values[idx];
    }

}
