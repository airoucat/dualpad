#pragma once

#include <cstdint>

namespace dualpad::input
{
    std::uint16_t ToXInputButtons(std::uint32_t mask);
}
