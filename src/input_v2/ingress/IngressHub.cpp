#include "pch.h"

#include "input_v2/ingress/IngressHub.h"

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
            ReplaceBacklogWithOverflowLocked();
            return false;
        }
        _queue.push_back(std::move(event));
        return true;
    }

    bool IngressHub::PushLocked(IngressEvent event)
    {
        if (_queue.size() >= _capacity) {
            ReplaceBacklogWithOverflowLocked();
            return false;
        }
        _queue.push_back(std::move(event));
        return true;
    }

    void IngressHub::ReplaceBacklogWithOverflowLocked()
    {
        IngressEvent overflow = MakeQueueOverflowEvent();
        overflow.seq = _nextSeq - 1;
        overflow.monotonicUs = NowMonotonicUs();
        _queue.clear();
        _queue.push_back(overflow);
    }

    bool IngressHub::PushPadSnapshot(const dualpad::input::PadEventSnapshot& snapshot)
    {
        IngressEvent event{};
        event.kind = snapshot.type == dualpad::input::PadEventSnapshotType::Reset ?
            IngressKind::ExplicitReset :
            IngressKind::PadSnapshot;
        event.source = IngressSource::LegacyDispatcher;
        event.monotonicUs = snapshot.sourceTimestampUs;
        if (snapshot.overflowed) {
            event.kind = IngressKind::QueueOverflow;
        } else if (snapshot.firstSequence != 0 && snapshot.sequence != 0 && snapshot.firstSequence > snapshot.sequence) {
            event.kind = IngressKind::SequenceGap;
        }
        return PushEvent(std::move(event));
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
        return drained;
    }

    void IngressHub::ResetForTests()
    {
        std::scoped_lock lock(_mutex);
        _queue.clear();
        _nextSeq = 1;
    }
}
