#pragma once

#include "input/injection/PadEventSnapshot.h"
#include "input_v2/ingress/FrameAssembler.h"

namespace dualpad::input
{
    class PadEventSnapshotProcessor
    {
    public:
        static PadEventSnapshotProcessor& GetSingleton();

        void Process(const PadEventSnapshot& snapshot);
        void ProcessIngressFrame(const input_v2::ingress::AssembledFactFrame& frame);
        void ResetState();

    private:
        PadEventSnapshotProcessor() = default;
    };
}
