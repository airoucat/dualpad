#include "pch.h"
#include "input/injection/AxisProjection.h"

namespace dualpad::input
{
    ProjectedAnalogState CollectBoundAnalogState(const SyntheticPadFrame& frame, InputContext context)
    {
        (void)context;
        ProjectedAnalogState analog{};
        analog.moveX = frame.leftStickX.value;
        analog.moveY = frame.leftStickY.value;
        analog.lookX = frame.rightStickX.value;
        analog.lookY = frame.rightStickY.value;
        analog.leftTrigger = frame.leftTrigger.value;
        analog.rightTrigger = frame.rightTrigger.value;
        analog.hasAnalog = true;
        return analog;
    }
}
