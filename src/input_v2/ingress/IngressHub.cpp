#include "pch.h"

#include "input_v2/ingress/IngressHub.h"

#include "input_v2/ingress/LegacyIngressAdapter.h"
#include "input_v2/ingress/LiveInputFactProducer.h"

#include <chrono>

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
        _queue.clear();
        _queue.push_back(overflow);
    }

    bool IngressHub::PushPadSnapshot(const dualpad::input::PadEventSnapshot& snapshot)
    {
        std::scoped_lock lock(_mutex);
        auto converted = ConvertLegacySnapshotToIngressEvents(snapshot, _lastLegacySequence);

        const auto available = _capacity > _queue.size() ? _capacity - _queue.size() : 0;
        if (converted.size() > available) {
            const auto overflowSeq = NextSeqLocked();
            const auto overflowTime = snapshot.sourceTimestampUs != 0 ? snapshot.sourceTimestampUs : NowMonotonicUs();
            ReplaceBacklogWithOverflowLocked(overflowSeq, overflowTime, converted);
            // QueueOverflow represents a dropped legacy input range through this snapshot.
            // Advance the watermark so the next contiguous accepted snapshot does not
            // report a second SequenceGap for the same discarded range.
            if (snapshot.sequence != 0) {
                _lastLegacySequence = snapshot.sequence;
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
        ++_pendingLegacySnapshots;
        return true;
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
        std::scoped_lock lock(_mutex);
        auto drained = std::move(_queue);
        _queue.clear();
        _pendingLegacySnapshots = 0;
        return drained;
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
        std::scoped_lock lock(_mutex);
        _queue.clear();
        _nextSeq = 1;
        _lastLegacySequence = 0;
        _pendingLegacySnapshots = 0;
        LiveInputFactProducer::GetSingleton().ResetForTests();
    }
}
