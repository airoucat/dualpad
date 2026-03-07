#include "pch.h"
#include "input/legacy/InputCompatBridge.h"

namespace dualpad::input
{
    std::uint32_t BuildLegacyButtonMask(const PadState& state)
    {
        return state.buttons.digitalMask;
    }

    CompatFrame BuildCompatFrame(const PadState& state)
    {
        CompatFrame frame{};
        frame.buttonMask = BuildLegacyButtonMask(state);
        frame.lx = state.leftStick.x;
        frame.ly = state.leftStick.y;
        frame.rx = state.rightStick.x;
        frame.ry = state.rightStick.y;
        frame.l2 = state.leftTrigger.normalized;
        frame.r2 = state.rightTrigger.normalized;
        frame.hasAxis = state.connected;
        return frame;
    }
}
