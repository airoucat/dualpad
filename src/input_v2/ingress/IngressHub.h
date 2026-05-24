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
        void PushSequenceGap();
        void PushExplicitReset();
        std::vector<IngressEvent> Drain();
        void ResetForTests();

    private:
        std::uint64_t NextSeqLocked();
        std::uint64_t NowMonotonicUs() const;
        bool PushLocked(IngressEvent event);
        void ReplaceBacklogWithOverflowLocked();

        std::size_t _capacity{ 256 };
        std::uint64_t _nextSeq{ 1 };
        std::vector<IngressEvent> _queue;
        std::mutex _mutex;
    };
}
