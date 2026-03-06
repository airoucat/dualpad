#include "pch.h"
#include "haptics/DecisionEngine.h"

#include "haptics/AudioOnlyScorer.h"
#include "haptics/DynamicHapticPool.h"
#include "haptics/EventNormalizer.h"
#include "haptics/FormSemanticCache.h"
#include "haptics/HapticEligibilityEngine.h"
#include "haptics/HapticsConfig.h"
#include "haptics/SemanticResolver.h"
#include "haptics/SessionFormPromoter.h"
#include "haptics/VoiceBindingMap.h"

#include <algorithm>

namespace logger = SKSE::log;

namespace dualpad::haptics
{
    namespace
    {
        bool IsStructuredCombatEvent(EventType type)
        {
            switch (type) {
            case EventType::WeaponSwing:
            case EventType::HitImpact:
            case EventType::Block:
                return true;
            default:
                return false;
            }
        }

        const char* DecisionLayerLabel(DecisionLayer layer)
        {
            switch (layer) {
            case DecisionLayer::L1Trace:
                return "L1";
            case DecisionLayer::L2Match:
                return "L2";
            case DecisionLayer::L3Fallback:
                return "L3";
            default:
                return "UNK";
            }
        }

        const char* DecisionReasonLabel(DecisionReason reason)
        {
            switch (reason) {
            case DecisionReason::L1FormSemantic:          return "L1FormSemantic";
            case DecisionReason::L1TraceHit:              return "L1TraceHit";
            case DecisionReason::L1TraceMiss:             return "L1TraceMiss";
            case DecisionReason::L2HighScore:             return "L2High";
            case DecisionReason::L2MidScore:              return "L2Mid";
            case DecisionReason::L2LowScorePass:          return "L2LowPass";
            case DecisionReason::L3DynamicPoolHit:        return "L3Dyn";
            case DecisionReason::L3LowScoreFallback:      return "L3Fallback";
            case DecisionReason::GateUnknownBlocked:      return "GateUnknown";
            case DecisionReason::GateBackgroundBlocked:   return "GateBackground";
            case DecisionReason::GateNoTraceContext:      return "GateNoTrace";
            case DecisionReason::GateLowSemanticConfidence:return "GateLowSem";
            case DecisionReason::GateLowRelativeEnergy:   return "GateLowRel";
            case DecisionReason::GateRefractoryBlocked:   return "GateRefractory";
            default:
                return "Other";
            }
        }

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

        DecisionReason ToDecisionReason(GateRejectReason reason)
        {
            switch (reason) {
            case GateRejectReason::UnknownBlocked:
                return DecisionReason::GateUnknownBlocked;
            case GateRejectReason::BackgroundBlocked:
                return DecisionReason::GateBackgroundBlocked;
            case GateRejectReason::NoTraceContext:
                return DecisionReason::GateNoTraceContext;
            case GateRejectReason::LowSemanticConfidence:
                return DecisionReason::GateLowSemanticConfidence;
            case GateRejectReason::LowRelativeEnergy:
                return DecisionReason::GateLowRelativeEnergy;
            case GateRejectReason::RefractoryBlocked:
                return DecisionReason::GateRefractoryBlocked;
            default:
                return DecisionReason::NoCandidate;
            }
        }

        int GetDefaultPriorityForEvent(const HapticsConfig& cfg, EventType type)
        {
            switch (type) {
            case EventType::HitImpact:
            case EventType::Block:
                return cfg.priorityHit;
            case EventType::WeaponSwing:
            case EventType::SpellCast:
            case EventType::SpellImpact:
            case EventType::BowRelease:
            case EventType::Shout:
                return cfg.prioritySwing;
            case EventType::Footstep:
            case EventType::Jump:
            case EventType::Land:
                return cfg.priorityFootstep;
            case EventType::UI:
            case EventType::Music:
            case EventType::Ambient:
                return cfg.priorityAmbient;
            default:
                return cfg.priorityFootstep;
            }
        }

