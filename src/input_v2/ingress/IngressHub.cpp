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
        return _nextSeq++;
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
            .remainingEvents = _queue.size()
        };
        _capturedPadGeneration = _latestPadGeneration;
        _capturedSourceGeneration = _latestSourceGeneration;
        return capture;
    }

    bool IngressHub::HasUncapturedLatest() const
    {
        std::scoped_lock lock(_mutex);
        return _latestPadGeneration > _capturedPadGeneration ||
            _latestSourceGeneration > _capturedSourceGeneration;
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
            _lastLegacySequence = 0;
            _latestPadGeneration = 0;
            _latestSourceGeneration = 0;
            _captureGeneration = 0;
            _capturedPadGeneration = 0;
            _capturedSourceGeneration = 0;
            _pendingLegacySnapshots = 0;
            _previousDigitalMask = 0;
            _edgeHistoryLost = false;
            _digitalDownAtUs = {};
            _lastUiSnapshot.reset();
            _latestPadState.reset();
            _latestSourceEvidence.reset();
        }
        LiveInputFactProducer::GetSingleton().ResetForTests();
    }
}
