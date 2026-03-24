#pragma once

#include "input/InputContext.h"
#include "input/injection/SyntheticPadFrame.h"

namespace dualpad::input
{
    struct ProjectedAnalogState
    {
        float moveX{ 0.0f };
        float moveY{ 0.0f };
        float lookX{ 0.0f };
        float lookY{ 0.0f };
        float leftTrigger{ 0.0f };
        float rightTrigger{ 0.0f };
        bool hasAnalog{ false };
    };

    ProjectedAnalogState CollectBoundAnalogState(const SyntheticPadFrame& frame, InputContext context);
}
