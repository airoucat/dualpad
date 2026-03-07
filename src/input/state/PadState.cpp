#include "pch.h"
#include "input/state/PadState.h"

namespace dualpad::input
{
    bool HasTouchData(const PadState& state)
    {
        return state.touch1.active || state.touch2.active;
    }

    bool HasImuData(const PadState& state)
    {
        return state.imu.valid;
    }
}