        std::uint32_t GetDefaultTtlForEvent(EventType type)
        {
            switch (type) {
            case EventType::WeaponSwing:
                return 74;
            case EventType::HitImpact:
            case EventType::Block:
                return 68;
            case EventType::SpellCast:
            case EventType::SpellImpact:
            case EventType::BowRelease:
            case EventType::Shout:
                return 64;
            case EventType::Footstep:
            case EventType::Jump:
            case EventType::Land:
                return 44;
            case EventType::UI:
                return 28;
            case EventType::Music:
            case EventType::Ambient:
                return 24;
            default:
                return 28;
            }
        }

        bool IsForegroundSemantic(SemanticGroup group, bool allowUnknownFootstep)
        {
            switch (group) {
            case SemanticGroup::WeaponSwing:
            case SemanticGroup::Hit:
            case SemanticGroup::Block:
            case SemanticGroup::Bow:
            case SemanticGroup::Voice:
                return true;
            case SemanticGroup::Footstep:
                return allowUnknownFootstep;
            default:
                return false;
            }
        }

        bool IsForegroundEvent(EventType type, bool allowUnknownFootstep)
        {
            switch (type) {
            case EventType::WeaponSwing:
            case EventType::HitImpact:
            case EventType::SpellCast:
            case EventType::SpellImpact:
            case EventType::BowRelease:
            case EventType::Jump:
            case EventType::Land:
            case EventType::Block:
            case EventType::Shout:
                return true;
            case EventType::Footstep:
                return allowUnknownFootstep;
            default:
                return false;
            }
        }

        void ApplyEventProfile(HapticSourceMsg& source, const HapticsConfig& cfg)
        {
            if (source.eventType == EventType::Unknown) {
                source.priority = std::min(source.priority, std::max(10, cfg.priorityFootstep));
                const bool hasFormContext = (source.sourceFormId != 0);
                const std::uint32_t ttlMin = hasFormContext ? 28u : 24u;
                const std::uint32_t ttlMax = hasFormContext ? 96u : 72u;
                source.ttlMs = std::clamp(source.ttlMs, ttlMin, ttlMax);
                return;
            }

            const auto* eventCfg = cfg.GetEventConfig(source.eventType);
            const auto priorityFloor = eventCfg ?
                static_cast<int>(eventCfg->priority) :
                GetDefaultPriorityForEvent(cfg, source.eventType);
            const auto ttlFloor = eventCfg ?
                eventCfg->ttlMs :
                GetDefaultTtlForEvent(source.eventType);

            source.priority = std::max(source.priority, priorityFloor);
            source.ttlMs = std::clamp(
                std::max(source.ttlMs, ttlFloor),
                24u,
                360u);
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
        HapticEligibilityEngine::GetSingleton().ResetStats();
        SessionFormPromoter::GetSingleton().Reset();
    }

    void DecisionEngine::Shutdown()
    {
        bool expected = true;
        _initialized.compare_exchange_strong(expected, false, std::memory_order_acq_rel);
    }

