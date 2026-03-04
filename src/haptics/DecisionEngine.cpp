#include "pch.h"
#include "haptics/DecisionEngine.h"

#include "haptics/AudioOnlyScorer.h"
#include "haptics/FormSemanticCache.h"
#include "haptics/HapticsConfig.h"
#include "haptics/InstanceTraceCache.h"
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
        const float formSemanticThreshold = std::clamp(cfg.l1FormSemanticMinConfidence, 0.0f, 1.0f);

        std::size_t batchL1Hits = 0;

        for (auto& s : l2) {
            DecisionResult r{};
            r.source = s;
            r.matchScore = std::clamp(s.confidence, 0.0f, 1.0f);
            r.traceHit = false;

            const auto voicePtr = static_cast<std::uintptr_t>(s.sourceVoiceId);
            std::optional<VoiceBinding> binding;
            if (voicePtr != 0) {
                binding = VoiceBindingMap::GetSingleton().TryGet(voicePtr);
            }

            if (enableFormSemantic) {
                std::uint32_t semanticFormID = s.sourceFormId;
                if (semanticFormID == 0 && binding.has_value()) {
                    auto trace = InstanceTraceCache::GetSingleton().TryGet(binding->instanceId);
                    if (trace.has_value()) {
                        semanticFormID = (trace->sourceFormId != 0) ? trace->sourceFormId : trace->soundFormId;
                    }
                }

                if (semanticFormID != 0) {
                    FormSemanticMeta meta{};
                    if (FormSemanticCache::GetSingleton().TryGet(semanticFormID, meta) &&
                        meta.confidence >= formSemanticThreshold) {
                        r.traceHit = true;
                        r.layer = DecisionLayer::L1Trace;
                        r.reason = DecisionReason::L1FormSemantic;
                        r.matchScore = std::max(r.matchScore, std::clamp(meta.confidence, 0.0f, 1.0f));
                        if (r.source.eventType == EventType::Unknown) {
                            r.source.eventType = SemanticToEventType(meta.group);
                        }

                        _l1Count.fetch_add(1, std::memory_order_relaxed);
                        _l1FormSemanticHit.fetch_add(1, std::memory_order_relaxed);
                        ++batchL1Hits;

                        out.push_back(r);
                        continue;
                    }

                    _l1FormSemanticMiss.fetch_add(1, std::memory_order_relaxed);
                }
            }

            // L1 voice binding
            if (voicePtr != 0) {
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
            }
            else if (r.matchScore >= kMid) {
                r.reason = DecisionReason::L2MidScore;
                _l2Count.fetch_add(1, std::memory_order_relaxed);
            }
            else {
                if (cfg.fallbackBaseWhenNoMatch) {
                    r.layer = DecisionLayer::L3Fallback;
                    r.reason = DecisionReason::L3LowScoreFallback;

                    r.source.confidence = std::min(r.source.confidence, 0.55f);
                    r.source.left *= 0.75f;
                    r.source.right *= 0.75f;

                    _l3Count.fetch_add(1, std::memory_order_relaxed);
                    _lowScoreFallback.fetch_add(1, std::memory_order_relaxed);
                }
                else {
                    r.reason = DecisionReason::L2LowScorePass;
                    _l2Count.fetch_add(1, std::memory_order_relaxed);
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
        s.noCandidate = _noCandidate.load(std::memory_order_relaxed);
        s.lowScoreFallback = _lowScoreFallback.load(std::memory_order_relaxed);
        s.traceBindHit = _traceBindHit.load(std::memory_order_relaxed);
        s.tickNoAudio = _tickNoAudio.load(std::memory_order_relaxed);
        s.audioPresentNoMatch = _audioPresentNoMatch.load(std::memory_order_relaxed);
        s.traceBindMissUnbound = _traceBindMissUnbound.load(std::memory_order_relaxed);
        s.traceBindMissExpired = _traceBindMissExpired.load(std::memory_order_relaxed);
        s.l1FormSemanticHit = _l1FormSemanticHit.load(std::memory_order_relaxed);
        s.l1FormSemanticMiss = _l1FormSemanticMiss.load(std::memory_order_relaxed);
        return s;
    }

    void DecisionEngine::ResetStats()
    {
        _l1Count.store(0, std::memory_order_relaxed);
        _l2Count.store(0, std::memory_order_relaxed);
        _l3Count.store(0, std::memory_order_relaxed);
        _noCandidate.store(0, std::memory_order_relaxed);
        _lowScoreFallback.store(0, std::memory_order_relaxed);
        _traceBindHit.store(0, std::memory_order_relaxed);
        _tickNoAudio.store(0, std::memory_order_relaxed);
        _audioPresentNoMatch.store(0, std::memory_order_relaxed);
        _traceBindMissUnbound.store(0, std::memory_order_relaxed);
        _traceBindMissExpired.store(0, std::memory_order_relaxed);
        _l1FormSemanticHit.store(0, std::memory_order_relaxed);
        _l1FormSemanticMiss.store(0, std::memory_order_relaxed);
    }
}
