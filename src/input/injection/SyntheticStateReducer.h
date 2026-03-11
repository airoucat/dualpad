#pragma once

#include "input/injection/PadEventSnapshot.h"
#include "input/injection/SyntheticPadFrame.h"

#include <array>
#include <cstdint>

namespace dualpad::input
{
    class SyntheticStateReducer
    {
    public:
        void Reset();

        const SyntheticPadFrame& Reduce(const PadEventSnapshot& snapshot, InputContext context);
        const SyntheticPadFrame& GetLatestFrame() const;

    private:
        SyntheticPadFrame _latest{};
        std::uint32_t _previousDownMask{ 0 };
        std::array<std::uint64_t, 32> _pressedAtUs{};
        std::array<std::uint64_t, 32> _releasedAtUs{};
        std::array<float, 6> _previousAxisValues{};

        void ReduceButtons(const PadEventSnapshot& snapshot);
        void ReduceAxes(const PadEventSnapshot& snapshot);
        void ReduceSemanticEvents(const PadEventSnapshot& snapshot);
    };
}
