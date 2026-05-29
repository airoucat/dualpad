#include "pch.h"

#include "input_v2/ingress/IngressHub.h"

#include "input_v2/ingress/LegacyIngressAdapter.h"
#include "input_v2/ingress/LiveInputFactProducer.h"

#include <chrono>

namespace dualpad::input_v2::ingress
{
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
            ReplaceBacklogWithOverflowLocked(event.seq, event.monotonicUs);
            return false;
        }
        _queue.push_back(std::move(event));
        return true;
    }

    bool IngressHub::PushLocked(IngressEvent event)
    {
        if (_queue.size() >= _capacity) {
            ReplaceBacklogWithOverflowLocked(event.seq, event.monotonicUs);
            return false;
        }
        _queue.push_back(std::move(event));
        return true;
    }

    void IngressHub::ReplaceBacklogWithOverflowLocked(std::uint64_t seq, std::uint64_t monotonicUs)
    {
        IngressEvent overflow = MakeQueueOverflowEvent();
        overflow.seq = seq;
        overflow.monotonicUs = monotonicUs != 0 ? monotonicUs : NowMonotonicUs();
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
            ReplaceBacklogWithOverflowLocked(overflowSeq, overflowTime);
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
