#include "pch.h"
#include "haptics/EventNormalizer.h"

namespace dualpad::haptics
{
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

        return out;
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
    }
}
