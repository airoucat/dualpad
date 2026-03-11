#pragma once

#include "input/backend/FrameActionPlan.h"
#include "input/backend/VirtualGamepadState.h"

namespace dualpad::input::backend
{
    void LogFrameActionPlan(const FrameActionPlan& plan);
    void LogVirtualGamepadState(const VirtualGamepadState& state);
}
