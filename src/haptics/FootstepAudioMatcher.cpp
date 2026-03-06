#include "pch.h"
#include "haptics/FootstepAudioMatcher.h"

#include "haptics/FootstepTruthBridge.h"
#include "haptics/FormSemanticCache.h"
#include "haptics/HapticsConfig.h"
#include "haptics/InstanceTraceCache.h"
#include "haptics/VoiceBindingMap.h"

#include <SKSE/SKSE.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace logger = SKSE::log;

namespace dualpad::haptics
{
    namespace
    {
        constexpr std::uint32_t kMaxProbeLinesPerSecond = 6;
        constexpr float kTimeTauUs = 18000.0f;

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

        float Clamp01(float v)
        {
            return std::clamp(v, 0.0f, 1.0f);
        }

        std::uint64_t ComputeTimeDistanceUs(std::uint64_t truthUs, const AudioFeatureMsg& feature)
        {
            if (feature.qpcStart == 0 || feature.qpcEnd == 0) {
                return std::numeric_limits<std::uint64_t>::max();
            }
            if (truthUs < feature.qpcStart) {
                return feature.qpcStart - truthUs;
            }
            if (truthUs > feature.qpcEnd) {
                return truthUs - feature.qpcEnd;
            }
            return 0;
        }

        bool ResolveFootstepMeta(const TraceMeta& trace, float& outConfidence)
        {
            outConfidence = 0.0f;
            if (trace.preferredEvent == EventType::Footstep) {
                outConfidence = std::max(outConfidence, Clamp01(trace.confidence));
            }
            if (trace.semantic == SemanticGroup::Footstep) {
                outConfidence = std::max(outConfidence, Clamp01(trace.confidence));
            }

            const auto formId = (trace.sourceFormId != 0) ? trace.sourceFormId : trace.soundFormId;
            if (formId != 0) {
                FormSemanticMeta meta{};
                if (FormSemanticCache::GetSingleton().TryGet(formId, meta) &&
                    meta.group == SemanticGroup::Footstep) {
                    outConfidence = std::max(outConfidence, Clamp01(meta.confidence));
                }
            }

            return outConfidence > 0.0f;
        }

        bool FeatureMatchesBridgeBinding(
            const AudioFeatureMsg& feature,
            const FootstepTruthBridge::Binding& binding)
        {
            if (binding.instanceId != 0 &&
                feature.instanceId != 0 &&
                feature.instanceId == binding.instanceId) {
                return true;
            }

            return binding.voicePtr != 0 &&
                binding.generation != 0 &&
                feature.voiceId == static_cast<std::uint64_t>(binding.voicePtr) &&
                feature.voiceGeneration == binding.generation;
        }
    }

    FootstepAudioMatcher& FootstepAudioMatcher::GetSingleton()
    {
        static FootstepAudioMatcher instance;
        return instance;
    }

    void FootstepAudioMatcher::Reset()
    {
        _audioWriteSeq.store(0, std::memory_order_relaxed);
        _featuresObserved.store(0, std::memory_order_relaxed);
        _truthsObserved.store(0, std::memory_order_relaxed);
        _truthsMatched.store(0, std::memory_order_relaxed);
        _truthBridgeBound.store(0, std::memory_order_relaxed);
        _truthBridgeMatched.store(0, std::memory_order_relaxed);
        _truthBridgeNoFeature.store(0, std::memory_order_relaxed);
        _truthNoWindow.store(0, std::memory_order_relaxed);
        _truthNoSemantic.store(0, std::memory_order_relaxed);
        _truthLowScore.store(0, std::memory_order_relaxed);
        _windowCandidates.store(0, std::memory_order_relaxed);
        _semanticCandidates.store(0, std::memory_order_relaxed);
        _bindingMissCandidates.store(0, std::memory_order_relaxed);
        _traceMissCandidates.store(0, std::memory_order_relaxed);
        {
            std::scoped_lock lock(_pendingMutex);
            _pendingTruths.clear();
        }
        {
            std::scoped_lock lock(_sampleMutex);
            _matchDeltasUs.clear();
            _matchDurationsUs.clear();
            _matchScoresPermille.clear();
            _matchPanAbsPermille.clear();
        }
    }

