#pragma once

#include "input/backend/NativeControlCode.h"

#include <cstdint>

namespace dualpad::input::backend
{
    std::uint16_t ResolveMappedGamepadButton(NativeControlCode control);
}
