#include "pch.h"
#include "input/state/PadStateNormalizer.h"

namespace dualpad::input
{
    float NormalizeStickByte(std::uint8_t raw)
    {
        return (static_cast<float>(raw) - 127.5f) / 127.5f;
    }

    float NormalizeTriggerByte(std::uint8_t raw)
    {
        return static_cast<float>(raw) / 255.0f;
    }

    void NormalizePadState(PadState& state)
    {
        state.leftStick.x = NormalizeStickByte(state.leftStick.rawX);
        state.leftStick.y = -NormalizeStickByte(state.leftStick.rawY);
        state.rightStick.x = NormalizeStickByte(state.rightStick.rawX);
        state.rightStick.y = -NormalizeStickByte(state.rightStick.rawY);

        state.leftTrigger.normalized = NormalizeTriggerByte(state.leftTrigger.raw);
        state.rightTrigger.normalized = NormalizeTriggerByte(state.rightTrigger.raw);
    }
}
