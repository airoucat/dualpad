#pragma once

#include "input/injection/PadEventSnapshot.h"
#include "input_v2/ingress/IngressMarkers.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace dualpad::input_v2::ingress
{
    class IngressHub
    {
    public:
        explicit IngressHub(std::size_t capacity = 256);

        static IngressHub& GetSingleton();

        bool PushEvent(IngressEvent event);
        bool PushPadSnapshot(const dualpad::input::PadEventSnapshot& snapshot);
        void PushManifestEpochChanged(std::uint64_t manifestEpoch);
        void PushSequenceGap();
        void PushExplicitReset();
        std::vector<IngressEvent> Drain();
        std::size_t PendingCount() const;
        std::size_t PendingLegacySnapshotCount() const;
        void ResetForTests();

    private:
        std::uint64_t NextSeqLocked();
        std::uint64_t NowMonotonicUs() const;
        bool PushLocked(IngressEvent event);
        void ReplaceBacklogWithOverflowLocked(
            std::uint64_t seq,
            std::uint64_t monotonicUs,
            const std::vector<IngressEvent>& incomingEvents = {});

        std::size_t _capacity{ 256 };
        std::uint64_t _nextSeq{ 1 };
        std::uint64_t _lastLegacySequence{ 0 };
        std::size_t _pendingLegacySnapshots{ 0 };
        std::vector<IngressEvent> _queue;
        mutable std::mutex _mutex;
    };
}
