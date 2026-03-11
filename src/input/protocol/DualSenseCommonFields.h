#pragma once

#include "input/state/PadState.h"

#include <cstdint>

namespace dualpad::input::protocol::common
{
    std::int16_t ReadI16LE(const std::uint8_t* data);
    TouchPointState ParseTouchPoint(const std::uint8_t* data);
    bool IsPlausibleTouchPoint(const TouchPointState& point);

    void ApplyDpad(std::uint32_t& digitalMask, std::uint8_t dpadNibble);
    std::uint32_t BuildDigitalMask(
        std::uint8_t buttons0,
        std::uint8_t buttons1,
        std::uint8_t buttons2,
        std::uint8_t buttons3);
    PadButtons BuildPadButtons(
        std::uint8_t buttons0,
        std::uint8_t buttons1,
        std::uint8_t buttons2,
        std::uint8_t buttons3);

    void ApplyBattery(PadState& state, std::uint8_t status0, std::uint8_t status1);
}
