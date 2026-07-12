#include "pch.h"

#include "input_v2/ingress/IngressHub.h"

#include "input_v2/ingress/LegacyIngressAdapter.h"
#include "input_v2/ingress/LiveInputFactProducer.h"

#include <algorithm>
#include <chrono>
#include <iterator>
#include <limits>

namespace dualpad::input_v2::ingress
{
    namespace
    {
        bool IsPulse(const actions::ControlSample& sample)
        {
            return sample.pressed || sample.released;
        }

        void CaptureOverflowFact(QueueOverflowPayload& payload, const IngressEvent& event)
        {
            switch (event.kind) {
            case IngressKind::PadSnapshot:
                payload.droppedControlSamples = payload.droppedControlSamples ||
                    !event.pad.samples.empty();
                payload.droppedLegacySnapshot = payload.droppedLegacySnapshot ||
                    event.pad.legacySnapshot.has_value();
                for (const auto& sample : event.pad.samples) {
                    payload.droppedPulseLedger = payload.droppedPulseLedger || IsPulse(sample);
                }
                break;
            case IngressKind::ManifestEpochChanged:
                payload.hasManifest = true;
                payload.manifest = event.manifest;
                break;
            case IngressKind::UiSnapshot:
                payload.hasUi = true;
                payload.ui = event.ui;
                break;
            case IngressKind::DeviceFamilyChanged:
                payload.hasDeviceFamily = true;
                payload.deviceFamily = event.deviceFamily;
                break;
            case IngressKind::SourceEvidence:
                payload.hasSourceEvidence = true;
                payload.sourceEvidence = event.sourceEvidence;
                break;
            case IngressKind::QueueOverflow:
                if (event.overflow.hasManifest) {
                    payload.hasManifest = true;
                    payload.manifest = event.overflow.manifest;
                }
                if (event.overflow.hasUi) {
                    payload.hasUi = true;
                    payload.ui = event.overflow.ui;
                }
                if (event.overflow.hasDeviceFamily) {
                    payload.hasDeviceFamily = true;
                    payload.deviceFamily = event.overflow.deviceFamily;
                }
                if (event.overflow.hasSourceEvidence) {
                    payload.hasSourceEvidence = true;
                    payload.sourceEvidence = event.overflow.sourceEvidence;
                }
                payload.droppedControlSamples = payload.droppedControlSamples ||
                    event.overflow.droppedControlSamples;
                payload.droppedPulseLedger = payload.droppedPulseLedger ||
                    event.overflow.droppedPulseLedger;
                payload.droppedLegacySnapshot = payload.droppedLegacySnapshot ||
                    event.overflow.droppedLegacySnapshot;
                break;
            default:
                break;
            }
        }
    }

    IngressHub::IngressHub(std::size_t capacity) :
        _capacity(capacity)
    {}

    IngressHub& IngressHub::GetSingleton()
    {
        static IngressHub hub;
        return hub;
    }

    std::uint64_t IngressHub::NextSeqLocked()
    {
        const auto seq = _nextSeq++;
        _lastAllocatedOrderedSeq = seq;
        return seq;
    }

