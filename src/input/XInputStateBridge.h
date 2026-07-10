#pragma once

#include "input_v2/gameplay/PollOutputFrame.h"

#include <cstdint>

namespace dualpad::input
{
    std::uint32_t FillSyntheticXInputState(
        void* pState,
        const input_v2::gameplay::PollOutputFrame& frame);
}
