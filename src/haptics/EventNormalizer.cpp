#include "pch.h"
#include "haptics/EventNormalizer.h"

#include "haptics/FormSemanticCache.h"
#include "haptics/HapticsConfig.h"

#include <array>
#include <vector>

namespace dualpad::haptics
{
    namespace
    {
        constexpr std::size_t kUnknownFormMapMax = 1024;

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

        bool IsBackgroundSemantic(SemanticGroup group)
        {
            return group == SemanticGroup::Ambient ||
                group == SemanticGroup::Music ||
                group == SemanticGroup::UI;
        }

        bool IsForegroundPromotableSemantic(SemanticGroup group, bool allowUnknownFootstep)
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

        bool IsStrongForegroundPromotableSemantic(SemanticGroup group)
        {
            switch (group) {
            case SemanticGroup::WeaponSwing:
            case SemanticGroup::Hit:
            case SemanticGroup::Block:
            case SemanticGroup::Bow:
            case SemanticGroup::Voice:
                return true;
            default:
                return false;
            }
        }

        bool TryPromoteStrongForegroundEvent(
            const FormSemanticMeta& meta,
            const HapticSourceMsg& in,
            const HapticsConfig& cfg,
            HapticSourceMsg& ioOut)
        {
            if (!IsStrongForegroundPromotableSemantic(meta.group)) {
                return false;
            }

            const auto promoted = SemanticToEventType(meta.group);
            if (promoted == EventType::Unknown) {
                return false;
            }

            const float conf = std::clamp(meta.confidence, 0.0f, 1.0f);
            const float inputLevel = std::max(in.left, in.right);
            const float relThreshold = std::max(1.20f, cfg.relativeEnergyRatioThreshold + 0.05f);
            const bool energetic = (in.relativeEnergy >= relThreshold) && (inputLevel >= 0.08f);
            const bool confidencePass = conf >= 0.44f;
            const bool strictPass = conf >= 0.52f;
            if (!confidencePass || (!strictPass && !energetic)) {
                return false;
            }

            ioOut.eventType = promoted;
            ioOut.confidence = std::max(ioOut.confidence, conf);
            return true;
        }
    }

    EventNormalizer& EventNormalizer::GetSingleton()
    {
        static EventNormalizer s;
        return s;
    }