    std::uint64_t IngressHub::NowMonotonicUs() const
    {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(now).count());
    }

    bool IngressHub::PushEvent(IngressEvent event)
    {
        std::scoped_lock lock(_mutex);
        event.seq = NextSeqLocked();
        if (event.monotonicUs == 0) {
            event.monotonicUs = NowMonotonicUs();
        }
        if (_queue.size() >= _capacity) {
            ReplaceBacklogWithOverflowLocked(event.seq, event.monotonicUs, { event });
            return false;
        }
        _queue.push_back(std::move(event));
        return true;
    }

    bool IngressHub::PushEvents(std::vector<IngressEvent> events)
    {
        if (events.empty()) {
            return true;
        }

        std::scoped_lock lock(_mutex);
        const auto available = _capacity > _queue.size() ? _capacity - _queue.size() : 0;
        if (events.size() > available) {
            const auto overflowSeq = NextSeqLocked();
            auto overflowTime = events.front().monotonicUs;
            if (overflowTime == 0) {
                overflowTime = NowMonotonicUs();
            }
            ReplaceBacklogWithOverflowLocked(overflowSeq, overflowTime, events);
            return false;
        }

        for (auto& event : events) {
            event.seq = NextSeqLocked();
            if (event.monotonicUs == 0) {
                event.monotonicUs = NowMonotonicUs();
            }
            _queue.push_back(std::move(event));
        }
        return true;
    }

    bool IngressHub::PushLocked(IngressEvent event)
    {
        if (_queue.size() >= _capacity) {
            ReplaceBacklogWithOverflowLocked(event.seq, event.monotonicUs, { event });
            return false;
        }
        _queue.push_back(std::move(event));
        return true;
    }

    void IngressHub::ReplaceBacklogWithOverflowLocked(
        std::uint64_t seq,
        std::uint64_t monotonicUs,
        const std::vector<IngressEvent>& incomingEvents)
    {
        IngressEvent overflow = MakeQueueOverflowEvent();
        overflow.seq = seq;
        overflow.monotonicUs = monotonicUs != 0 ? monotonicUs : NowMonotonicUs();
        for (const auto& event : _queue) {
            CaptureOverflowFact(overflow.overflow, event);
        }
        for (const auto& event : incomingEvents) {
            CaptureOverflowFact(overflow.overflow, event);
        }
        _edgeHistoryLost = _edgeHistoryLost ||
            overflow.overflow.droppedControlSamples ||
            overflow.overflow.droppedPulseLedger;
        _queue.clear();
        _queue.push_back(overflow);
        _pendingLegacySnapshots = 0;
    }

    bool IngressHub::PushPadSnapshot(
        const dualpad::input::PadEventSnapshot& snapshot,
        bool retainLegacySnapshot,
        const presentation::SourceEvidenceFrame* sourceEvidenceFrame)
    {
        std::scoped_lock lock(_mutex);
        std::vector<IngressEvent> converted;
        if (sourceEvidenceFrame) {
            ApplySourceEvidenceFrameLocked(*sourceEvidenceFrame, converted);
        }

        if (snapshot.type == dualpad::input::PadEventSnapshotType::Reset) {
            _latestPadState.reset();
            _previousDigitalMask = 0;
            _digitalDownAtUs = {};
            _lastUiSnapshot.reset();
        } else {
            _latestPadState = LatestPadState{
                .generation = ++_latestPadGeneration,
                .sourceSequence = snapshot.sequence,
                .sourceTimestampUs = LatestTimestampUs(snapshot),
                .context = snapshot.context,
                .contextEpoch = snapshot.contextEpoch,
                .contextRevision = snapshot.contextRevision,
                .currentDownMask = snapshot.state.buttons.digitalMask,
                .state = snapshot.state
            };
        }

        const UiSnapshotPayload uiSnapshot{
            .contextRevision = snapshot.contextRevision != 0 ? snapshot.contextRevision : snapshot.contextEpoch,
            .menuStackRevision = snapshot.contextEpoch
        };
        const bool includeUiSnapshot =
            snapshot.type != dualpad::input::PadEventSnapshotType::Reset &&
            (!_lastUiSnapshot ||
                _lastUiSnapshot->contextRevision != uiSnapshot.contextRevision ||
                _lastUiSnapshot->menuStackRevision != uiSnapshot.menuStackRevision);
        auto legacyEvents = ConvertLegacySnapshotToIngressEvents(
            snapshot,
            _lastLegacySequence,
            LegacyIngressConversionOptions{
                .includeContinuousSamples = retainLegacySnapshot,
                .retainLegacySnapshot = retainLegacySnapshot,
                .includeUiSnapshot = includeUiSnapshot,
                .resetLiveProducer = false
            });

        if (snapshot.type != dualpad::input::PadEventSnapshotType::Reset) {
            auto digitalEdges = BuildOrderedDigitalEdges(snapshot, _previousDigitalMask, _digitalDownAtUs);
            _previousDigitalMask = snapshot.state.buttons.digitalMask;
            if (_edgeHistoryLost) {
                digitalEdges.clear();
                if (snapshot.state.buttons.digitalMask == 0) {
                    _edgeHistoryLost = false;
                    _digitalDownAtUs = {};
                }
            }

            auto pad = std::find_if(legacyEvents.begin(), legacyEvents.end(), [](const IngressEvent& event) {
                return event.kind == IngressKind::PadSnapshot;
            });
            if (pad == legacyEvents.end() && (!digitalEdges.empty() || retainLegacySnapshot)) {
                IngressEvent event{};
                event.kind = IngressKind::PadSnapshot;
                event.source = IngressSource::LegacyDispatcher;
                event.monotonicUs = LatestTimestampUs(snapshot);
                event.pad.firstSequence = snapshot.firstSequence != 0 ? snapshot.firstSequence : snapshot.sequence;
                event.pad.sequence = snapshot.sequence;
                event.pad.overflowed = snapshot.overflowed || snapshot.events.overflowed;
                event.pad.coalesced = snapshot.coalesced;
                event.pad.crossContextMismatch = snapshot.crossContextMismatch;
                if (retainLegacySnapshot) {
                    event.pad.legacySnapshot = snapshot;
                }
                event.pad.samples = std::move(digitalEdges);
                legacyEvents.push_back(std::move(event));
            } else if (pad != legacyEvents.end() && !retainLegacySnapshot) {
                pad->pad.samples = std::move(digitalEdges);
                if (pad->pad.samples.empty()) {
                    legacyEvents.erase(pad);
                }
            }
        }

        converted.insert(
            converted.end(),
            std::make_move_iterator(legacyEvents.begin()),
            std::make_move_iterator(legacyEvents.end()));

        const auto available = _capacity > _queue.size() ? _capacity - _queue.size() : 0;
        if (converted.size() > available) {
            const auto overflowSeq = NextSeqLocked();
            const auto overflowTime = snapshot.sourceTimestampUs != 0 ? snapshot.sourceTimestampUs : NowMonotonicUs();
            ReplaceBacklogWithOverflowLocked(overflowSeq, overflowTime, converted);
            if (_edgeHistoryLost &&
                snapshot.type != dualpad::input::PadEventSnapshotType::Reset &&
                snapshot.state.buttons.digitalMask == 0) {
                _edgeHistoryLost = false;
                _digitalDownAtUs = {};
            }
            // QueueOverflow represents a dropped legacy input range through this snapshot.
            // Advance the watermark so the next contiguous accepted snapshot does not
            // report a second SequenceGap for the same discarded range.
            if (snapshot.sequence != 0) {
                _lastLegacySequence = snapshot.sequence;
            }
            if (!retainLegacySnapshot && snapshot.type != dualpad::input::PadEventSnapshotType::Reset) {
                _lastUiSnapshot = uiSnapshot;
            }
            return false;
        }

        for (auto& event : converted) {
            event.seq = NextSeqLocked();
            if (event.monotonicUs == 0) {
                event.monotonicUs = NowMonotonicUs();
            }
            _queue.push_back(std::move(event));
        }
        if (snapshot.sequence != 0) {
            _lastLegacySequence = snapshot.sequence;
        }
        if (snapshot.type != dualpad::input::PadEventSnapshotType::Reset) {
            _lastUiSnapshot = uiSnapshot;
        }
        if (retainLegacySnapshot && snapshot.type != dualpad::input::PadEventSnapshotType::Reset) {
            ++_pendingLegacySnapshots;
        }
        return true;
    }

    PublishedIngressBatchReceipt IngressHub::PublishGamepadBatch(
        ClassifiedGamepadReportDraft report,
        std::optional<GamepadConnectionDraft> connection)
    {
        std::scoped_lock lock(_mutex);
        const auto orderedCount = report.orderedDigitalEdges.size() +
            report.meaningfulActivities.size() + report.sourceActivities.size();
        const auto available = _capacity > _queue.size() ? _capacity - _queue.size() : 0;

        if (orderedCount > available) {
            const auto overflowSeq = NextSeqLocked();
            ReplaceBacklogWithOverflowLocked(overflowSeq, report.current.sourceTimestampUs, {});
            ++_inputStateEpoch;

            _latestPadState = LatestPadState{
                .generation = ++_latestPadGeneration,
                .sourceSequence = report.current.sourceSequence,
                .sourceTimestampUs = report.current.sourceTimestampUs,
                .context = dualpad::input::InputContext::Gameplay,
                .contextEpoch = 0,
                .contextRevision = _boundaryKey.contextRevision,
                .currentDownMask = report.current.currentDownMask,
                .state = report.current.state,
                .causalOrderedTailSeq = overflowSeq,
                .inputStateEpoch = _inputStateEpoch,
                .gamepadSessionId = _gamepadSessionId,
                .virtualGameplayEligible = false,
                .recoveryReasons = ToMask(InputResetReason::QueueOverflow)
            };

            return PublishedIngressBatchReceipt{
                .accepted = false,
                .publishedResetReasons = ToMask(InputResetReason::QueueOverflow),
                .publishedResetScope = InputResetScope::GlobalInputState,
                .causalOrderedTailSeq = overflowSeq,
                .inputStateEpoch = _inputStateEpoch,
                .gamepadSessionId = _gamepadSessionId,
                .contextRevision = _boundaryKey.contextRevision,
                .controlMapRevision = _boundaryKey.controlMapRevision
            };
        }

        if (connection && connection->connectivity != _gamepadConnectivity) {
            _gamepadConnectivity = connection->connectivity;
            ++_gamepadSessionId;
        }

        std::uint64_t firstOrderedSeq = 0;
        const auto nextOrdered = [&]() {
            const auto seq = NextSeqLocked();
            if (firstOrderedSeq == 0) {
                firstOrderedSeq = seq;
            }
            return seq;
        };

        for (auto& draft : report.orderedDigitalEdges) {
            IngressEvent event{};
            event.seq = nextOrdered();
            event.monotonicUs = draft.sourceTimestampUs != 0 ? draft.sourceTimestampUs : NowMonotonicUs();
            event.kind = IngressKind::GamepadDigitalEdge;
            event.gamepadDigitalEdge = GamepadDigitalEdge{
                draft,
                event.seq,
                _inputStateEpoch,
                _gamepadSessionId,
                _boundaryKey.contextRevision
            };
            _queue.push_back(std::move(event));
        }

        for (auto& draft : report.meaningfulActivities) {
            IngressEvent event{};
            event.seq = nextOrdered();
            event.monotonicUs = draft.sourceTimestampUs != 0 ? draft.sourceTimestampUs : NowMonotonicUs();
            event.kind = IngressKind::GamepadMeaningfulActivity;
            event.gamepadActivity = GamepadMeaningfulActivity{
                draft,
                event.seq,
                _inputStateEpoch,
                _gamepadSessionId,
                _boundaryKey.contextRevision
            };
            _queue.push_back(std::move(event));
        }

        for (auto& draft : report.sourceActivities) {
            IngressEvent event{};
            event.seq = nextOrdered();
            event.monotonicUs = draft.producerTimestampUs != 0 ? draft.producerTimestampUs : NowMonotonicUs();
            event.kind = IngressKind::MeaningfulSourceActivity;
            event.sourceActivity = MeaningfulSourceActivity{
                draft,
                event.seq,
                _inputStateEpoch,
                draft.source == PhysicalInputSource::Gamepad ? _gamepadSessionId : 0,
                _boundaryKey.contextRevision
            };
            _queue.push_back(std::move(event));
        }

        const auto causalTail = _lastAllocatedOrderedSeq;
        _latestPadState = LatestPadState{
            .generation = ++_latestPadGeneration,
            .sourceSequence = report.current.sourceSequence,
            .sourceTimestampUs = report.current.sourceTimestampUs,
            .context = dualpad::input::InputContext::Gameplay,
            .contextEpoch = 0,
            .contextRevision = _boundaryKey.contextRevision,
            .currentDownMask = report.current.currentDownMask,
            .state = report.current.state,
            .causalOrderedTailSeq = causalTail,
            .inputStateEpoch = _inputStateEpoch,
            .gamepadSessionId = _gamepadSessionId,
            .virtualGameplayEligible = true,
            .recoveryReasons = 0
        };

        if (connection) {
            _latestGamepadConnection = GamepadConnectionFacts{
                .causal = CausalLatestHeader{
                    .generation = ++_latestGamepadConnectionGeneration,
                    .causalOrderedTailSeq = causalTail,
                    .inputStateEpoch = _inputStateEpoch,
                    .contextRevision = _boundaryKey.contextRevision,
                    .controlMapRevision = _boundaryKey.controlMapRevision
                },
                .connectivity = connection->connectivity,
                .gamepadSessionId = _gamepadSessionId
            };
        }

        return PublishedIngressBatchReceipt{
            .accepted = true,
            .firstOrderedSeq = firstOrderedSeq,
            .causalOrderedTailSeq = causalTail,
            .inputStateEpoch = _inputStateEpoch,
            .gamepadSessionId = _gamepadSessionId,
            .contextRevision = _boundaryKey.contextRevision,
            .controlMapRevision = _boundaryKey.controlMapRevision
        };
    }

    PublishedIngressBatchReceipt IngressHub::PublishOwnerKbmBatch(OwnerKbmIngressDraft batch)
    {
        std::scoped_lock lock(_mutex);
        const auto orderedCount = batch.kbm.orderedEdges.size() + batch.kbm.sourceActivities.size();
        const auto available = _capacity > _queue.size() ? _capacity - _queue.size() : 0;
        if (orderedCount > available) {
            const auto overflowSeq = NextSeqLocked();
            ReplaceBacklogWithOverflowLocked(overflowSeq, NowMonotonicUs(), {});
            ++_inputStateEpoch;
            return PublishedIngressBatchReceipt{
                .accepted = false,
                .publishedResetReasons = ToMask(InputResetReason::QueueOverflow),
                .publishedResetScope = InputResetScope::GlobalInputState,
                .causalOrderedTailSeq = overflowSeq,
                .inputStateEpoch = _inputStateEpoch,
                .gamepadSessionId = _gamepadSessionId,
                .contextRevision = _boundaryKey.contextRevision,
                .controlMapRevision = _boundaryKey.controlMapRevision
            };
        }

        _boundaryKey.contextRevision = batch.boundary.contextRevision;
        _boundaryKey.menuStackRevision = batch.boundary.menuStackRevision;
        if (batch.boundary.controlMapFingerprint != _controlMapFingerprint) {
            _controlMapFingerprint = batch.boundary.controlMapFingerprint;
            ++_boundaryKey.controlMapRevision;
        }

        std::uint64_t firstOrderedSeq = 0;
        const auto nextOrdered = [&]() {
            const auto seq = NextSeqLocked();
            if (firstOrderedSeq == 0) {
                firstOrderedSeq = seq;
            }
            return seq;
        };

        for (auto& draft : batch.kbm.orderedEdges) {
            IngressEvent event{};
            event.seq = nextOrdered();
            event.monotonicUs = draft.producerTimestampUs != 0 ? draft.producerTimestampUs : NowMonotonicUs();
            event.kind = IngressKind::KbmGameplayEdge;
            event.kbmGameplayEdge = KbmGameplayEdge{
                draft,
                event.seq,
                _inputStateEpoch,
                _boundaryKey.contextRevision,
                _boundaryKey.controlMapRevision
            };
            _queue.push_back(std::move(event));
        }

        for (auto& draft : batch.kbm.sourceActivities) {
            IngressEvent event{};
            event.seq = nextOrdered();
            event.monotonicUs = draft.producerTimestampUs != 0 ? draft.producerTimestampUs : NowMonotonicUs();
            event.kind = IngressKind::MeaningfulSourceActivity;
            event.sourceActivity = MeaningfulSourceActivity{
                draft,
                event.seq,
                _inputStateEpoch,
                0,
                _boundaryKey.contextRevision
            };
            _queue.push_back(std::move(event));
        }

        const auto causalTail = _lastAllocatedOrderedSeq;
        const auto physicalMouseMoveThisFrame = std::any_of(
            batch.kbm.orderedEdges.begin(),
            batch.kbm.orderedEdges.end(),
            [](const KbmGameplayEdgeDraft& edge) {
                return edge.phase == KbmEdgePhase::MouseDelta && edge.origin == KbmEdgeOrigin::Physical;
            });
        _latestKbmGameplay = LatestKbmGameplayFacts{
            .causal = CausalLatestHeader{
                .generation = ++_latestKbmGameplayGeneration,
                .causalOrderedTailSeq = causalTail,
                .inputStateEpoch = _inputStateEpoch,
                .contextRevision = _boundaryKey.contextRevision,
                .controlMapRevision = _boundaryKey.controlMapRevision
            },
            .ownerTickToken = batch.kbm.ownerTickToken,
            .eventBatchToken = batch.kbm.eventBatchToken,
            .current = batch.kbm.completeCurrent,
            .physical = batch.kbm.physical,
            .lastPhysicalMouseMoveOwnerUs = batch.kbm.lastPhysicalMouseMoveOwnerUs,
            .physicalMouseMoveThisFrame = physicalMouseMoveThisFrame,
            .baseline = batch.kbm.baseline,
            .resetReasons = 0
        };

        return PublishedIngressBatchReceipt{
            .accepted = true,
            .firstOrderedSeq = firstOrderedSeq,
            .causalOrderedTailSeq = causalTail,
            .inputStateEpoch = _inputStateEpoch,
            .gamepadSessionId = _gamepadSessionId,
            .contextRevision = _boundaryKey.contextRevision,
            .controlMapRevision = _boundaryKey.controlMapRevision
        };
    }

    PublishedIngressBatchReceipt IngressHub::PublishGlobalReset(
        InputResetReasonMask reasons,
        InputResetScope scope)
    {
        std::scoped_lock lock(_mutex);
        if (scope == InputResetScope::GlobalInputState) {
            ++_inputStateEpoch;
        }
        if (scope != InputResetScope::KeyboardMouseSource) {
            _latestPadState.reset();
        }
        if (scope != InputResetScope::GamepadSource) {
            _latestKbmGameplay.reset();
        }

        IngressEvent event{};
        event.seq = NextSeqLocked();
        event.monotonicUs = NowMonotonicUs();
        event.kind = IngressKind::InputReset;
        event.inputReset = InputResetMarker{
            .reasons = reasons,
            .scope = scope,
            .inputStateEpoch = _inputStateEpoch,
            .gamepadSessionId = _gamepadSessionId,
            .contextRevision = _boundaryKey.contextRevision,
            .controlMapRevision = _boundaryKey.controlMapRevision
        };
        const auto accepted = PushLocked(std::move(event));
        return PublishedIngressBatchReceipt{
            .accepted = accepted,
            .publishedResetReasons = reasons,
            .publishedResetScope = scope,
            .firstOrderedSeq = accepted ? _lastAllocatedOrderedSeq : 0,
            .causalOrderedTailSeq = _lastAllocatedOrderedSeq,
            .inputStateEpoch = _inputStateEpoch,
            .gamepadSessionId = _gamepadSessionId,
            .contextRevision = _boundaryKey.contextRevision,
            .controlMapRevision = _boundaryKey.controlMapRevision
        };
    }

    PublishedIngressBatchReceipt IngressHub::PublishGamepadDisconnect()
    {
        std::scoped_lock lock(_mutex);
        if (_gamepadConnectivity != GamepadConnectivity::Disconnected) {
            _gamepadConnectivity = GamepadConnectivity::Disconnected;
            ++_gamepadSessionId;
        }
        _latestPadState.reset();
        _latestGamepadConnection = GamepadConnectionFacts{
            .causal = CausalLatestHeader{
                .generation = ++_latestGamepadConnectionGeneration,
                .causalOrderedTailSeq = _lastAllocatedOrderedSeq,
                .inputStateEpoch = _inputStateEpoch,
                .contextRevision = _boundaryKey.contextRevision,
                .controlMapRevision = _boundaryKey.controlMapRevision
            },
            .connectivity = GamepadConnectivity::Disconnected,
            .gamepadSessionId = _gamepadSessionId
        };
        return PublishedIngressBatchReceipt{
            .accepted = true,
            .publishedResetReasons = ToMask(InputResetReason::DeviceDisconnected),
            .publishedResetScope = InputResetScope::GamepadSource,
            .causalOrderedTailSeq = _lastAllocatedOrderedSeq,
            .inputStateEpoch = _inputStateEpoch,
            .gamepadSessionId = _gamepadSessionId,
            .contextRevision = _boundaryKey.contextRevision,
            .controlMapRevision = _boundaryKey.controlMapRevision
        };
    }

    void IngressHub::ApplySourceEvidenceFrameLocked(
        const presentation::SourceEvidenceFrame& frame,
        std::vector<IngressEvent>& orderedEvents)
    {
        for (const auto& record : frame.records) {
            if (record.kind == presentation::SourceEvidenceRecordKind::DeviceFamilyChanged) {
                IngressEvent marker{};
                marker.kind = IngressKind::DeviceFamilyChanged;
                marker.source = IngressSource::DeviceFamilyPublisher;
                marker.monotonicUs = record.deviceFamilyChanged.publishedTick;
                marker.deviceFamily = DeviceFamilyChangedPayload{
                    .family = record.deviceFamilyChanged.family,
                    .deviceFamilyRevision = record.deviceFamilyChanged.newRevision
                };
                orderedEvents.push_back(std::move(marker));
            } else {
                _latestSourceEvidence = LatestSourceEvidence{
                    .generation = ++_latestSourceGeneration,
                    .snapshot = record.sourceEvidence
                };
            }
        }
    }

    void IngressHub::PublishSourceEvidenceFrame(const presentation::SourceEvidenceFrame& frame)
    {
        std::scoped_lock lock(_mutex);
        std::vector<IngressEvent> orderedEvents;
        ApplySourceEvidenceFrameLocked(frame, orderedEvents);
        const auto available = _capacity > _queue.size() ? _capacity - _queue.size() : 0;
        if (orderedEvents.size() > available) {
            const auto overflowSeq = NextSeqLocked();
            const auto overflowTime = orderedEvents.empty() ? NowMonotonicUs() : orderedEvents.front().monotonicUs;
            ReplaceBacklogWithOverflowLocked(overflowSeq, overflowTime, orderedEvents);
            return;
        }
        for (auto& event : orderedEvents) {
            event.seq = NextSeqLocked();
            if (event.monotonicUs == 0) {
                event.monotonicUs = NowMonotonicUs();
            }
            _queue.push_back(std::move(event));
        }
    }

    void IngressHub::PushManifestEpochChanged(std::uint64_t manifestEpoch)
    {
        IngressEvent event{};
        event.kind = IngressKind::ManifestEpochChanged;
        event.source = IngressSource::ManifestPublisher;
        event.manifest.manifestEpoch = static_cast<std::uint32_t>(manifestEpoch);
        (void)PushEvent(std::move(event));
    }

    void IngressHub::PushSequenceGap()
    {
        (void)PushEvent(MakeSequenceGapEvent());
    }

    void IngressHub::PushExplicitReset()
    {
        (void)PushEvent(MakeExplicitResetEvent());
    }

    std::vector<IngressEvent> IngressHub::Drain()
    {
        return Drain(std::numeric_limits<std::size_t>::max());
    }

    std::vector<IngressEvent> IngressHub::Drain(std::size_t maxEvents)
    {
        std::scoped_lock lock(_mutex);
        return DrainLocked(maxEvents);
    }

    std::vector<IngressEvent> IngressHub::DrainLocked(std::size_t maxEvents)
    {
        const auto count = std::min(maxEvents, _queue.size());
        std::vector<IngressEvent> drained;
        drained.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            auto event = std::move(_queue.front());
            _queue.pop_front();
            _lastConsumedOrderedSeq = std::max(_lastConsumedOrderedSeq, event.seq);
            if (event.kind == IngressKind::PadSnapshot &&
                event.pad.legacySnapshot.has_value() &&
                _pendingLegacySnapshots != 0) {
                --_pendingLegacySnapshots;
            }
            drained.push_back(std::move(event));
        }
        return drained;
    }

    IngressCapture IngressHub::Capture(std::size_t maxEvents)
    {
        std::scoped_lock lock(_mutex);
        IngressCapture capture{
            .generation = ++_captureGeneration,
            .events = DrainLocked(maxEvents),
            .latestPadState = _latestPadState,
            .latestSourceEvidence = _latestSourceEvidence,
            .remainingEvents = _queue.size(),
            .orderedCutoffSeq = _lastConsumedOrderedSeq,
            .inputStateEpoch = _inputStateEpoch,
            .gamepadSessionId = _gamepadSessionId,
            .latestGamepadConnection = _latestGamepadConnection,
            .latestKbmGameplay = _latestKbmGameplay
        };
        _capturedPadGeneration = _latestPadGeneration;
        _capturedSourceGeneration = _latestSourceGeneration;
        _capturedGamepadConnectionGeneration = _latestGamepadConnectionGeneration;
        _capturedKbmGameplayGeneration = _latestKbmGameplayGeneration;
        return capture;
    }

    bool IngressHub::HasUncapturedLatest() const
    {
        std::scoped_lock lock(_mutex);
        return _latestPadGeneration > _capturedPadGeneration ||
            _latestSourceGeneration > _capturedSourceGeneration ||
            _latestGamepadConnectionGeneration > _capturedGamepadConnectionGeneration ||
            _latestKbmGameplayGeneration > _capturedKbmGameplayGeneration;
    }

    std::size_t IngressHub::PendingCount() const
    {
        std::scoped_lock lock(_mutex);
        return _queue.size();
    }

    std::size_t IngressHub::PendingLegacySnapshotCount() const
    {
        std::scoped_lock lock(_mutex);
        return _pendingLegacySnapshots;
    }

    void IngressHub::ResetForTests()
    {
        {
            std::scoped_lock lock(_mutex);
            _queue.clear();
            _nextSeq = 1;
            _lastAllocatedOrderedSeq = 0;
            _lastConsumedOrderedSeq = 0;
            _lastLegacySequence = 0;
            _latestPadGeneration = 0;
            _latestSourceGeneration = 0;
            _latestGamepadConnectionGeneration = 0;
            _latestKbmGameplayGeneration = 0;
            _captureGeneration = 0;
            _capturedPadGeneration = 0;
            _capturedSourceGeneration = 0;
            _capturedGamepadConnectionGeneration = 0;
            _capturedKbmGameplayGeneration = 0;
            _inputStateEpoch = 1;
            _gamepadSessionId = 0;
            _controlMapFingerprint = 0;
            _boundaryKey = {};
            _gamepadConnectivity = GamepadConnectivity::Disconnected;
            _pendingLegacySnapshots = 0;
            _previousDigitalMask = 0;
            _edgeHistoryLost = false;
            _digitalDownAtUs = {};
            _lastUiSnapshot.reset();
            _latestPadState.reset();
            _latestSourceEvidence.reset();
            _latestGamepadConnection.reset();
            _latestKbmGameplay.reset();
        }
        LiveInputFactProducer::GetSingleton().ResetForTests();
    }
}
