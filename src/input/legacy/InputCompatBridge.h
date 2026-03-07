#pragma once

#include "input/state/PadState.h"

#include <cstdint>

namespace dualpad::input
{
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

    std::uint32_t BuildLegacyButtonMask(const PadState& state);
    CompatFrame BuildCompatFrame(const PadState& state);
}
