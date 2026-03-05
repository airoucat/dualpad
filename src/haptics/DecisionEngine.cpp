#include "pch.h"
#include "haptics/DecisionEngine.h"

#include "haptics/AudioOnlyScorer.h"
#include "haptics/DynamicHapticPool.h"
#include "haptics/EventNormalizer.h"
#include "haptics/FormSemanticCache.h"
#include "haptics/HapticsConfig.h"
#include "haptics/SemanticResolver.h"
#include "haptics/VoiceBindingMap.h"

#include <algorithm>

namespace dualpad::haptics
{
    namespace
    {
        EventType SemanticToEventType(SemanticGroup group)
        {
            switch (group) {
            case SemanticGroup::WeaponSwing: return EventType::WeaponSwing;
            case SemanticGroup::Hit:         return EventType::HitImpact;
            case SemanticGroup::Block:       return EventType::Block;
            case SemanticGroup::Footstep:    return EventType::Footstep;
            case SemanticGroup::Bow:         return EventType::BowRelease;
            case SemanticGroup::Voice:       return EventType::Shout;
            case SemanticGroup::UI:          return EventType::UI;
            case SemanticGroup::Music:       return EventType::Music;
            case SemanticGroup::Ambient:     return EventType::Ambient;
            default:                         return EventType::Unknown;
            }
        }
    }

    DecisionEngine& DecisionEngine::GetSingleton()
    {
        static DecisionEngine s;
        return s;
    }

    void DecisionEngine::Initialize()
    {
        bool expected = false;
        _initialized.compare_exchange_strong(expected, true, std::memory_order_acq_rel);
        ResetStats();
    }

    void DecisionEngine::Shutdown()
    {
        bool expected = true;
        _initialized.compare_exchange_strong(expected, false, std::memory_order_acq_rel);
    }

