#include "pch.h"
#include "haptics/HapticEligibilityEngine.h"

#include "haptics/HapticsConfig.h"
#include "haptics/SemanticResolver.h"

#include <algorithm>
#include <cmath>

namespace dualpad::haptics
{
    namespace
    {
        std::uint64_t Mix64(std::uint64_t x)
        {
            x += 0x9E3779B97F4A7C15ull;
            x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
            x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
            return x ^ (x >> 31);
        }

        bool IsTracePromotionAllowed(const GateContext& ctx, const HapticsConfig& cfg)
        {
            return cfg.enableUnknownPromotion &&
                ctx.hasTrace &&
                ctx.tracePreferredEvent != EventType::Unknown &&
                ctx.traceConfidence >= std::clamp(cfg.unknownPromotionMinConfidence, 0.0f, 1.0f);
        }

        float GetUnknownSemanticPromotionThreshold(
            const HapticsConfig& cfg,
            const GateContext& ctx,
            SemanticGroup group,
            bool strongSemantic)
        {
            float threshold = std::clamp(cfg.unknownSemanticMinConfidence, 0.0f, 1.0f);
            if (!strongSemantic) {
                return threshold;
            }

            // Combat-like semantics should not be held back by overly strict generic thresholds.
            threshold = std::min(threshold, 0.56f);
            if (ctx.hasTrace && ctx.tracePreferredEvent != EventType::Unknown) {
                threshold = std::min(threshold, 0.52f);
            }

            if (group == SemanticGroup::Footstep && !cfg.allowUnknownFootstep) {
                return 1.0f;
            }

            return threshold;
        }

        bool IsBackgroundSemantic(SemanticGroup group)
        {
            return group == SemanticGroup::Ambient ||
                group == SemanticGroup::Music ||
                group == SemanticGroup::UI;
        }
    }

    HapticEligibilityEngine& HapticEligibilityEngine::GetSingleton()
    {
        static HapticEligibilityEngine s;
        return s;
    }