    std::vector<DecisionResult> DecisionEngine::Update()
    {
        static std::atomic<std::uint64_t> s_attackDecisionProbeWindowUs{ 0 };
        static std::atomic<std::uint32_t> s_attackDecisionProbeLines{ 0 };

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
        const float l2LearnMinScore = std::min(
            0.56f,
            std::clamp(cfg.dynamicPoolL2MinConfidence, 0.0f, 1.0f));

        std::size_t batchL1Hits = 0;
        auto tryLearnFromL2 = [&](const DecisionResult& candidate) {
            if (!enableL2Learn) {
                return;
            }

            if (candidate.matchScore < l2LearnMinScore) {
                _dynamicPoolLearnFromL2LowScore.fetch_add(1, std::memory_order_relaxed);
                return;
            }

            // Unknown without form key is too noisy for session learning.
            if (candidate.source.eventType == EventType::Unknown &&
                candidate.source.sourceFormId == 0) {
                _dynamicPoolLearnFromL2NoKey.fetch_add(1, std::memory_order_relaxed);
                return;
            }

            dynamicPool.ObserveL1(candidate.source, candidate.matchScore);
            _dynamicPoolLearnFromL2.fetch_add(1, std::memory_order_relaxed);
        };

        auto& sessionPromoter = SessionFormPromoter::GetSingleton();
        for (auto& s : l2) {
            DecisionResult r{};
            auto normalized = EventNormalizer::GetSingleton().Normalize(s, nowUs);
            r.source = normalized.source;
            r.matchScore = std::clamp(s.confidence, 0.0f, 1.0f);
            r.traceHit = false;

            const auto voicePtr = static_cast<std::uintptr_t>(s.sourceVoiceId);
            const auto& binding = normalized.binding;

            GateContext gateCtx{};
            gateCtx.hasBinding = normalized.binding.has_value();
            gateCtx.hasTrace = normalized.trace.has_value();
            if (normalized.trace.has_value()) {
                gateCtx.tracePreferredEvent = normalized.trace->preferredEvent;
                gateCtx.traceConfidence = std::clamp(normalized.trace->confidence, 0.0f, 1.0f);
            }

            auto observeSessionPromoter = [&](std::uint32_t formId, EventType eventType, float confidence) {
                if (formId == 0 || !IsForegroundEvent(eventType, cfg.allowUnknownFootstep)) {
                    return;
                }
                sessionPromoter.Observe(
                    formId,
                    eventType,
                    std::clamp(confidence, 0.0f, 1.0f),
                    nowUs);
            };

            auto tryPromoteFromSession = [&](std::uint32_t formId) {
                EventType promotedEvent = EventType::Unknown;
                float promotedConfidence = 0.0f;
                if (!sessionPromoter.TryPromote(formId, nowUs, promotedEvent, promotedConfidence)) {
                    return false;
                }
                if (!IsForegroundEvent(promotedEvent, cfg.allowUnknownFootstep)) {
                    return false;
                }
                r.source.eventType = promotedEvent;
                r.source.confidence = std::max(
                    r.source.confidence,
                    std::clamp(promotedConfidence, 0.0f, 1.0f));
                r.source.flags = static_cast<std::uint8_t>(r.source.flags | HapticSourceFlagSessionPromoted);
                return true;
            };

            bool unknownSemanticHintStrong = false;
            bool unknownTraceSemanticContext = false;
            SemanticResolveResult semHint{};
            if (r.source.sourceFormId != 0) {
                semHint = SemanticResolver::GetSingleton().Resolve(r.source.sourceFormId, 0.0f);
                if (semHint.matched) {
                    observeSessionPromoter(
                        semHint.formID,
                        SemanticToEventType(semHint.meta.group),
                        semHint.meta.confidence);
                }
            }

            if (normalized.trace.has_value()) {
                const auto& trace = *normalized.trace;
                if (r.source.sourceFormId != 0 && trace.preferredEvent != EventType::Unknown) {
                    observeSessionPromoter(r.source.sourceFormId, trace.preferredEvent, trace.confidence);
                }
                if (r.source.sourceFormId != 0 && trace.semantic != SemanticGroup::Unknown) {
                    observeSessionPromoter(
                        r.source.sourceFormId,
                        SemanticToEventType(trace.semantic),
                        trace.confidence);
                }

                if (r.source.eventType == EventType::Unknown) {
                    const bool canPromoteFromTracePreferred =
                        trace.preferredEvent != EventType::Unknown &&
                        trace.confidence >= std::min(
                            0.55f,
                            std::clamp(cfg.tracePreferredEventMinConfidence, 0.0f, 1.0f));
                    if (canPromoteFromTracePreferred) {
                        r.source.eventType = trace.preferredEvent;
                        r.source.confidence = std::max(r.source.confidence, trace.confidence);
                    }
                    else if (trace.semantic != SemanticGroup::Unknown) {
                        const bool fgTraceSemantic =
                            IsForegroundSemantic(trace.semantic, cfg.allowUnknownFootstep) &&
                            trace.confidence >= 0.50f;
                        unknownTraceSemanticContext = fgTraceSemantic;
                        unknownSemanticHintStrong = fgTraceSemantic;
                        if (fgTraceSemantic) {
                            const auto promoted = SemanticToEventType(trace.semantic);
                            if (promoted != EventType::Unknown) {
                                r.source.eventType = promoted;
                                r.source.confidence = std::max(r.source.confidence, trace.confidence);
                            }
                        }
                    }
                }
            }

            if (r.source.eventType == EventType::Unknown &&
                semHint.matched &&
                IsForegroundSemantic(semHint.meta.group, cfg.allowUnknownFootstep) &&
                semHint.meta.confidence >= 0.52f) {
                const auto semPromoted = SemanticToEventType(semHint.meta.group);
                if (semPromoted != EventType::Unknown) {
                    r.source.eventType = semPromoted;
                    r.source.confidence = std::max(r.source.confidence, semHint.meta.confidence);
                    unknownSemanticHintStrong = true;
                }
            }

            if (r.source.sourceFormId != 0 && r.source.eventType != EventType::Unknown) {
                observeSessionPromoter(r.source.sourceFormId, r.source.eventType, r.source.confidence);
            }
            if (r.source.eventType == EventType::Unknown && r.source.sourceFormId != 0) {
                if (tryPromoteFromSession(r.source.sourceFormId)) {
                    unknownSemanticHintStrong = true;
                }
            }

            const bool combatTraceHint =
                normalized.trace.has_value() &&
                IsStructuredCombatEvent(normalized.trace->preferredEvent);
            const bool combatSemanticHint =
                semHint.matched &&
                IsStructuredCombatEvent(SemanticToEventType(semHint.meta.group));
            const bool combatLikeEvent =
                IsStructuredCombatEvent(r.source.eventType) ||
                combatTraceHint ||
                combatSemanticHint;

            const auto gate = HapticEligibilityEngine::GetSingleton().Evaluate(
                r.source,
                gateCtx,
                nowUs);

            if (!gate.accepted) {
                r.source = gate.adjustedSource;
                r.reason = ToDecisionReason(gate.reason);
                _gateRejected.fetch_add(1, std::memory_order_relaxed);
                switch (gate.reason) {
                case GateRejectReason::UnknownBlocked:
                    _rejectUnknownBlocked.fetch_add(1, std::memory_order_relaxed);
                    break;
                case GateRejectReason::BackgroundBlocked:
                    _rejectBackgroundBlocked.fetch_add(1, std::memory_order_relaxed);
                    break;
                case GateRejectReason::NoTraceContext:
                    _rejectNoTraceContext.fetch_add(1, std::memory_order_relaxed);
                    break;
                case GateRejectReason::LowSemanticConfidence:
                    _rejectLowSemanticConfidence.fetch_add(1, std::memory_order_relaxed);
                    break;
                case GateRejectReason::LowRelativeEnergy:
                    _rejectLowRelativeEnergy.fetch_add(1, std::memory_order_relaxed);
                    break;
                case GateRejectReason::RefractoryBlocked:
                    _rejectRefractoryBlocked.fetch_add(1, std::memory_order_relaxed);
                    break;
                default:
                    break;
                }
                if (combatLikeEvent &&
                    ShouldEmitWindowedProbe(
                        s_attackDecisionProbeWindowUs,
                        s_attackDecisionProbeLines,
                        nowUs,
                        12)) {
                    logger::info(
                        "[Haptics][ProbeAttack] site=attack/decision_reject evt={} reason={} match={:.2f} conf={:.2f} form=0x{:08X} voice=0x{:X} bind={} trace={} semHint={} rel={:.2f} motor={:.2f}/{:.2f}",
                        ToString(r.source.eventType),
                        DecisionReasonLabel(r.reason),
                        r.matchScore,
                        r.source.confidence,
                        r.source.sourceFormId,
                        voicePtr,
                        binding.has_value() ? 1 : 0,
                        normalized.trace.has_value() ? 1 : 0,
                        combatSemanticHint ? 1 : 0,
                        r.source.relativeEnergy,
                        r.source.left,
                        r.source.right);
                }
                continue;
            }

            _gateAccepted.fetch_add(1, std::memory_order_relaxed);
            r.source = gate.adjustedSource;

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
                    r.source.flags = static_cast<std::uint8_t>(r.source.flags | HapticSourceFlagL1Trace);
                    if (r.source.eventType == EventType::Unknown) {
                        r.source.eventType = SemanticToEventType(sem.meta.group);
                    }

                    _l1Count.fetch_add(1, std::memory_order_relaxed);
                    _l1FormSemanticHit.fetch_add(1, std::memory_order_relaxed);
                    if (enableShadowProbe) {
                        (void)dynamicPool.ShadowCanResolve(r.source);
                    }
                    if (r.source.eventType != EventType::Unknown || unknownSemanticHintStrong) {
                        dynamicPool.ObserveL1(r.source, r.matchScore);
                    }
                    ++batchL1Hits;

                    ApplyEventProfile(r.source, cfg);
                    if (combatLikeEvent &&
                        ShouldEmitWindowedProbe(
                            s_attackDecisionProbeWindowUs,
                            s_attackDecisionProbeLines,
                            nowUs,
                            12)) {
                        logger::info(
                            "[Haptics][ProbeAttack] site=attack/decision_accept evt={} layer={} reason={} match={:.2f} conf={:.2f} form=0x{:08X} voice=0x{:X} bind={} trace={} rel={:.2f} ttl={}ms motor={:.2f}/{:.2f}",
                            ToString(r.source.eventType),
                            DecisionLayerLabel(r.layer),
                            DecisionReasonLabel(r.reason),
                            r.matchScore,
                            r.source.confidence,
                            r.source.sourceFormId,
                            voicePtr,
                            binding.has_value() ? 1 : 0,
                            normalized.trace.has_value() ? 1 : 0,
                            r.source.relativeEnergy,
                            r.source.ttlMs,
                            r.source.left,
                            r.source.right);
                    }
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

            ApplyEventProfile(r.source, cfg);

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
                    bool allowL1Trace = true;
                    if (r.source.eventType == EventType::Unknown) {
                        const float inputLevel = std::max(r.source.left, r.source.right);
                        const float relThreshold = std::max(1.20f, cfg.relativeEnergyRatioThreshold);
                        const bool traceBackedUnknown =
                            gateCtx.hasTrace &&
                            r.source.sourceFormId != 0 &&
                            r.matchScore >= 0.50f;
                        const bool energeticUnknown =
                            (r.matchScore >= 0.66f) ||
                            ((r.source.relativeEnergy >= relThreshold) &&
                                (r.source.confidence >= 0.52f) &&
                                (inputLevel >= 0.12f));
                        allowL1Trace = unknownSemanticHintStrong || traceBackedUnknown || energeticUnknown;
                    }

                    if (ageUs <= ttlUs && allowL1Trace) {
                        r.traceHit = true;
                        r.layer = DecisionLayer::L1Trace;
                        r.reason = DecisionReason::L1TraceHit;
                        r.source.flags = static_cast<std::uint8_t>(r.source.flags | HapticSourceFlagL1Trace);

                        _l1Count.fetch_add(1, std::memory_order_relaxed);
                        _traceBindHit.fetch_add(1, std::memory_order_relaxed);
                        if (enableShadowProbe) {
                            (void)dynamicPool.ShadowCanResolve(r.source);
                        }
                        if (r.source.eventType != EventType::Unknown || unknownSemanticHintStrong) {
                            dynamicPool.ObserveL1(r.source, r.matchScore);
                        }
                        ++batchL1Hits;

                        ApplyEventProfile(r.source, cfg);
                        if (combatLikeEvent &&
                            ShouldEmitWindowedProbe(
                                s_attackDecisionProbeWindowUs,
                                s_attackDecisionProbeLines,
                                nowUs,
                                12)) {
                            logger::info(
                                "[Haptics][ProbeAttack] site=attack/decision_accept evt={} layer={} reason={} match={:.2f} conf={:.2f} form=0x{:08X} voice=0x{:X} bind={} trace={} rel={:.2f} ttl={}ms motor={:.2f}/{:.2f}",
                                ToString(r.source.eventType),
                                DecisionLayerLabel(r.layer),
                                DecisionReasonLabel(r.reason),
                                r.matchScore,
                                r.source.confidence,
                                r.source.sourceFormId,
                                voicePtr,
                                binding.has_value() ? 1 : 0,
                                normalized.trace.has_value() ? 1 : 0,
                                r.source.relativeEnergy,
                                r.source.ttlMs,
                                r.source.left,
                                r.source.right);
                        }
                        out.push_back(r);
                        continue;
                    }
                    else if (ageUs <= ttlUs) {
                        r.reason = DecisionReason::L1TraceMiss;
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
            const bool isUnknownEvent = (r.source.eventType == EventType::Unknown);
            const bool semanticHintStrong = isUnknownEvent && unknownSemanticHintStrong;
            const bool traceBackedUnknown = isUnknownEvent &&
                gateCtx.hasTrace &&
                r.source.sourceFormId != 0;
            const bool hasUnknownSemanticContext = isUnknownEvent &&
                (semanticHintStrong || unknownTraceSemanticContext || traceBackedUnknown);
            const float kHighThreshold = isUnknownEvent ?
                (hasUnknownSemanticContext ? 0.68f : 0.84f) :
                kHigh;
            const float kMidThreshold = isUnknownEvent ?
                (hasUnknownSemanticContext ? 0.50f : 0.76f) :
                kMid;

            if (r.matchScore >= kHighThreshold) {
                r.reason = DecisionReason::L2HighScore;
                _l2Count.fetch_add(1, std::memory_order_relaxed);
                _l2HighScore.fetch_add(1, std::memory_order_relaxed);
                if (enableShadowProbe) {
                    (void)dynamicPool.ShadowCanResolve(r.source);
                }
                tryLearnFromL2(r);
            }
            else if (r.matchScore >= kMidThreshold) {
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
                    const bool allowPoolResolve =
                        !isUnknownEvent || hasUnknownSemanticContext;
                    if (allowPoolResolve && dynamicPool.TryResolve(r.source, poolResolved)) {
                        r.reason = DecisionReason::L3DynamicPoolHit;
                        r.source = poolResolved;
                        _dynamicPoolHit.fetch_add(1, std::memory_order_relaxed);
                    }
                    else {
                        r.reason = DecisionReason::L3LowScoreFallback;
                        if (isUnknownEvent) {
                            const float relThreshold = std::max(1.20f, cfg.relativeEnergyRatioThreshold);
                            const bool structuredUnknown = hasUnknownSemanticContext;
                            const float inputLevel = std::max(r.source.left, r.source.right);
                            const bool energeticUnknown = structuredUnknown ?
                                ((r.source.relativeEnergy >= relThreshold) ||
                                    ((r.source.confidence >= 0.44f) && (inputLevel >= 0.08f))) :
                                ((r.source.relativeEnergy >= relThreshold) &&
                                    (r.source.confidence >= 0.58f) &&
                                    (inputLevel >= 0.18f));

                            if (!structuredUnknown) {
                                if (!energeticUnknown) {
                                    // Drop weak and unstructured unknown directly to avoid noisy lingering.
                                    _l3DroppedUnstructuredWeakUnknown.fetch_add(1, std::memory_order_relaxed);
                                    _dynamicPoolMiss.fetch_add(1, std::memory_order_relaxed);
                                    continue;
                                }

                                // Unstructured but energetic unknown: allow only very tiny/short fallback.
                                r.source.confidence = std::min(r.source.confidence, 0.34f);
                                r.source.left *= 0.34f;
                                r.source.right *= 0.34f;
                                r.source.ttlMs = std::clamp(std::max(r.source.ttlMs, 40u), 28u, 80u);
                            }
                            else if (energeticUnknown) {
                                r.source.confidence = std::min(r.source.confidence, 0.54f);
                                r.source.left *= 0.72f;
                                r.source.right *= 0.72f;
                                r.source.ttlMs = std::clamp(std::max(r.source.ttlMs, 58u), 40u, 140u);
                            }
                            else {
                                // Structured weak unknown: keep fallback but softer and shorter.
                                r.source.confidence = std::min(r.source.confidence, 0.44f);
                                r.source.left *= 0.58f;
                                r.source.right *= 0.58f;
                                r.source.ttlMs = std::clamp(std::max(r.source.ttlMs, 48u), 34u, 110u);
                            }
                        }
                        else {
                            r.source.confidence = std::min(r.source.confidence, 0.72f);
                            r.source.left *= 0.88f;
                            r.source.right *= 0.88f;
                            r.source.ttlMs = std::max(r.source.ttlMs, GetDefaultTtlForEvent(r.source.eventType));
                        }
                        _lowScoreFallback.fetch_add(1, std::memory_order_relaxed);
                        _dynamicPoolMiss.fetch_add(1, std::memory_order_relaxed);
                    }

                    ApplyEventProfile(r.source, cfg);
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

            if (combatLikeEvent &&
                ShouldEmitWindowedProbe(
                    s_attackDecisionProbeWindowUs,
                    s_attackDecisionProbeLines,
                    nowUs,
                    12)) {
                logger::info(
                    "[Haptics][ProbeAttack] site=attack/decision_accept evt={} layer={} reason={} match={:.2f} conf={:.2f} form=0x{:08X} voice=0x{:X} bind={} trace={} rel={:.2f} ttl={}ms motor={:.2f}/{:.2f}",
                    ToString(r.source.eventType),
                    DecisionLayerLabel(r.layer),
                    DecisionReasonLabel(r.reason),
                    r.matchScore,
                    r.source.confidence,
                    r.source.sourceFormId,
                    voicePtr,
                    binding.has_value() ? 1 : 0,
                    normalized.trace.has_value() ? 1 : 0,
                    r.source.relativeEnergy,
                    r.source.ttlMs,
                    r.source.left,
                    r.source.right);
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
        s.l3DroppedUnstructuredWeakUnknown = _l3DroppedUnstructuredWeakUnknown.load(std::memory_order_relaxed);
        s.gateAccepted = _gateAccepted.load(std::memory_order_relaxed);
        s.gateRejected = _gateRejected.load(std::memory_order_relaxed);
        s.rejectUnknownBlocked = _rejectUnknownBlocked.load(std::memory_order_relaxed);
        s.rejectBackgroundBlocked = _rejectBackgroundBlocked.load(std::memory_order_relaxed);
        s.rejectNoTraceContext = _rejectNoTraceContext.load(std::memory_order_relaxed);
        s.rejectLowSemanticConfidence = _rejectLowSemanticConfidence.load(std::memory_order_relaxed);
        s.rejectLowRelativeEnergy = _rejectLowRelativeEnergy.load(std::memory_order_relaxed);
        s.rejectRefractoryBlocked = _rejectRefractoryBlocked.load(std::memory_order_relaxed);
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
        _l3DroppedUnstructuredWeakUnknown.store(0, std::memory_order_relaxed);
        _gateAccepted.store(0, std::memory_order_relaxed);
        _gateRejected.store(0, std::memory_order_relaxed);
        _rejectUnknownBlocked.store(0, std::memory_order_relaxed);
        _rejectBackgroundBlocked.store(0, std::memory_order_relaxed);
        _rejectNoTraceContext.store(0, std::memory_order_relaxed);
        _rejectLowSemanticConfidence.store(0, std::memory_order_relaxed);
        _rejectLowRelativeEnergy.store(0, std::memory_order_relaxed);
        _rejectRefractoryBlocked.store(0, std::memory_order_relaxed);
    }
}