    std::vector<DecisionResult> DecisionEngine::Update()
    {
        std::vector<DecisionResult> out;
        if (!_initialized.load(std::memory_order_acquire)) {
            return out;
        }

        auto& cfg = HapticsConfig::GetSingleton();
        auto l2 = AudioOnlyScorer::GetSingleton().Update();

        if (l2.empty()) {
            _noCandidate.fetch_add(1, std::memory_order_relaxed);
            _tickNoAudio.fetch_add(1, std::memory_order_relaxed);
            return out;
        }

        out.reserve(l2.size());

        constexpr float kHigh = 0.75f;
        constexpr float kMid = 0.55f;

        const auto nowUs = ToQPC(Now());
        const auto ttlUs = static_cast<std::uint64_t>(
            std::max<std::uint32_t>(100, cfg.traceBindingTtlMs) * 1000ull);

        const bool enableFormSemantic = cfg.enableFormSemanticCache && cfg.enableL1FormSemantic;
        const bool enableL1VoiceTrace = cfg.enableL1VoiceTrace;
        const float formSemanticThreshold = std::clamp(cfg.l1FormSemanticMinConfidence, 0.0f, 1.0f);
        auto& dynamicPool = DynamicHapticPool::GetSingleton();
        dynamicPool.Configure(
            cfg.enableDynamicHapticPool,
            cfg.dynamicPoolTopK,
            cfg.dynamicPoolMinConfidence,
            cfg.dynamicPoolOutputCap,
            cfg.dynamicPoolResolveMinHits,
            cfg.dynamicPoolResolveMinInputEnergy);
        const bool enableShadowProbe = cfg.enableDynamicHapticPool && cfg.enableDynamicPoolShadowProbe;
        const bool enableL2Learn = cfg.enableDynamicHapticPool && cfg.enableDynamicPoolLearnFromL2;
        const float l2LearnMinScore = std::clamp(cfg.dynamicPoolL2MinConfidence, 0.0f, 1.0f);

        std::size_t batchL1Hits = 0;
        auto tryLearnFromL2 = [&](const DecisionResult& candidate) {
            if (!enableL2Learn) {
                return;
            }

            if (candidate.matchScore < l2LearnMinScore) {
                _dynamicPoolLearnFromL2LowScore.fetch_add(1, std::memory_order_relaxed);
                return;
            }

            if (candidate.source.sourceFormId == 0 && candidate.source.eventType == EventType::Unknown) {
                _dynamicPoolLearnFromL2NoKey.fetch_add(1, std::memory_order_relaxed);
                return;
            }

            dynamicPool.ObserveL1(candidate.source, candidate.matchScore);
            _dynamicPoolLearnFromL2.fetch_add(1, std::memory_order_relaxed);
        };

        for (auto& s : l2) {
            DecisionResult r{};
            auto normalized = EventNormalizer::GetSingleton().Normalize(s, nowUs);
            r.source = normalized.source;
            r.matchScore = std::clamp(s.confidence, 0.0f, 1.0f);
            r.traceHit = false;

            const auto voicePtr = static_cast<std::uintptr_t>(s.sourceVoiceId);
            const auto& binding = normalized.binding;

            if (enableFormSemantic) {
                const auto sem = SemanticResolver::GetSingleton().Resolve(
                    r.source.sourceFormId,
                    formSemanticThreshold);

                if (sem.matched) {
                    r.source.sourceFormId = sem.formID;
                    r.traceHit = true;
                    r.layer = DecisionLayer::L1Trace;
                    r.reason = DecisionReason::L1FormSemantic;
                    r.matchScore = std::max(r.matchScore, std::clamp(sem.meta.confidence, 0.0f, 1.0f));
                    if (r.source.eventType == EventType::Unknown) {
                        r.source.eventType = SemanticToEventType(sem.meta.group);
                    }

                    _l1Count.fetch_add(1, std::memory_order_relaxed);
                    _l1FormSemanticHit.fetch_add(1, std::memory_order_relaxed);
                    if (enableShadowProbe) {
                        (void)dynamicPool.ShadowCanResolve(r.source);
                    }
                    dynamicPool.ObserveL1(r.source, r.matchScore);
                    ++batchL1Hits;

                    out.push_back(r);
                    continue;
                }

                _l1FormSemanticMiss.fetch_add(1, std::memory_order_relaxed);
                switch (sem.rejectReason) {
                case SemanticRejectReason::NoFormID:
                    _l1FormSemanticNoFormID.fetch_add(1, std::memory_order_relaxed);
                    r.reason = DecisionReason::L1FormSemanticNoFormID;
                    break;
                case SemanticRejectReason::CacheMiss:
                    _l1FormSemanticCacheMiss.fetch_add(1, std::memory_order_relaxed);
                    r.reason = DecisionReason::L1FormSemanticCacheMiss;
                    break;
                case SemanticRejectReason::LowConfidence:
                    _l1FormSemanticLowConfidence.fetch_add(1, std::memory_order_relaxed);
                    r.reason = DecisionReason::L1FormSemanticLowConfidence;
                    break;
                default:
                    break;
                }
            }

            // L1 voice binding (can be disabled for L3/DynamicPool verification)
            if (!enableL1VoiceTrace) {
                _traceBindBypassDisabled.fetch_add(1, std::memory_order_relaxed);
                r.reason = DecisionReason::L1Disabled;
            }
            else if (voicePtr != 0) {
                if (!binding.has_value()) {
                    _traceBindMissUnbound.fetch_add(1, std::memory_order_relaxed);
                }
                else {
                    const auto ageUs = (nowUs > binding->tsUs) ? (nowUs - binding->tsUs) : 0ull;
                    if (ageUs <= ttlUs) {
                        r.traceHit = true;
                        r.layer = DecisionLayer::L1Trace;
                        r.reason = DecisionReason::L1TraceHit;

                        _l1Count.fetch_add(1, std::memory_order_relaxed);
                        _traceBindHit.fetch_add(1, std::memory_order_relaxed);
                        if (enableShadowProbe) {
                            (void)dynamicPool.ShadowCanResolve(r.source);
                        }
                        if (r.source.sourceFormId != 0 || r.source.eventType != EventType::Unknown) {
                            dynamicPool.ObserveL1(r.source, r.matchScore);
                        }
                        ++batchL1Hits;

                        out.push_back(r);
                        continue;
                    }
                    else {
                        _traceBindMissExpired.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            }
            else {
                _traceBindMissUnbound.fetch_add(1, std::memory_order_relaxed);
            }

            // L2/L3 fallback
            r.layer = DecisionLayer::L2Match;

            if (r.matchScore >= kHigh) {
                r.reason = DecisionReason::L2HighScore;
                _l2Count.fetch_add(1, std::memory_order_relaxed);
                _l2HighScore.fetch_add(1, std::memory_order_relaxed);
                if (enableShadowProbe) {
                    (void)dynamicPool.ShadowCanResolve(r.source);
                }
                tryLearnFromL2(r);
            }
            else if (r.matchScore >= kMid) {
                r.reason = DecisionReason::L2MidScore;
                _l2Count.fetch_add(1, std::memory_order_relaxed);
                _l2MidScore.fetch_add(1, std::memory_order_relaxed);
                if (enableShadowProbe) {
                    (void)dynamicPool.ShadowCanResolve(r.source);
                }
                tryLearnFromL2(r);
            }
            else {
                if (cfg.fallbackBaseWhenNoMatch) {
                    r.layer = DecisionLayer::L3Fallback;
                    HapticSourceMsg poolResolved{};
                    if (dynamicPool.TryResolve(r.source, poolResolved)) {
                        r.reason = DecisionReason::L3DynamicPoolHit;
                        r.source = poolResolved;
                        _dynamicPoolHit.fetch_add(1, std::memory_order_relaxed);
                    }
                    else {
                        r.reason = DecisionReason::L3LowScoreFallback;
                        r.source.confidence = std::min(r.source.confidence, 0.55f);
                        r.source.left *= 0.75f;
                        r.source.right *= 0.75f;
                        _lowScoreFallback.fetch_add(1, std::memory_order_relaxed);
                        _dynamicPoolMiss.fetch_add(1, std::memory_order_relaxed);
                    }

                    _l3Count.fetch_add(1, std::memory_order_relaxed);
                }
                else {
                    r.reason = DecisionReason::L2LowScorePass;
                    _l2Count.fetch_add(1, std::memory_order_relaxed);
                    _l2LowScorePass.fetch_add(1, std::memory_order_relaxed);
                    if (enableShadowProbe) {
                        (void)dynamicPool.ShadowCanResolve(r.source);
                    }
                }
            }

            out.push_back(r);
        }

        if (!l2.empty() && batchL1Hits == 0) {
            _audioPresentNoMatch.fetch_add(1, std::memory_order_relaxed);
        }
        return out;
    }

    DecisionEngine::Stats DecisionEngine::GetStats() const
    {
        Stats s{};
        s.l1Count = _l1Count.load(std::memory_order_relaxed);
        s.l2Count = _l2Count.load(std::memory_order_relaxed);
        s.l3Count = _l3Count.load(std::memory_order_relaxed);
        s.l2HighScore = _l2HighScore.load(std::memory_order_relaxed);
        s.l2MidScore = _l2MidScore.load(std::memory_order_relaxed);
        s.l2LowScorePass = _l2LowScorePass.load(std::memory_order_relaxed);
        s.noCandidate = _noCandidate.load(std::memory_order_relaxed);
        s.lowScoreFallback = _lowScoreFallback.load(std::memory_order_relaxed);
        s.traceBindHit = _traceBindHit.load(std::memory_order_relaxed);
        s.tickNoAudio = _tickNoAudio.load(std::memory_order_relaxed);
        s.audioPresentNoMatch = _audioPresentNoMatch.load(std::memory_order_relaxed);
        s.traceBindMissUnbound = _traceBindMissUnbound.load(std::memory_order_relaxed);
        s.traceBindMissExpired = _traceBindMissExpired.load(std::memory_order_relaxed);
        s.traceBindBypassDisabled = _traceBindBypassDisabled.load(std::memory_order_relaxed);
        s.l1FormSemanticHit = _l1FormSemanticHit.load(std::memory_order_relaxed);
        s.l1FormSemanticMiss = _l1FormSemanticMiss.load(std::memory_order_relaxed);
        s.l1FormSemanticNoFormID = _l1FormSemanticNoFormID.load(std::memory_order_relaxed);
        s.l1FormSemanticCacheMiss = _l1FormSemanticCacheMiss.load(std::memory_order_relaxed);
        s.l1FormSemanticLowConfidence = _l1FormSemanticLowConfidence.load(std::memory_order_relaxed);
        s.dynamicPoolHit = _dynamicPoolHit.load(std::memory_order_relaxed);
        s.dynamicPoolMiss = _dynamicPoolMiss.load(std::memory_order_relaxed);
        s.dynamicPoolLearnFromL2 = _dynamicPoolLearnFromL2.load(std::memory_order_relaxed);
        s.dynamicPoolLearnFromL2NoKey = _dynamicPoolLearnFromL2NoKey.load(std::memory_order_relaxed);
        s.dynamicPoolLearnFromL2LowScore = _dynamicPoolLearnFromL2LowScore.load(std::memory_order_relaxed);
        return s;
    }

    void DecisionEngine::ResetStats()
    {
        _l1Count.store(0, std::memory_order_relaxed);
        _l2Count.store(0, std::memory_order_relaxed);
        _l3Count.store(0, std::memory_order_relaxed);
        _l2HighScore.store(0, std::memory_order_relaxed);
        _l2MidScore.store(0, std::memory_order_relaxed);
        _l2LowScorePass.store(0, std::memory_order_relaxed);
        _noCandidate.store(0, std::memory_order_relaxed);
        _lowScoreFallback.store(0, std::memory_order_relaxed);
        _traceBindHit.store(0, std::memory_order_relaxed);
        _tickNoAudio.store(0, std::memory_order_relaxed);
        _audioPresentNoMatch.store(0, std::memory_order_relaxed);
        _traceBindMissUnbound.store(0, std::memory_order_relaxed);
        _traceBindMissExpired.store(0, std::memory_order_relaxed);
        _traceBindBypassDisabled.store(0, std::memory_order_relaxed);
        _l1FormSemanticHit.store(0, std::memory_order_relaxed);
        _l1FormSemanticMiss.store(0, std::memory_order_relaxed);
        _l1FormSemanticNoFormID.store(0, std::memory_order_relaxed);
        _l1FormSemanticCacheMiss.store(0, std::memory_order_relaxed);
        _l1FormSemanticLowConfidence.store(0, std::memory_order_relaxed);
        _dynamicPoolHit.store(0, std::memory_order_relaxed);
        _dynamicPoolMiss.store(0, std::memory_order_relaxed);
        _dynamicPoolLearnFromL2.store(0, std::memory_order_relaxed);
        _dynamicPoolLearnFromL2NoKey.store(0, std::memory_order_relaxed);
        _dynamicPoolLearnFromL2LowScore.store(0, std::memory_order_relaxed);
    }
}
