#pragma once

#include "input/state/PadState.h"

namespace dualpad::input
{
    float NormalizeStickByte(std::uint8_t raw);
    float NormalizeTriggerByte(std::uint8_t raw);

    // Keeps protocol parsing free of response-curve policy so deadzones can evolve later.
    void NormalizePadState(PadState& state);
}
