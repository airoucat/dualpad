#include "pch.h"

#include "input_v2/ingress/FrameAssembler.h"

#include <algorithm>
#include <sstream>

namespace logger = SKSE::log;

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

        std::uint64_t MergeDownAtUs(
            const actions::ControlSample& existing,
            const actions::ControlSample& incoming)
        {
            if (existing.downAtUs == 0) {
                return incoming.downAtUs;
            }
            if (incoming.downAtUs == 0) {
                return existing.downAtUs;
            }
            return std::min(existing.downAtUs, incoming.downAtUs);
        }

        FactFrame BuildDurableCarryFacts(FactFrame facts)
        {
            for (auto& sample : facts.controlSamples) {
                sample.pressed = false;
                sample.released = false;
                if (!sample.down) {
                    sample.downAtUs = 0;
                }
            }
            facts.pulseLedger.clear();
            facts.health = FactHealth{};
            facts.overflowCompaction.reset();
            return facts;
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
            const bool pressed = it->pressed || sample.pressed;
            const bool released = it->released || sample.released;
            const auto downAtUs = MergeDownAtUs(*it, sample);
            const auto timestampUs = std::max(it->timestampUs, sample.timestampUs);
            *it = sample;
            it->pressed = pressed;
            it->released = released;
            it->downAtUs = downAtUs;
            it->timestampUs = timestampUs;
        }

        std::string BoolString(bool value)
        {
            return value ? "true" : "false";
        }

        void LogDeviceSequenceGap(const IngressEvent& event)
        {
            logger::warn(
                "[DualPad][SequenceGap] source=device_report expected={} actual={} droppedByCompaction={} pendingBefore={} pendingAfter={} recovery=SoftGap",
                event.sequenceGap.expected,
                event.sequenceGap.actual,
                event.sequenceGap.droppedByCompaction,
                event.sequenceGap.pendingBefore,
                event.sequenceGap.pendingAfter);
        }

        OverflowCompactionDebugSummary BuildOverflowCompactionDebugSummary(
            const QueueOverflowPayload& payload)
        {
            OverflowCompactionDebugSummary summary{
                .transitionObserved = true,
                .typedCompactionApplied = true,
                .retainedManifest = payload.hasManifest,
                .retainedUi = payload.hasUi,
                .retainedDeviceFamily = payload.hasDeviceFamily,
                .retainedSourceEvidence = payload.hasSourceEvidence,
                .droppedControlSamples = payload.droppedControlSamples,
                .droppedPulseLedger = payload.droppedPulseLedger,
                .droppedLegacySnapshot = payload.droppedLegacySnapshot
            };

            std::ostringstream out;
            out << "overflow_transition=true"
                << " typed_compaction=true"
                << " retained_manifest=" << BoolString(summary.retainedManifest)
                << " retained_ui=" << BoolString(summary.retainedUi)
                << " retained_device_family=" << BoolString(summary.retainedDeviceFamily)
                << " retained_source_evidence=" << BoolString(summary.retainedSourceEvidence)
                << " dropped_control_samples=" << BoolString(summary.droppedControlSamples)
                << " dropped_pulse_ledger=" << BoolString(summary.droppedPulseLedger)
                << " dropped_legacy_snapshot=" << BoolString(summary.droppedLegacySnapshot);
            summary.debugSummary = out.str();
            return summary;
        }
    }

    void FrameAssembler::Reset()
    {
        _pendingDeviceMarker.reset();
        _currentKey = IngressBoundaryKey{};
        _latestFacts = FactFrame{};
        _window = Window{};
        _lastConsumedSeq = 0;
        _lastMonotonicUs = 0;
        _lastMonotonicUsBySource = {};
        _lastLatestPadGeneration = 0;
        _lastLatestSourceGeneration = 0;
    }

    std::vector<AssembledFactFrame> FrameAssembler::Assemble(const std::vector<IngressEvent>& events)
    {
        return Assemble(events, std::nullopt, std::nullopt);
    }

    std::vector<AssembledFactFrame> FrameAssembler::Assemble(
        const std::vector<IngressEvent>& events,
        const std::optional<LatestPadState>& latestPadState,
        const std::optional<LatestSourceEvidence>& latestSourceEvidence)
    {
        std::vector<AssembledFactFrame> frames;
        for (const auto& event : events) {
            if (HandleOrderingViolation(frames, event)) {
                continue;
            }

            if (IsHealthMarker(event.kind)) {
                if (event.kind == IngressKind::SequenceGap) {
                    if (event.source == IngressSource::LegacyDispatcher) {
                        LogDeviceSequenceGap(event);
                    }
                    continue;
                }

                FlushWindow(frames);
                auto reason = TransitionReason::ExplicitReset;
                if (event.kind == IngressKind::QueueOverflow) {
                    reason = TransitionReason::QueueOverflow;
                }
                EmitTransition(frames, _currentKey, _currentKey, reason);
                if (event.kind == IngressKind::QueueOverflow) {
                    ApplyOverflowCompaction(frames, event);
                }
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

        if (latestSourceEvidence && latestSourceEvidence->generation > _lastLatestSourceGeneration) {
            ApplyLatestSourceEvidence(frames, *latestSourceEvidence);
        }
        if (latestPadState && latestPadState->generation > _lastLatestPadGeneration) {
            ApplyLatestPadState(*latestPadState);
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
        _window.firstMonotonicUs = event.monotonicUs;
        _window.lastMonotonicUs = event.monotonicUs;
        _window.key = _currentKey;
        _window.facts = BuildDurableCarryFacts(_latestFacts);
        ApplyFactsFromBoundaryKey(_window.facts, _currentKey);
        _window.facts.monotonicUs = std::max(_window.facts.monotonicUs, event.monotonicUs);
    }

    bool FrameAssembler::HandleOrderingViolation(std::vector<AssembledFactFrame>& frames, const IngressEvent& event)
    {
        if (event.seq != 0) {
            if (_lastConsumedSeq != 0) {
                if (event.seq <= _lastConsumedSeq) {
                    FlushWindow(frames);
                    FactHealth health{};
                    health.sequenceGap = true;
                    EmitTransition(frames, _currentKey, _currentKey, TransitionReason::SequenceGap, health);
                    return true;
                }
                if (event.kind != IngressKind::SequenceGap && event.seq > _lastConsumedSeq + 1) {
                    FlushWindow(frames);
                    FactHealth health{};
                    health.sequenceGap = true;
                    EmitTransition(frames, _currentKey, _currentKey, TransitionReason::SequenceGap, health);
                }
            }
            _lastConsumedSeq = event.seq;
        }

        if (event.monotonicUs != 0) {
            // Ingress seq is assigned under the hub lock and is the ordering
            // authority. Producer timestamps are observations from independent
            // threads and may overlap or arrive slightly out of timestamp order.
            // A regression within one producer still rejects volatile history,
            // while cross-producer overlap only advances the evaluation clock by max.
            const auto sourceIndex = static_cast<std::size_t>(event.source);
            auto& lastSourceMonotonicUs = _lastMonotonicUsBySource.at(sourceIndex);
            if (lastSourceMonotonicUs != 0 && event.monotonicUs < lastSourceMonotonicUs) {
                FlushWindow(frames);
                FactHealth health{};
                health.sequenceGap = true;
                EmitTransition(frames, _currentKey, _currentKey, TransitionReason::SequenceGap, health);
                return true;
            }
            lastSourceMonotonicUs = event.monotonicUs;
            _lastMonotonicUs = std::max(_lastMonotonicUs, event.monotonicUs);
        }

        return false;
    }

    void FrameAssembler::ApplyEventToWindow(const IngressEvent& event)
    {
        if (!_window.open) {
            StartWindow(event);
        }
        _window.lastSeq = event.seq;
        if (event.monotonicUs != 0) {
            if (_window.firstMonotonicUs == 0) {
                _window.firstMonotonicUs = event.monotonicUs;
            }
            _window.lastMonotonicUs = event.monotonicUs;
            _window.facts.monotonicUs = std::max(_window.facts.monotonicUs, event.monotonicUs);
        }

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

    void FrameAssembler::ApplyOverflowCompaction(std::vector<AssembledFactFrame>& frames, const IngressEvent& event)
    {
        const auto& payload = event.overflow;
        if (!payload.hasManifest &&
            !payload.hasUi &&
            !payload.hasDeviceFamily &&
            !payload.hasSourceEvidence) {
            return;
        }

        auto nextKey = _currentKey;
        if (payload.hasManifest) {
            nextKey.manifestEpoch = payload.manifest.manifestEpoch;
        }
        if (payload.hasUi) {
            nextKey.contextRevision = payload.ui.contextRevision;
            nextKey.menuStackRevision = payload.ui.menuStackRevision;
        }
        if (payload.hasDeviceFamily) {
            nextKey.deviceFamilyRevision = payload.deviceFamily.deviceFamilyRevision;
        }

        FactFrame facts = _latestFacts;
        facts.controlSamples.clear();
        facts.pulseLedger.clear();
        facts.legacySnapshot.reset();
        facts.health = FactHealth{};
        facts.health.queueOverflow = true;
        facts.monotonicUs = event.monotonicUs;
        facts.overflowCompaction = BuildOverflowCompactionDebugSummary(payload);
        if (payload.hasSourceEvidence) {
            facts.sourceEvidence = payload.sourceEvidence;
            if (payload.hasDeviceFamily &&
                payload.sourceEvidence.deviceFamilyEvidence.deviceFamilyRevision !=
                    payload.deviceFamily.deviceFamilyRevision) {
                facts.health.pendingBoundaryMarkerPair = true;
                facts.health.boundaryMarkerMismatch = true;
            }
        } else if (payload.hasDeviceFamily) {
            facts.health.pendingBoundaryMarkerPair = true;
        }
        ApplyFactsFromBoundaryKey(facts, nextKey);

        frames.push_back(AssembledFactFrame{
            .kind = AssembledFrameKind::Stable,
            .firstSeq = event.seq,
            .lastSeq = event.seq,
            .boundaryKey = nextKey,
            .facts = facts
        });

        _currentKey = nextKey;
        _latestFacts = facts;
        _latestFacts.health = FactHealth{};
        _window = Window{};
        _pendingDeviceMarker.reset();
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
        _latestFacts = BuildDurableCarryFacts(_window.facts);
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
                .flushPendingPulseEdges = hard
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
            if (revision < _pendingDeviceMarker->deviceFamilyRevision) {
                return;
            }
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

        if (revision < _currentKey.deviceFamilyRevision) {
            return;
        }
        if (revision > _currentKey.deviceFamilyRevision) {
            auto nextKey = _currentKey;
            nextKey.deviceFamilyRevision = revision;
            FlushWindow(frames);
            EmitTransition(frames, _currentKey, nextKey, TransitionReason::BoundaryKeyChanged);
            _currentKey = nextKey;
            ApplyEventToWindow(event);
            return;
        }

        ApplyEventToWindow(event);
    }

    void FrameAssembler::ApplyLatestSourceEvidence(
        std::vector<AssembledFactFrame>& frames,
        const LatestSourceEvidence& latest)
    {
        const auto revision = latest.snapshot.deviceFamilyEvidence.deviceFamilyRevision;
        if (!_pendingDeviceMarker && revision > _currentKey.deviceFamilyRevision) {
            return;
        }
        _lastLatestSourceGeneration = latest.generation;
        IngressEvent event{};
        event.seq = _lastConsumedSeq;
        event.monotonicUs = std::max(latest.snapshot.collectedTick, _lastMonotonicUs);
        event.source = IngressSource::DeviceFamilyPublisher;
        event.kind = IngressKind::SourceEvidence;
        event.sourceEvidence = latest.snapshot;
        HandleSourceEvidence(frames, event);
        if (_window.open) {
            _window.facts.latestSourceEvidenceGeneration = latest.generation;
            _latestFacts = _window.facts;
        } else {
            _latestFacts.latestSourceEvidenceGeneration = latest.generation;
        }
    }

    void FrameAssembler::ApplyLatestPadState(const LatestPadState& latest)
    {
        const auto contextRevision = latest.contextRevision != 0 ? latest.contextRevision : latest.contextEpoch;
        if (_currentKey.contextRevision != contextRevision ||
            _currentKey.menuStackRevision != latest.contextEpoch) {
            return;
        }
        _lastLatestPadGeneration = latest.generation;
        IngressEvent event{};
        event.seq = _lastConsumedSeq;
        event.monotonicUs = std::max(latest.sourceTimestampUs, _lastMonotonicUs);
        event.source = IngressSource::LegacyDispatcher;
        event.kind = IngressKind::PadSnapshot;
        event.pad.samples = BuildLatestAnalogSamples(latest);
        ApplyEventToWindow(event);
        _window.facts.latestPadStateGeneration = latest.generation;
        _latestFacts = _window.facts;
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
        kernel.facts.monotonicUs = frame.facts.monotonicUs;
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
