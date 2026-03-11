#pragma once

#include "input/state/PadState.h"

#include <cstdint>

namespace dualpad::input
{
    // Transitional compatibility frame for the pre-refactor upper layer. Keep this
    // deliberately narrow: it forwards only the legacy button mask and axis subset
    // consumed by the current XInput shim. Touch2, IMU, battery, transport metadata,
    // and parse completeness intentionally stay below this bridge for now.
    struct CompatFrame
    {
        std::uint32_t buttonMask{ 0 };
        float lx{ 0.0f };
        float ly{ 0.0f };
        float rx{ 0.0f };
        float ry{ 0.0f };
        float l2{ 0.0f };
        float r2{ 0.0f };
        bool hasAxis{ false };
    };

    // Temporary bridge for existing consumers. This is not the long-term semantic API.
    std::uint32_t BuildLegacyButtonMask(const PadState& state);
    CompatFrame BuildCompatFrame(const PadState& state);
}