    void FootstepAudioMatcher::ObserveAudioFeature(const AudioFeatureMsg& msg)
    {
        const auto& cfg = HapticsConfig::GetSingleton();
        if (!cfg.enableFootstepAudioMatcherShadow) {
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

    void FootstepAudioMatcher::ObserveTruthEvent(std::uint64_t truthUs, std::string_view tag)
    {
        const auto& cfg = HapticsConfig::GetSingleton();
        if (!cfg.enableFootstepAudioMatcherShadow) {
            return;
        }

        _truthsObserved.fetch_add(1, std::memory_order_relaxed);
        std::scoped_lock lock(_pendingMutex);
        _pendingTruths.push_back(PendingTruth{
            .truthUs = truthUs,
            .tag = std::string(tag)
        });
    }

    void FootstepAudioMatcher::Tick(std::uint64_t nowUs)
    {
        const auto& cfg = HapticsConfig::GetSingleton();
        if (!cfg.enableFootstepAudioMatcherShadow) {
            return;
        }

        if (nowUs == 0) {
            nowUs = ToQPC(Now());
        }

        ProcessMatureTruths(nowUs);
    }

    FootstepAudioMatcher::Stats FootstepAudioMatcher::GetStats()
    {
        Tick(ToQPC(Now()));

        Stats s;
        s.featuresObserved = _featuresObserved.load(std::memory_order_relaxed);
        s.truthsObserved = _truthsObserved.load(std::memory_order_relaxed);
        s.truthsMatched = _truthsMatched.load(std::memory_order_relaxed);
        s.truthBridgeBound = _truthBridgeBound.load(std::memory_order_relaxed);
        s.truthBridgeMatched = _truthBridgeMatched.load(std::memory_order_relaxed);
        s.truthBridgeNoFeature = _truthBridgeNoFeature.load(std::memory_order_relaxed);
        s.truthNoWindow = _truthNoWindow.load(std::memory_order_relaxed);
        s.truthNoSemantic = _truthNoSemantic.load(std::memory_order_relaxed);
        s.truthLowScore = _truthLowScore.load(std::memory_order_relaxed);
        s.windowCandidates = _windowCandidates.load(std::memory_order_relaxed);
        s.semanticCandidates = _semanticCandidates.load(std::memory_order_relaxed);
        s.bindingMissCandidates = _bindingMissCandidates.load(std::memory_order_relaxed);
        s.traceMissCandidates = _traceMissCandidates.load(std::memory_order_relaxed);
        {
            std::scoped_lock lock(_sampleMutex);
            s.matchDeltaP50Us = PercentileOf(_matchDeltasUs, 0.50f);
            s.matchDeltaP95Us = PercentileOf(_matchDeltasUs, 0.95f);
            s.matchDurationP50Us = PercentileOf(_matchDurationsUs, 0.50f);
            s.matchDurationP95Us = PercentileOf(_matchDurationsUs, 0.95f);
            s.matchScoreP50Permille = PercentileOf(_matchScoresPermille, 0.50f);
            s.matchScoreP95Permille = PercentileOf(_matchScoresPermille, 0.95f);
            s.matchPanAbsP50Permille = PercentileOf(_matchPanAbsPermille, 0.50f);
            s.matchPanAbsP95Permille = PercentileOf(_matchPanAbsPermille, 0.95f);
            s.samples = static_cast<std::uint32_t>(_matchDeltasUs.size());
        }
        {
            std::scoped_lock lock(_pendingMutex);
            s.pendingTruths = static_cast<std::uint32_t>(_pendingTruths.size());
        }
        return s;
    }

    void FootstepAudioMatcher::ProcessMatureTruths(std::uint64_t nowUs)
    {
        static std::atomic<std::uint64_t> s_probeWindowUs{ 0 };
        static std::atomic<std::uint32_t> s_probeLines{ 0 };

        const auto& cfg = HapticsConfig::GetSingleton();
        if (!cfg.enableFootstepAudioMatcherShadow) {
            return;
        }

        const auto lookaheadUs = std::max<std::uint64_t>(
            cfg.footstepAudioMatcherLookaheadUs,
            cfg.enableFootstepTruthBridgeShadow ?
                static_cast<std::uint64_t>(cfg.footstepTruthBridgeLookaheadUs) :
                0ull);
        std::deque<PendingTruth> matured{};
        {
            std::scoped_lock lock(_pendingMutex);
            while (!_pendingTruths.empty()) {
                const auto& pending = _pendingTruths.front();
                if (pending.truthUs == 0 || (pending.truthUs + lookaheadUs) > nowUs) {
                    break;
                }
                matured.push_back(std::move(_pendingTruths.front()));
                _pendingTruths.pop_front();
            }
        }

        for (const auto& truth : matured) {
            const auto match = MatchTruth(truth.truthUs, truth.tag);
            if (match.bridgeBound) {
                _truthBridgeBound.fetch_add(1, std::memory_order_relaxed);
            }
            if (match.bridgeMatched) {
                _truthBridgeMatched.fetch_add(1, std::memory_order_relaxed);
            }
            if (match.bridgeNoFeature) {
                _truthBridgeNoFeature.fetch_add(1, std::memory_order_relaxed);
            }
            _windowCandidates.fetch_add(match.windowCandidates, std::memory_order_relaxed);
            _semanticCandidates.fetch_add(match.semanticCandidates, std::memory_order_relaxed);
            _bindingMissCandidates.fetch_add(match.bindingMissCandidates, std::memory_order_relaxed);
            _traceMissCandidates.fetch_add(match.traceMissCandidates, std::memory_order_relaxed);

            if (match.matched) {
                _truthsMatched.fetch_add(1, std::memory_order_relaxed);
                {
                    std::scoped_lock lock(_sampleMutex);
                    if (_matchDeltasUs.size() >= kSampleCap) {
                        _matchDeltasUs.erase(_matchDeltasUs.begin());
                    }
                    if (_matchDurationsUs.size() >= kSampleCap) {
                        _matchDurationsUs.erase(_matchDurationsUs.begin());
                    }
                    if (_matchScoresPermille.size() >= kSampleCap) {
                        _matchScoresPermille.erase(_matchScoresPermille.begin());
                    }
                    if (_matchPanAbsPermille.size() >= kSampleCap) {
                        _matchPanAbsPermille.erase(_matchPanAbsPermille.begin());
                    }
                    _matchDeltasUs.push_back(match.matchDeltaUs);
                    _matchDurationsUs.push_back(match.matchDurationUs);
                    _matchScoresPermille.push_back(match.matchScorePermille);
                    _matchPanAbsPermille.push_back(match.matchPanAbsPermille);
                }

                if ((match.matchDeltaUs >= 20000u || match.matchScorePermille < 650u) &&
                    ShouldEmitWindowedProbe(
                        s_probeWindowUs,
                        s_probeLines,
                        truth.truthUs,
                        kMaxProbeLinesPerSecond)) {
                    logger::info(
                        "[Haptics][FootAudio] shadow_weak tag={} delta={}us score={:.2f} dur={}us panAbs={:.2f} windowCand={} semCand={}",
                        truth.tag,
                        match.matchDeltaUs,
                        static_cast<float>(match.matchScorePermille) / 1000.0f,
                        match.matchDurationUs,
                        static_cast<float>(match.matchPanAbsPermille) / 1000.0f,
                        match.windowCandidates,
                        match.semanticCandidates);
                }
                continue;
            }

            const char* reason = "low_score";
            if (match.bridgeNoFeature) {
                _truthLowScore.fetch_add(1, std::memory_order_relaxed);
                reason = "bridge_no_feature";
            } else if (match.windowCandidates == 0) {
                _truthNoWindow.fetch_add(1, std::memory_order_relaxed);
                reason = "no_window";
            } else if (match.semanticCandidates == 0) {
                _truthNoSemantic.fetch_add(1, std::memory_order_relaxed);
                reason = "no_semantic";
            } else {
                _truthLowScore.fetch_add(1, std::memory_order_relaxed);
            }

            if (ShouldEmitWindowedProbe(
                    s_probeWindowUs,
                    s_probeLines,
                    truth.truthUs,
                    kMaxProbeLinesPerSecond)) {
                logger::info(
                    "[Haptics][FootAudio] shadow_miss tag={} reason={} bestScore={:.2f} bestDelta={}us windowCand={} semCand={} bindMiss={} traceMiss={}",
                    truth.tag,
                    reason,
                    match.bestScore,
                    match.bestDeltaUs,
                    match.windowCandidates,
                    match.semanticCandidates,
                    match.bindingMissCandidates,
                    match.traceMissCandidates);
            }
        }
    }

    FootstepAudioMatcher::MatchResult FootstepAudioMatcher::MatchTruth(
        std::uint64_t truthUs,
        std::string_view tag) const
    {
        MatchResult out{};
        bool haveBest = false;
        const auto& cfg = HapticsConfig::GetSingleton();
        const auto latestSeq = _audioWriteSeq.load(std::memory_order_acquire);
        if (latestSeq == 0) {
            return out;
        }

        const auto lookbackUs = static_cast<std::uint64_t>(cfg.footstepAudioMatcherLookbackUs);
        const auto lookaheadUs = static_cast<std::uint64_t>(cfg.footstepAudioMatcherLookaheadUs);
        const auto ttlUs = static_cast<std::uint64_t>(
            std::max<std::uint32_t>(100u, cfg.traceBindingTtlMs) * 1000ull);
        const auto scanBudget = std::min<std::uint64_t>({
            latestSeq,
            static_cast<std::uint64_t>(kAudioRingCapacity),
            static_cast<std::uint64_t>(std::max<std::uint32_t>(1u, cfg.footstepAudioMatcherMaxCandidates))
        });

        const auto bridgeBinding =
            cfg.enableFootstepTruthBridgeShadow ?
            FootstepTruthBridge::GetSingleton().TryGetBindingForTruth(truthUs, tag) :
            std::nullopt;
        if (bridgeBinding.has_value()) {
            out.bridgeBound = true;

            MatchResult bridgeBest = out;
            bool bridgeHaveBest = false;
            bool bridgeSawIdentityFeature = false;
            for (std::uint64_t i = 0; i < scanBudget; ++i) {
                const auto seq = latestSeq - i;
                if (seq == 0) {
                    break;
                }

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

                if (!FeatureMatchesBridgeBinding(feature, *bridgeBinding)) {
                    continue;
                }

                bridgeSawIdentityFeature = true;
                ++bridgeBest.windowCandidates;

                std::optional<TraceMeta> trace{};
                if (feature.instanceId != 0) {
                    trace = InstanceTraceCache::GetSingleton().TryGet(feature.instanceId);
                }
                if (!trace.has_value() && bridgeBinding->instanceId != 0) {
                    trace = InstanceTraceCache::GetSingleton().TryGet(bridgeBinding->instanceId);
                }
                if (!trace.has_value()) {
                    ++bridgeBest.traceMissCandidates;
                    continue;
                }

                float metaConfidence = 0.0f;
                if (!ResolveFootstepMeta(*trace, metaConfidence)) {
                    continue;
                }

                ++bridgeBest.semanticCandidates;

                const auto deltaTruthUs = ComputeTimeDistanceUs(truthUs, feature);
                const auto deltaObservedUs = ComputeTimeDistanceUs(bridgeBinding->observedUs, feature);
                const auto durationUs = static_cast<std::uint32_t>(
                    std::clamp<std::uint64_t>(
                        (feature.qpcEnd > feature.qpcStart) ? (feature.qpcEnd - feature.qpcStart) : 0ull,
                        0ull,
                        std::numeric_limits<std::uint32_t>::max()));
                const auto sum = std::max(1e-4f, feature.energyL + feature.energyR);
                const auto pan = std::clamp((feature.energyR - feature.energyL) / sum, -1.0f, 1.0f);
                const auto panAbs = std::abs(pan);
                const auto timeScore = std::max(
                    0.25f,
                    Clamp01(std::exp(-static_cast<float>(deltaObservedUs) / 85000.0f)));
                const auto energyScore = Clamp01(0.65f * feature.peak + 0.35f * feature.rms);
                const auto score = Clamp01(
                    0.50f * Clamp01(metaConfidence) +
                    0.20f * 1.0f +
                    0.20f * energyScore +
                    0.10f * timeScore);

                const bool better =
                    !bridgeHaveBest ||
                    score > bridgeBest.bestScore ||
                    (score == bridgeBest.bestScore && deltaTruthUs < bridgeBest.bestDeltaUs);
                if (!better) {
                    continue;
                }

                bridgeHaveBest = true;
                bridgeBest.bestScore = score;
                bridgeBest.bestDeltaUs = deltaTruthUs;
                if (score >= cfg.footstepAudioMatcherMinScore) {
                    bridgeBest.matched = true;
                    bridgeBest.bridgeMatched = true;
                    bridgeBest.matchDeltaUs = static_cast<std::uint32_t>(
                        std::min<std::uint64_t>(deltaTruthUs, std::numeric_limits<std::uint32_t>::max()));
                    bridgeBest.matchDurationUs = durationUs;
                    bridgeBest.matchScorePermille = static_cast<std::uint16_t>(
                        std::clamp(score * 1000.0f, 0.0f, 1000.0f));
                    bridgeBest.matchPanAbsPermille = static_cast<std::uint16_t>(
                        std::clamp(panAbs * 1000.0f, 0.0f, 1000.0f));
                }
            }

            if (bridgeHaveBest) {
                bridgeBest.bridgeBound = true;
                return bridgeBest;
            }
            if (!bridgeSawIdentityFeature) {
                out.bridgeNoFeature = true;
            }
        }

        for (std::uint64_t i = 0; i < scanBudget; ++i) {
            const auto seq = latestSeq - i;
            if (seq == 0) {
                break;
            }

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

            if (feature.qpcStart == 0 || feature.qpcEnd == 0 || feature.voiceId == 0) {
                continue;
            }
            if ((feature.qpcEnd + lookbackUs) < truthUs || feature.qpcStart > (truthUs + lookaheadUs)) {
                continue;
            }

            ++out.windowCandidates;

            std::optional<TraceMeta> trace{};
            float voiceScore = 1.0f;
            std::uint64_t bindingAgeUs = 0;
            if (feature.instanceId != 0) {
                trace = InstanceTraceCache::GetSingleton().TryGet(feature.instanceId);
            } else {
                const auto voicePtr = static_cast<std::uintptr_t>(feature.voiceId);
                const auto binding = VoiceBindingMap::GetSingleton().TryGet(voicePtr);
                if (!binding.has_value()) {
                    ++out.bindingMissCandidates;
                    continue;
                }

                if (feature.voiceGeneration != 0 && binding->generation != feature.voiceGeneration) {
                    ++out.bindingMissCandidates;
                    continue;
                }

                bindingAgeUs = (truthUs > binding->tsUs) ? (truthUs - binding->tsUs) : 0ull;
                if (bindingAgeUs > ttlUs) {
                    ++out.bindingMissCandidates;
                    continue;
                }

                voiceScore = Clamp01(1.0f - (static_cast<float>(bindingAgeUs) / static_cast<float>(ttlUs)));
                trace = InstanceTraceCache::GetSingleton().TryGet(binding->instanceId);
            }

            if (!trace.has_value()) {
                ++out.traceMissCandidates;
                continue;
            }

            float metaConfidence = 0.0f;
            if (!ResolveFootstepMeta(*trace, metaConfidence)) {
                continue;
            }

            ++out.semanticCandidates;

            const auto deltaUs = ComputeTimeDistanceUs(truthUs, feature);
            const auto durationUs = static_cast<std::uint32_t>(
                std::clamp<std::uint64_t>(
                    (feature.qpcEnd > feature.qpcStart) ? (feature.qpcEnd - feature.qpcStart) : 0ull,
                    0ull,
                    std::numeric_limits<std::uint32_t>::max()));
            const auto sum = std::max(1e-4f, feature.energyL + feature.energyR);
            const auto pan = std::clamp((feature.energyR - feature.energyL) / sum, -1.0f, 1.0f);
            const auto panAbs = std::abs(pan);
            const auto timeScore = Clamp01(std::exp(-static_cast<float>(deltaUs) / kTimeTauUs));
            const auto energyScore = Clamp01(0.65f * feature.peak + 0.35f * feature.rms);
            const auto score = Clamp01(
                0.35f * timeScore +
                0.35f * Clamp01(metaConfidence) +
                0.15f * voiceScore +
                0.15f * energyScore);

            const bool better =
                !haveBest ||
                score > out.bestScore ||
                (score == out.bestScore && deltaUs < out.bestDeltaUs);
            if (!better) {
                continue;
            }

            haveBest = true;
            out.bestScore = score;
            out.bestDeltaUs = deltaUs;
            if (score >= cfg.footstepAudioMatcherMinScore) {
                out.matched = true;
                out.matchDeltaUs = static_cast<std::uint32_t>(
                    std::min<std::uint64_t>(deltaUs, std::numeric_limits<std::uint32_t>::max()));
                out.matchDurationUs = durationUs;
                out.matchScorePermille = static_cast<std::uint16_t>(
                    std::clamp(score * 1000.0f, 0.0f, 1000.0f));
                out.matchPanAbsPermille = static_cast<std::uint16_t>(
                    std::clamp(panAbs * 1000.0f, 0.0f, 1000.0f));
            }
        }

        return out;
    }

    std::uint32_t FootstepAudioMatcher::PercentileOf(std::vector<std::uint32_t> values, float p)
    {
        if (values.empty()) {
            return 0;
        }

        const auto clamped = std::clamp(p, 0.0f, 1.0f);
        const auto idx = static_cast<std::size_t>(clamped * static_cast<float>(values.size() - 1));
        std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(idx), values.end());
        return values[idx];
    }
}
