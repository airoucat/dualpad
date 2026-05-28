#include "pch.h"

#include "input_v2/ingress/FrameAssembler.h"

#include <algorithm>

namespace dualpad::input_v2::ingress
{
    namespace
    {
        bool IsPulse(const actions::ControlSample& sample)
        {
            return sample.pressed || sample.released;
        }

        bool IsHealthMarker(IngressKind kind)
        {
            return kind == IngressKind::SequenceGap ||
                kind == IngressKind::QueueOverflow ||
                kind == IngressKind::ExplicitReset;
        }

        void UpsertLatestSample(std::vector<actions::ControlSample>& samples, const actions::ControlSample& sample)
        {
            auto it = std::find_if(
                samples.begin(),
                samples.end(),
                [&](const actions::ControlSample& existing) {
                    return existing.path == sample.path;
                });
            if (it == samples.end()) {
                samples.push_back(sample);
                return;
            }
            *it = sample;
        }
    }

    void FrameAssembler::Reset()
    {
        _pendingDeviceMarker.reset();
        _currentKey = IngressBoundaryKey{};
        _latestFacts = FactFrame{};
        _window = Window{};
    }

    std::vector<AssembledFactFrame> FrameAssembler::Assemble(const std::vector<IngressEvent>& events)
    {
        std::vector<IngressEvent> sorted = events;
        std::sort(
            sorted.begin(),
            sorted.end(),
            [](const IngressEvent& lhs, const IngressEvent& rhs) {
                return lhs.seq < rhs.seq;
            });

        std::vector<AssembledFactFrame> frames;
        for (const auto& event : sorted) {
            if (IsHealthMarker(event.kind)) {
                FlushWindow(frames);
                auto reason = TransitionReason::ExplicitReset;
                if (event.kind == IngressKind::SequenceGap) {
                    reason = TransitionReason::SequenceGap;
                } else if (event.kind == IngressKind::QueueOverflow) {
                    reason = TransitionReason::QueueOverflow;
                }
                EmitTransition(frames, _currentKey, _currentKey, reason);
                continue;
            }

            if (event.kind == IngressKind::ManifestEpochChanged) {
                auto nextKey = _currentKey;
                nextKey.manifestEpoch = event.manifest.manifestEpoch;
                HandleBoundaryChange(frames, event, nextKey, TransitionReason::ManifestEpochChanged);
                continue;
            }

            if (event.kind == IngressKind::UiSnapshot) {
                auto nextKey = _currentKey;
                nextKey.contextRevision = event.ui.contextRevision;
                nextKey.menuStackRevision = event.ui.menuStackRevision;
                if (nextKey == _currentKey) {
                    ApplyEventToWindow(event);
                } else {
                    HandleBoundaryChange(frames, event, nextKey, TransitionReason::BoundaryKeyChanged);
                }
                continue;
            }

            if (event.kind == IngressKind::DeviceFamilyChanged) {
                _pendingDeviceMarker = event.deviceFamily;
                auto nextKey = _currentKey;
                nextKey.deviceFamilyRevision = event.deviceFamily.deviceFamilyRevision;
                FlushWindow(frames);
                EmitTransition(frames, _currentKey, nextKey, TransitionReason::BoundaryKeyChanged);
                _currentKey = nextKey;
                continue;
            }

            if (event.kind == IngressKind::SourceEvidence) {
                HandleSourceEvidence(frames, event);
                continue;
            }

            ApplyEventToWindow(event);
        }

        FlushWindow(frames);
        return frames;
    }

    void FrameAssembler::ApplyFactsFromBoundaryKey(FactFrame& facts, const IngressBoundaryKey& key) const
    {
        facts.manifestEpoch = key.manifestEpoch;
        facts.contextRevision = key.contextRevision;
        facts.menuStackRevision = key.menuStackRevision;
        facts.deviceFamilyRevision = key.deviceFamilyRevision;
    }

    void FrameAssembler::StartWindow(const IngressEvent& event)
    {
        _window = Window{};
        _window.open = true;
        _window.firstSeq = event.seq;
        _window.lastSeq = event.seq;
        _window.key = _currentKey;
        _window.facts = _latestFacts;
        ApplyFactsFromBoundaryKey(_window.facts, _currentKey);
    }

    void FrameAssembler::ApplyEventToWindow(const IngressEvent& event)
    {
        if (!_window.open) {
            StartWindow(event);
        }
        _window.lastSeq = event.seq;

        if (event.kind == IngressKind::PadSnapshot) {
            if (event.pad.legacySnapshot) {
                _window.facts.legacySnapshot = event.pad.legacySnapshot;
            }
            _window.facts.health.coalescedSnapshot =
                _window.facts.health.coalescedSnapshot || event.pad.coalesced;
            _window.facts.health.crossContextMismatch =
                _window.facts.health.crossContextMismatch || event.pad.crossContextMismatch;
            _window.facts.health.queueOverflow =
                _window.facts.health.queueOverflow || event.pad.overflowed;
            for (const auto& sample : event.pad.samples) {
                if (IsPulse(sample)) {
                    _window.facts.pulseLedger.push_back(sample);
                }
                UpsertLatestSample(_window.facts.controlSamples, sample);
            }
        } else if (event.kind == IngressKind::SourceEvidence) {
            _window.facts.sourceEvidence = event.sourceEvidence;
        }

        _latestFacts = _window.facts;
    }