    GateResult HapticEligibilityEngine::Evaluate(
        const HapticSourceMsg& in,
        const GateContext& ctx,
        std::uint64_t nowUs)
    {
        const auto& cfg = HapticsConfig::GetSingleton();
        HapticSourceMsg adjusted = in;

        if (adjusted.type == SourceType::AudioMod) {
            if (adjusted.relativeEnergy >= 1.0f &&
                adjusted.relativeEnergy < std::max(1.0f, cfg.relativeEnergyRatioThreshold)) {
                const float inputLevel = std::max(adjusted.left, adjusted.right);
                const bool hasStructuredContext =
                    adjusted.sourceFormId != 0 ||
                    ctx.hasTrace ||
                    ctx.hasBinding;
                if (!hasStructuredContext || inputLevel < 0.12f) {
                    return Reject(adjusted, GateRejectReason::LowRelativeEnergy);
                }
            }

            if (adjusted.eventType == EventType::Unknown) {
                if (IsTracePromotionAllowed(ctx, cfg)) {
                    adjusted.eventType = ctx.tracePreferredEvent;
                }
            }

            if (adjusted.eventType == EventType::Unknown &&
                cfg.enableUnknownSemanticGate &&
                adjusted.sourceFormId != 0) {
                const auto sem = SemanticResolver::GetSingleton().Resolve(
                    adjusted.sourceFormId,
                    0.0f);

                if (sem.matched) {
                    const float inputLevel = std::max(adjusted.left, adjusted.right);
                    const float unknownSemanticMin =
                        std::clamp(cfg.unknownSemanticMinConfidence, 0.0f, 1.0f);
                    const float energeticRelThreshold =
                        std::max(1.25f, cfg.relativeEnergyRatioThreshold + 0.10f);
                    const bool energeticTraceBypass =
                        ctx.hasTrace &&
                        adjusted.relativeEnergy >= energeticRelThreshold &&
                        adjusted.confidence >= 0.60f &&
                        inputLevel >= 0.18f;

                    // Unknown semantic with a form key but no meaningful semantic group is a
                    // major source of idle/noisy rumble. Reject unless an energetic trace-backed
                    // burst indicates this may be a valid foreground transient.
                    if (sem.meta.group == SemanticGroup::Unknown) {
                        // Guardrail for unresolved semantic: allow structured/trace-backed unknown
                        // to pass at conservative strength, even if user config is overly strict.
                        const bool traceBackedStructured =
                            ctx.hasTrace &&
                            adjusted.sourceFormId != 0;
                        const float unresolvedSemanticThreshold = std::min(
                            unknownSemanticMin,
                            traceBackedStructured ? 0.50f : 0.56f);
                        const float unresolvedInputFloor = std::clamp(
                            cfg.unknownMinInputLevel,
                            traceBackedStructured ? 0.05f : 0.08f,
                            traceBackedStructured ? 0.12f : 0.16f);
                        const bool relativeBurst =
                            adjusted.relativeEnergy >= std::max(1.05f, cfg.relativeEnergyRatioThreshold - 0.05f) &&
                            adjusted.confidence >= 0.46f;

                        if (!energeticTraceBypass) {
                            if (sem.meta.confidence < unresolvedSemanticThreshold && !relativeBurst) {
                                return Reject(adjusted, GateRejectReason::LowSemanticConfidence);
                            }
                            if (inputLevel < unresolvedInputFloor && !relativeBurst) {
                                return Reject(adjusted, GateRejectReason::LowSemanticConfidence);
                            }
                        }

                        if (adjusted.eventType == EventType::Unknown) {
                            const bool l1Backed = (adjusted.flags & HapticSourceFlagL1Trace) != 0;
                            const float ampCap = l1Backed ? 0.15f : 0.10f;
                            const float confCap = l1Backed ? 0.46f : 0.34f;
                            const std::uint32_t ttlMax = l1Backed ? 64u : 48u;
                            adjusted.left = std::min(adjusted.left, ampCap);
                            adjusted.right = std::min(adjusted.right, ampCap);
                            adjusted.confidence = std::min(adjusted.confidence, confCap);
                            adjusted.priority = std::min(adjusted.priority, std::max(10, cfg.priorityFootstep));
                            adjusted.ttlMs = std::clamp(adjusted.ttlMs, 22u, ttlMax);
                        }
                    }

                    if (IsBackgroundSemantic(sem.meta.group) && !cfg.allowBackgroundEvent) {
                        return Reject(adjusted, GateRejectReason::BackgroundBlocked);
                    }
                    if (sem.meta.group == SemanticGroup::Footstep && !cfg.allowUnknownFootstep) {
                        return Reject(adjusted, GateRejectReason::UnknownBlocked);
                    }

                    const bool strongSemantic = IsStrongSemanticForUnknown(sem.meta.group, cfg.allowUnknownFootstep);
                    const float promoteThreshold = GetUnknownSemanticPromotionThreshold(
                        cfg,
                        ctx,
                        sem.meta.group,
                        strongSemantic);
                    if (strongSemantic && sem.meta.confidence >= promoteThreshold) {
                        adjusted.eventType = SemanticToEventType(sem.meta.group);
                        adjusted.confidence = std::max(
                            adjusted.confidence,
                            std::clamp(sem.meta.confidence, 0.0f, 1.0f));
                    }
                }
            }

            if (adjusted.eventType == EventType::Unknown && !ctx.hasTrace && adjusted.sourceFormId == 0) {
                return Reject(adjusted, GateRejectReason::NoTraceContext);
            }

            if (adjusted.eventType == EventType::Unknown && !cfg.allowUnknownAudioEvent) {
                const bool hasStructuredContext =
                    adjusted.sourceFormId != 0 ||
                    ctx.hasTrace ||
                    ctx.hasBinding;
                if (!hasStructuredContext) {
                    return Reject(adjusted, GateRejectReason::UnknownBlocked);
                }

                // Permit structured unknown at conservative amplitude/confidence to avoid complete silence.
                adjusted.left = std::min(adjusted.left, 0.20f);
                adjusted.right = std::min(adjusted.right, 0.20f);
                adjusted.confidence = std::min(adjusted.confidence, 0.46f);
                adjusted.priority = std::min(adjusted.priority, std::max(10, cfg.priorityFootstep));
            }

            if (IsBackgroundEvent(adjusted.eventType) && !cfg.allowBackgroundEvent) {
                return Reject(adjusted, GateRejectReason::BackgroundBlocked);
            }

            if (IsRefractoryBlocked(adjusted, nowUs)) {
                return Reject(adjusted, GateRejectReason::RefractoryBlocked);
            }
        }

        _accepted.fetch_add(1, std::memory_order_relaxed);

        GateResult result{};
        result.accepted = true;
        result.reason = GateRejectReason::None;
        result.adjustedSource = adjusted;
        return result;
    }