    NormalizedSource EventNormalizer::Normalize(const HapticSourceMsg& in, std::uint64_t nowUs)
    {
        (void)nowUs;

        _inputs.fetch_add(1, std::memory_order_relaxed);

        NormalizedSource out{};
        out.source = in;

        const auto voicePtr = static_cast<std::uintptr_t>(in.sourceVoiceId);
        if (voicePtr == 0) {
            out.missReason = NormalizeMissReason::NoVoiceID;
            _noVoiceID.fetch_add(1, std::memory_order_relaxed);
            return out;
        }

        out.binding = VoiceBindingMap::GetSingleton().TryGet(voicePtr);
        if (!out.binding.has_value()) {
            out.missReason = NormalizeMissReason::VoiceBindingMiss;
            _bindingMiss.fetch_add(1, std::memory_order_relaxed);
            return out;
        }

        _bindingHit.fetch_add(1, std::memory_order_relaxed);

        out.trace = InstanceTraceCache::GetSingleton().TryGet(out.binding->instanceId);
        if (!out.trace.has_value()) {
            out.missReason = NormalizeMissReason::TraceMetaMiss;
            _traceMiss.fetch_add(1, std::memory_order_relaxed);
            return out;
        }

        _traceHit.fetch_add(1, std::memory_order_relaxed);
        out.missReason = NormalizeMissReason::None;

        if (out.source.sourceFormId == 0) {
            const auto traceFormID = (out.trace->sourceFormId != 0) ? out.trace->sourceFormId : out.trace->soundFormId;
            if (traceFormID != 0) {
                out.source.sourceFormId = traceFormID;
                _patchedFormID.fetch_add(1, std::memory_order_relaxed);
            }
        }

        if (out.source.eventType == EventType::Unknown && out.trace->preferredEvent != EventType::Unknown) {
            out.source.eventType = out.trace->preferredEvent;
            _patchedEventType.fetch_add(1, std::memory_order_relaxed);
        }

        if (out.source.eventType == EventType::Unknown &&
            out.trace->preferredEvent == EventType::Unknown &&
            out.trace->semantic != SemanticGroup::Unknown) {
            const auto& cfg = HapticsConfig::GetSingleton();
            const bool isBg = IsBackgroundSemantic(out.trace->semantic);
            const bool canPromote =
                !(isBg && !cfg.traceAllowBackgroundEvent) &&
                IsForegroundPromotableSemantic(out.trace->semantic, cfg.allowUnknownFootstep);
            if (canPromote) {
                float promoteThreshold = std::clamp(
                    std::min(0.55f, cfg.tracePreferredEventMinConfidence),
                    0.0f,
                    1.0f);
                if (IsStrongForegroundPromotableSemantic(out.trace->semantic)) {
                    promoteThreshold = std::min(promoteThreshold, 0.48f);
                }
                else if (out.trace->semantic == SemanticGroup::Footstep) {
                    promoteThreshold = std::min(promoteThreshold, 0.52f);
                }
                if (out.trace->confidence >= promoteThreshold) {
                    const auto promoted = SemanticToEventType(out.trace->semantic);
                    if (promoted != EventType::Unknown) {
                        out.source.eventType = promoted;
                        out.source.confidence = std::max(out.source.confidence, out.trace->confidence);
                        _patchedEventType.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            }
        }

        if (out.source.eventType == EventType::Unknown && out.source.sourceFormId != 0) {
            const auto& cfg = HapticsConfig::GetSingleton();
            const float basePromoteThreshold = std::min(
                std::clamp(cfg.tracePreferredEventMinConfidence, 0.0f, 1.0f),
                0.55f);

            const std::uint32_t traceSoundFormId = out.trace.has_value() ? out.trace->soundFormId : 0u;
            const std::array<std::uint32_t, 2> candidates{
                out.source.sourceFormId,
                traceSoundFormId
            };

            for (auto formId : candidates) {
                if (formId == 0) {
                    continue;
                }

                FormSemanticMeta meta{};
                if (!FormSemanticCache::GetSingleton().TryGet(formId, meta)) {
                    continue;
                }

                const bool isBg = IsBackgroundSemantic(meta.group);
                if (isBg && !cfg.traceAllowBackgroundEvent) {
                    continue;
                }
                if (!IsForegroundPromotableSemantic(meta.group, cfg.allowUnknownFootstep)) {
                    continue;
                }
                float promoteThreshold = basePromoteThreshold;
                if (IsStrongForegroundPromotableSemantic(meta.group)) {
                    promoteThreshold = std::min(promoteThreshold, 0.48f);
                }
                else if (meta.group == SemanticGroup::Footstep) {
                    promoteThreshold = std::min(promoteThreshold, 0.52f);
                }
                if (meta.confidence < promoteThreshold) {
                    continue;
                }

                const auto promoted = SemanticToEventType(meta.group);
                if (promoted == EventType::Unknown) {
                    continue;
                }

                out.source.eventType = promoted;
                out.source.sourceFormId = formId;
                out.source.confidence = std::max(out.source.confidence, meta.confidence);
                _patchedEventType.fetch_add(1, std::memory_order_relaxed);
                break;
            }
        }

        // Conservative foreground promotion fallback:
        // keep Unknown suppression for background/footstep, but allow strong foreground
        // semantics to promote when confidence/energy is sufficient.
        if (out.source.eventType == EventType::Unknown) {
            const auto& cfg = HapticsConfig::GetSingleton();
            bool promotedConservative = false;

            if (out.trace.has_value()) {
                FormSemanticMeta traceMeta{};
                traceMeta.group = out.trace->semantic;
                traceMeta.confidence = std::clamp(out.trace->confidence, 0.0f, 1.0f);
                traceMeta.baseWeight = 0.0f;
                traceMeta.texturePresetId = 0;
                traceMeta.flags = 0;

                if (TryPromoteStrongForegroundEvent(traceMeta, out.source, cfg, out.source)) {
                    promotedConservative = true;
                }
            }

            if (!promotedConservative) {
                const std::uint32_t traceSoundFormId = out.trace.has_value() ? out.trace->soundFormId : 0u;
                const std::array<std::uint32_t, 2> candidates{
                    out.source.sourceFormId,
                    traceSoundFormId
                };
                for (auto formId : candidates) {
                    if (formId == 0) {
                        continue;
                    }

                    FormSemanticMeta meta{};
                    if (!FormSemanticCache::GetSingleton().TryGet(formId, meta)) {
                        continue;
                    }

                    if (!TryPromoteStrongForegroundEvent(meta, out.source, cfg, out.source)) {
                        continue;
                    }

                    out.source.sourceFormId = formId;
                    promotedConservative = true;
                    break;
                }
            }

            if (promotedConservative) {
                _patchedEventType.fetch_add(1, std::memory_order_relaxed);
                _patchedEventTypeConservative.fetch_add(1, std::memory_order_relaxed);
            }
        }

        if (out.source.eventType == EventType::Unknown) {
            _unknownAfterNormalize.fetch_add(1, std::memory_order_relaxed);
            std::uint32_t unknownFormID = out.source.sourceFormId;
            if (unknownFormID == 0 && out.trace.has_value()) {
                unknownFormID = out.trace->soundFormId;
            }
            RecordUnknownForm(unknownFormID);
        }

        return out;
    }

    void EventNormalizer::RecordUnknownForm(std::uint32_t formID)
    {
        if (formID == 0) {
            _unknownNoFormID.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        _unknownWithFormID.fetch_add(1, std::memory_order_relaxed);

        std::lock_guard lk(_unknownMx);
        if (auto it = _unknownFormHits.find(formID); it != _unknownFormHits.end()) {
            ++it->second;
            return;
        }

        if (_unknownFormHits.size() >= kUnknownFormMapMax) {
            _unknownMapOverflow.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        _unknownFormHits.emplace(formID, 1u);
    }

    EventNormalizer::Stats EventNormalizer::GetStats() const
    {
        Stats s{};
        s.inputs = _inputs.load(std::memory_order_relaxed);
        s.noVoiceID = _noVoiceID.load(std::memory_order_relaxed);
        s.bindingHit = _bindingHit.load(std::memory_order_relaxed);
        s.bindingMiss = _bindingMiss.load(std::memory_order_relaxed);
        s.traceHit = _traceHit.load(std::memory_order_relaxed);
        s.traceMiss = _traceMiss.load(std::memory_order_relaxed);
        s.patchedFormID = _patchedFormID.load(std::memory_order_relaxed);
        s.patchedEventType = _patchedEventType.load(std::memory_order_relaxed);
        s.patchedEventTypeConservative = _patchedEventTypeConservative.load(std::memory_order_relaxed);
        s.unknownAfterNormalize = _unknownAfterNormalize.load(std::memory_order_relaxed);
        s.unknownWithFormID = _unknownWithFormID.load(std::memory_order_relaxed);
        s.unknownNoFormID = _unknownNoFormID.load(std::memory_order_relaxed);
        s.unknownMapOverflow = _unknownMapOverflow.load(std::memory_order_relaxed);

        std::vector<std::pair<std::uint32_t, std::uint32_t>> tops;
        {
            std::lock_guard lk(_unknownMx);
            tops.reserve(_unknownFormHits.size());
            for (const auto& [formID, hits] : _unknownFormHits) {
                tops.emplace_back(formID, hits);
            }
        }

        std::sort(
            tops.begin(),
            tops.end(),
            [](const auto& a, const auto& b) {
                if (a.second != b.second) {
                    return a.second > b.second;
                }
                return a.first < b.first;
            });

        const auto topCount = std::min<std::size_t>(s.unknownTop.size(), tops.size());
        s.unknownTopCount = static_cast<std::uint32_t>(topCount);
        for (std::size_t i = 0; i < topCount; ++i) {
            FormSemanticMeta meta{};
            const bool hasMeta = FormSemanticCache::GetSingleton().TryGet(tops[i].first, meta);
            s.unknownTop[i].formID = tops[i].first;
            s.unknownTop[i].hits = tops[i].second;
            s.unknownTop[i].semantic = hasMeta ? meta.group : SemanticGroup::Unknown;
            s.unknownTop[i].semanticConfidence = hasMeta ? std::clamp(meta.confidence, 0.0f, 1.0f) : 0.0f;
        }
        return s;
    }

    void EventNormalizer::ResetStats()
    {
        _inputs.store(0, std::memory_order_relaxed);
        _noVoiceID.store(0, std::memory_order_relaxed);
        _bindingHit.store(0, std::memory_order_relaxed);
        _bindingMiss.store(0, std::memory_order_relaxed);
        _traceHit.store(0, std::memory_order_relaxed);
        _traceMiss.store(0, std::memory_order_relaxed);
        _patchedFormID.store(0, std::memory_order_relaxed);
        _patchedEventType.store(0, std::memory_order_relaxed);
        _patchedEventTypeConservative.store(0, std::memory_order_relaxed);
        _unknownAfterNormalize.store(0, std::memory_order_relaxed);
        _unknownWithFormID.store(0, std::memory_order_relaxed);
        _unknownNoFormID.store(0, std::memory_order_relaxed);
        _unknownMapOverflow.store(0, std::memory_order_relaxed);
        {
            std::lock_guard lk(_unknownMx);
            _unknownFormHits.clear();
        }
    }
}
