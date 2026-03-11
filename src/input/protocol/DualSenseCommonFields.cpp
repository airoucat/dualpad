#include "pch.h"
#include "input/protocol/DualSenseCommonFields.h"

#include "input/protocol/DualSenseButtons.h"

#include <algorithm>

namespace dualpad::input::protocol::common
{
    namespace
    {
        inline constexpr std::uint8_t kDpadMask = 0x0F;
        inline constexpr std::uint8_t kSquare = 0x10;
        inline constexpr std::uint8_t kCross = 0x20;
        inline constexpr std::uint8_t kCircle = 0x40;
        inline constexpr std::uint8_t kTriangle = 0x80;

        inline constexpr std::uint8_t kL1 = 0x01;
        inline constexpr std::uint8_t kR1 = 0x02;
        inline constexpr std::uint8_t kL2Button = 0x04;
        inline constexpr std::uint8_t kR2Button = 0x08;
        inline constexpr std::uint8_t kCreate = 0x10;
        inline constexpr std::uint8_t kOptions = 0x20;
        inline constexpr std::uint8_t kL3 = 0x40;
        inline constexpr std::uint8_t kR3 = 0x80;

        inline constexpr std::uint8_t kPS = 0x01;
        inline constexpr std::uint8_t kTouchpadClick = 0x02;
        inline constexpr std::uint8_t kMute = 0x04;

        inline constexpr std::uint8_t kExtraFnLeft = 0x10;
        inline constexpr std::uint8_t kExtraFnRight = 0x20;
        inline constexpr std::uint8_t kExtraBackLeft = 0x40;
        inline constexpr std::uint8_t kExtraBackRight = 0x80;
    }

    std::int16_t ReadI16LE(const std::uint8_t* data)
    {
        return static_cast<std::int16_t>(
            static_cast<std::uint16_t>(data[0]) |
            (static_cast<std::uint16_t>(data[1]) << 8));
    }

    TouchPointState ParseTouchPoint(const std::uint8_t* data)
    {
        TouchPointState point{};
        const std::uint8_t b0 = data[0];
        point.active = (b0 & 0x80) == 0;
        point.id = static_cast<std::uint8_t>(b0 & 0x7F);
        point.x = static_cast<std::uint16_t>(data[1] | ((data[2] & 0x0F) << 8));
        point.y = static_cast<std::uint16_t>(((data[2] & 0xF0) >> 4) | (data[3] << 4));
        return point;
    }

    bool IsPlausibleTouchPoint(const TouchPointState& point)
    {
        return !point.active || (point.x <= 1919 && point.y <= 1079);
    }

    void ApplyDpad(std::uint32_t& digitalMask, std::uint8_t dpadNibble)
    {
        const auto dpad = static_cast<std::uint8_t>(dpadNibble & kDpadMask);
        if (dpad == 0 || dpad == 1 || dpad == 7) digitalMask |= buttons::kDpadUp;
        if (dpad == 1 || dpad == 2 || dpad == 3) digitalMask |= buttons::kDpadRight;
        if (dpad == 3 || dpad == 4 || dpad == 5) digitalMask |= buttons::kDpadDown;
        if (dpad == 5 || dpad == 6 || dpad == 7) digitalMask |= buttons::kDpadLeft;
    }

    std::uint32_t BuildDigitalMask(
        std::uint8_t buttons0,
        std::uint8_t buttons1,
        std::uint8_t buttons2,
        std::uint8_t buttons3)
    {
        std::uint32_t mask = 0;

        if (buttons0 & kSquare) mask |= buttons::kSquare;
        if (buttons0 & kCross) mask |= buttons::kCross;
        if (buttons0 & kCircle) mask |= buttons::kCircle;
        if (buttons0 & kTriangle) mask |= buttons::kTriangle;

        if (buttons1 & kL1) mask |= buttons::kL1;
        if (buttons1 & kR1) mask |= buttons::kR1;
        if (buttons1 & kL2Button) mask |= buttons::kL2Button;
        if (buttons1 & kR2Button) mask |= buttons::kR2Button;
        if (buttons1 & kCreate) mask |= buttons::kCreate;
        if (buttons1 & kOptions) mask |= buttons::kOptions;
        if (buttons1 & kL3) mask |= buttons::kL3;
        if (buttons1 & kR3) mask |= buttons::kR3;

        if (buttons2 & kPS) mask |= buttons::kPS;
        if (buttons2 & kTouchpadClick) mask |= buttons::kTouchpadClick;
        if (buttons2 & kMute) mask |= buttons::kMute;

        if (buttons2 & kExtraFnLeft || buttons3 & 0x01) mask |= buttons::kFnLeft;
        if (buttons2 & kExtraFnRight || buttons3 & 0x02) mask |= buttons::kFnRight;
        if (buttons2 & kExtraBackLeft || buttons3 & 0x04) mask |= buttons::kBackLeft;
        if (buttons2 & kExtraBackRight || buttons3 & 0x08) mask |= buttons::kBackRight;

        ApplyDpad(mask, buttons0);
        return mask;
    }

    PadButtons BuildPadButtons(
        std::uint8_t buttons0,
        std::uint8_t buttons1,
        std::uint8_t buttons2,
        std::uint8_t buttons3)
    {
        PadButtons buttonsState{};
        buttonsState.digitalMask = BuildDigitalMask(buttons0, buttons1, buttons2, buttons3);
        buttonsState.ps = (buttons2 & kPS) != 0;
        buttonsState.touchpadClick = (buttons2 & kTouchpadClick) != 0;
        buttonsState.mute = (buttons2 & kMute) != 0;
        return buttonsState;
    }

    void ApplyBattery(PadState& state, std::uint8_t status0, std::uint8_t status1)
    {
        const auto level = static_cast<std::uint8_t>((std::min)(static_cast<unsigned>(status0 & 0x0F), 10u));
        state.battery = static_cast<std::uint8_t>(level * 10);
        if (status1 & 0x20) {
            state.battery = 100;
        }
        state.batteryValid = true;
    }
}
