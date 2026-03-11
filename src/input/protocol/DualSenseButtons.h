#pragma once

#include <cstdint>

namespace dualpad::input::protocol::buttons
{
    // These semantic bits intentionally match the legacy compatibility mask so the
    // upper layers can transition without changing their current expectations.
    inline constexpr std::uint32_t kSquare = 0x00000001;
    inline constexpr std::uint32_t kCross = 0x00000002;
    inline constexpr std::uint32_t kCircle = 0x00000004;
    inline constexpr std::uint32_t kTriangle = 0x00000008;

    inline constexpr std::uint32_t kL1 = 0x00000010;
    inline constexpr std::uint32_t kR1 = 0x00000020;
    inline constexpr std::uint32_t kL2Button = 0x00000040;
    inline constexpr std::uint32_t kR2Button = 0x00000080;

    inline constexpr std::uint32_t kCreate = 0x00000100;
    inline constexpr std::uint32_t kOptions = 0x00000200;
    inline constexpr std::uint32_t kL3 = 0x00000400;
    inline constexpr std::uint32_t kR3 = 0x00000800;

    inline constexpr std::uint32_t kPS = 0x00001000;
    inline constexpr std::uint32_t kMute = 0x00002000;
    inline constexpr std::uint32_t kTouchpadClick = 0x00004000;

    inline constexpr std::uint32_t kDpadUp = 0x00010000;
    inline constexpr std::uint32_t kDpadDown = 0x00020000;
    inline constexpr std::uint32_t kDpadLeft = 0x00040000;
    inline constexpr std::uint32_t kDpadRight = 0x00080000;

    inline constexpr std::uint32_t kFnLeft = 0x00100000;
    inline constexpr std::uint32_t kFnRight = 0x00200000;
    inline constexpr std::uint32_t kBackLeft = 0x00400000;
    inline constexpr std::uint32_t kBackRight = 0x00800000;
}