    void FrameAssembler::FlushWindow(std::vector<AssembledFactFrame>& frames)
    {
        if (!_window.open) {
            return;
        }

        ApplyFactsFromBoundaryKey(_window.facts, _window.key);
        frames.push_back(AssembledFactFrame{
            .kind = AssembledFrameKind::Stable,
            .firstSeq = _window.firstSeq,
            .lastSeq = _window.lastSeq,
            .boundaryKey = _window.key,
            .facts = _window.facts
        });
        _window = Window{};
    }

    void FrameAssembler::EmitTransition(
        std::vector<AssembledFactFrame>& frames,
        const IngressBoundaryKey& from,
        const IngressBoundaryKey& to,
        TransitionReason reason,
        FactHealth health)
    {
        const bool hard = reason == TransitionReason::ManifestEpochChanged ||
            reason == TransitionReason::QueueOverflow ||
            reason == TransitionReason::ExplicitReset;
        const bool soft = reason == TransitionReason::SequenceGap;

        FactFrame facts = _latestFacts;
        ApplyFactsFromBoundaryKey(facts, to);
        facts.health.boundaryMarkerMismatch = facts.health.boundaryMarkerMismatch || health.boundaryMarkerMismatch;
        facts.health.pendingBoundaryMarkerPair = facts.health.pendingBoundaryMarkerPair || health.pendingBoundaryMarkerPair;
        facts.health.queueOverflow = reason == TransitionReason::QueueOverflow;
        facts.health.sequenceGap = reason == TransitionReason::SequenceGap;

        frames.push_back(AssembledFactFrame{
            .kind = AssembledFrameKind::Transition,
            .boundaryKey = to,
            .facts = facts,
            .transition = TransitionFrameMeta{
                .from = from,
                .to = to,
                .reason = reason,
                .requestSoftResync = soft,
                .requestHardResync = hard,
                .flushPendingPulseEdges = hard || soft
            }
        });
    }

    void FrameAssembler::HandleBoundaryChange(
        std::vector<AssembledFactFrame>& frames,
        const IngressEvent& event,
        IngressBoundaryKey nextKey,
        TransitionReason reason)
    {
        FlushWindow(frames);
        EmitTransition(frames, _currentKey, nextKey, reason);
        _currentKey = nextKey;
        if (event.kind != IngressKind::ManifestEpochChanged) {
            ApplyEventToWindow(event);
        }
    }

    void FrameAssembler::HandleSourceEvidence(std::vector<AssembledFactFrame>& frames, const IngressEvent& event)
    {
        const auto revision = event.sourceEvidence.deviceFamilyEvidence.deviceFamilyRevision;
        if (_pendingDeviceMarker) {
            if (_pendingDeviceMarker->deviceFamilyRevision != revision) {
                FactHealth health{};
                health.pendingBoundaryMarkerPair = true;
                health.boundaryMarkerMismatch = true;
                FlushWindow(frames);
                EmitTransition(frames, _currentKey, _currentKey, TransitionReason::ExplicitReset, health);
                _pendingDeviceMarker.reset();
                return;
            }
            _pendingDeviceMarker.reset();
            ApplyEventToWindow(event);
            return;
        }

        if (revision != _currentKey.deviceFamilyRevision) {
            FactHealth health{};
            health.boundaryMarkerMismatch = true;
            FlushWindow(frames);
            EmitTransition(frames, _currentKey, _currentKey, TransitionReason::ExplicitReset, health);
            return;
        }

        ApplyEventToWindow(event);
    }

    bool ShouldDispatchToInteractionEngine(const AssembledFactFrame& frame)
    {
        return frame.kind == AssembledFrameKind::Stable;
    }

    actions::KernelFrame BuildKernelFrame(const AssembledFactFrame& frame)
    {
        actions::KernelFrame kernel{};
        if (frame.kind != AssembledFrameKind::Stable) {
            kernel.state.healthDegraded = true;
            return kernel;
        }

        kernel.facts.manifestEpoch = frame.boundaryKey.manifestEpoch;
        kernel.facts.contextRevision = frame.boundaryKey.contextRevision;
        kernel.facts.menuStackRevision = frame.boundaryKey.menuStackRevision;
        kernel.facts.deviceFamilyRevision = frame.boundaryKey.deviceFamilyRevision;
        kernel.facts.monotonicUs = frame.lastSeq;
        kernel.state.controlSamples = frame.facts.controlSamples;
        kernel.state.cleanBoundaryBaseline = true;
        kernel.state.healthDegraded = frame.facts.health.boundaryMarkerMismatch ||
            frame.facts.health.pendingBoundaryMarkerPair ||
            frame.facts.health.queueOverflow ||
            frame.facts.health.sequenceGap ||
            frame.facts.health.coalescedSnapshot ||
            frame.facts.health.crossContextMismatch;
        kernel.kernelRevision = frame.lastSeq;
        return kernel;
    }
}