    HapticEligibilityEngine::Stats HapticEligibilityEngine::GetStats() const
    {
        Stats s{};
        s.accepted = _accepted.load(std::memory_order_relaxed);
        s.rejected = _rejected.load(std::memory_order_relaxed);
        s.rejectUnknownBlocked = _rejectUnknownBlocked.load(std::memory_order_relaxed);
        s.rejectBackgroundBlocked = _rejectBackgroundBlocked.load(std::memory_order_relaxed);
        s.rejectNoTraceContext = _rejectNoTraceContext.load(std::memory_order_relaxed);
        s.rejectLowSemanticConfidence = _rejectLowSemanticConfidence.load(std::memory_order_relaxed);
        s.rejectLowRelativeEnergy = _rejectLowRelativeEnergy.load(std::memory_order_relaxed);
        s.rejectRefractoryBlocked = _rejectRefractoryBlocked.load(std::memory_order_relaxed);
        s.refractoryWindowHit = _refractoryWindowHit.load(std::memory_order_relaxed);
        s.refractorySoftSuppressed = _refractorySoftSuppressed.load(std::memory_order_relaxed);
        s.refractoryHardDropped = _refractoryHardDropped.load(std::memory_order_relaxed);
        return s;
    }

    void HapticEligibilityEngine::ResetStats()
    {
        _accepted.store(0, std::memory_order_relaxed);
        _rejected.store(0, std::memory_order_relaxed);
        _rejectUnknownBlocked.store(0, std::memory_order_relaxed);
        _rejectBackgroundBlocked.store(0, std::memory_order_relaxed);
        _rejectNoTraceContext.store(0, std::memory_order_relaxed);
        _rejectLowSemanticConfidence.store(0, std::memory_order_relaxed);
        _rejectLowRelativeEnergy.store(0, std::memory_order_relaxed);
        _rejectRefractoryBlocked.store(0, std::memory_order_relaxed);
        _refractoryWindowHit.store(0, std::memory_order_relaxed);
        _refractorySoftSuppressed.store(0, std::memory_order_relaxed);
        _refractoryHardDropped.store(0, std::memory_order_relaxed);

        {
            std::scoped_lock lk(_refractoryMx);
            _refractoryLastAcceptedUs.clear();
            _lastRefractoryCleanupUs = 0;
        }
    }

    bool HapticEligibilityEngine::IsBackgroundEvent(EventType type)
    {
        return type == EventType::Ambient || type == EventType::Music || type == EventType::UI;
    }

    EventType HapticEligibilityEngine::SemanticToEventType(SemanticGroup group)
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

    bool HapticEligibilityEngine::IsStrongSemanticForUnknown(SemanticGroup group, bool allowFootstep)
    {
        switch (group) {
        case SemanticGroup::WeaponSwing:
        case SemanticGroup::Hit:
        case SemanticGroup::Block:
        case SemanticGroup::Bow:
        case SemanticGroup::Voice:
            return true;
        case SemanticGroup::Footstep:
            return allowFootstep;
        default:
            return false;
        }
    }

    std::uint8_t HapticEligibilityEngine::ClassifyRefractoryFamily(EventType type)
    {
        switch (type) {
        case EventType::HitImpact:
        case EventType::Block:
            return 1;  // Hit family
        case EventType::WeaponSwing:
        case EventType::SpellCast:
        case EventType::SpellImpact:
        case EventType::BowRelease:
            return 2;  // Swing family
        case EventType::Footstep:
        case EventType::Jump:
        case EventType::Land:
            return 3;  // Footstep family
        default:
            return 0;
        }
    }

