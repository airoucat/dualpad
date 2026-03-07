#pragma once

#include <cstdint>

namespace dualpad::input::protocol::report
{
    inline constexpr std::uint8_t kUsbInput01 = 0x01;
    inline constexpr std::uint8_t kBtInput01 = 0x01;
    inline constexpr std::uint8_t kBtInput31 = 0x31;
}