    GateResult HapticEligibilityEngine::Reject(const HapticSourceMsg& in, GateRejectReason reason)
    {
        _rejected.fetch_add(1, std::memory_order_relaxed);
        switch (reason) {
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

        GateResult result{};
        result.accepted = false;
        result.reason = reason;
        result.adjustedSource = in;
        return result;
    }

    std::uint64_t HapticEligibilityEngine::BuildRefractoryKey(std::uint8_t family, const HapticSourceMsg& src) const
    {
        // Family in high bits avoids cross-family suppression.
        std::uint64_t key = static_cast<std::uint64_t>(family) << 60;

        if (src.sourceFormId != 0) {
            // Stable across voice recycling; best key when available.
            key ^= Mix64(static_cast<std::uint64_t>(src.sourceFormId));
        } else if (src.sourceVoiceId != 0) {
            // Fallback when no form context.
            key ^= Mix64(src.sourceVoiceId);
        } else {
            // Last resort: preserve previous global behavior for no-key traffic.
            key ^= 0xA5A5A5A5A5A5A5A5ull;
        }

        return key;
    }

    void HapticEligibilityEngine::CleanupRefractoryMapLocked(std::uint64_t nowUs)
    {
        if (_refractoryLastAcceptedUs.empty()) {
            _lastRefractoryCleanupUs = nowUs;
            return;
        }

        if (_lastRefractoryCleanupUs != 0 &&
            (nowUs - _lastRefractoryCleanupUs) < kRefractoryCleanupIntervalUs) {
            return;
        }
        _lastRefractoryCleanupUs = nowUs;

        for (auto it = _refractoryLastAcceptedUs.begin(); it != _refractoryLastAcceptedUs.end();) {
            const auto lastUs = it->second;
            if (nowUs >= lastUs && (nowUs - lastUs) > kRefractoryStaleUs) {
                it = _refractoryLastAcceptedUs.erase(it);
            } else {
                ++it;
            }
        }
    }

    bool HapticEligibilityEngine::IsRefractoryBlocked(HapticSourceMsg& src, std::uint64_t nowUs)
    {
        const auto& cfg = HapticsConfig::GetSingleton();
        const auto family = ClassifyRefractoryFamily(src.eventType);
        if (family == 0) {
            return false;
        }

        std::uint32_t refractoryMs = 0;
        switch (family) {
        case 1:
            refractoryMs = cfg.refractoryHitMs;
            break;
        case 2:
            refractoryMs = cfg.refractorySwingMs;
            break;
        case 3:
            refractoryMs = cfg.refractoryFootstepMs;
            break;
        default:
            return false;
        }

        if (refractoryMs == 0) {
            return false;
        }

        const auto key = BuildRefractoryKey(family, src);
        const auto windowUs = static_cast<std::uint64_t>(refractoryMs) * 1000ull;

        std::scoped_lock lk(_refractoryMx);
        CleanupRefractoryMapLocked(nowUs);

        auto& lastAcceptedUs = _refractoryLastAcceptedUs[key];
        if (lastAcceptedUs != 0 && nowUs >= lastAcceptedUs) {
            const auto elapsedUs = nowUs - lastAcceptedUs;
            if (elapsedUs < windowUs) {
                _refractoryWindowHit.fetch_add(1, std::memory_order_relaxed);
                // Soft-suppress repeated bursts from the same key instead of hard-drop.
                const float t = std::clamp(
                    static_cast<float>(elapsedUs) / static_cast<float>(windowUs),
                    0.0f,
                    1.0f);
                const float gain = 0.24f + 0.76f * t;

                const float prePeak = std::max(src.left, src.right);
                src.left *= gain;
                src.right *= gain;
                src.confidence = std::min(src.confidence, 0.40f + 0.55f * t);
                src.ttlMs = std::clamp(
                    static_cast<std::uint32_t>(std::lround(18.0f + t * 36.0f)),
                    14u,
                    60u);
                src.priority = std::max(8, static_cast<int>(std::lround(static_cast<float>(src.priority) * (0.72f + 0.28f * t))));

                // Extremely dense and weak repeats are still dropped to avoid idle buzzing.
                const auto hardDropUs = std::max<std::uint64_t>(2'500ull, windowUs / 5ull);
                if (elapsedUs < hardDropUs &&
                    prePeak < 0.13f &&
                    src.confidence < 0.56f) {
                    _refractoryHardDropped.fetch_add(1, std::memory_order_relaxed);
                    return true;
                }
                _refractorySoftSuppressed.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
        }

        lastAcceptedUs = nowUs;
        return false;
    }
}
