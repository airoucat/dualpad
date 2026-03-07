#include "pch.h"
#include "input/protocol/DualSenseProtocol.h"

#include "input/protocol/DualSenseButtons.h"
#include "input/protocol/DualSenseReportIds.h"
#include "input/state/PadStateDebugger.h"

#include <algorithm>

namespace dualpad::input
{
    namespace
    {
        namespace raw
        {
            inline constexpr std::size_t kMinPacketSize = 11;

            inline constexpr std::size_t kLeftStickX = 1;
            inline constexpr std::size_t kLeftStickY = 2;
            inline constexpr std::size_t kRightStickX = 3;
            inline constexpr std::size_t kRightStickY = 4;
            inline constexpr std::size_t kLeftTrigger = 5;
            inline constexpr std::size_t kRightTrigger = 6;
            inline constexpr std::size_t kButtons0 = 8;
            inline constexpr std::size_t kButtons1 = 9;
            inline constexpr std::size_t kButtons2 = 10;
            inline constexpr std::size_t kButtons3 = 11;

            inline constexpr std::size_t kGyroX = 16;
            inline constexpr std::size_t kGyroY = 18;
            inline constexpr std::size_t kGyroZ = 20;
            inline constexpr std::size_t kAccelX = 22;
            inline constexpr std::size_t kAccelY = 24;
            inline constexpr std::size_t kAccelZ = 26;
            inline constexpr std::size_t kStatus0 = 33;
            inline constexpr std::size_t kStatus1 = 34;
            inline constexpr std::size_t kTouch1 = 43;
            inline constexpr std::size_t kTouch2 = 47;

            // TODO(protocol verification): verify whether all Windows USB captures expose
            // touch data at the Linux hid-playstation offsets above. The legacy parser used
            // byte 33/37, so keep a fallback for malformed but still-plausible captures.
            inline constexpr std::size_t kLegacyTouch1 = 33;
            inline constexpr std::size_t kLegacyTouch2 = 37;

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

        void ApplyDpad(std::uint8_t buttons0, PadState& state)
        {
            const auto dpad = static_cast<std::uint8_t>(buttons0 & raw::kDpadMask);
            if (dpad == 0 || dpad == 1 || dpad == 7) state.buttons.digitalMask |= protocol::buttons::kDpadUp;
            if (dpad == 1 || dpad == 2 || dpad == 3) state.buttons.digitalMask |= protocol::buttons::kDpadRight;
            if (dpad == 3 || dpad == 4 || dpad == 5) state.buttons.digitalMask |= protocol::buttons::kDpadDown;
            if (dpad == 5 || dpad == 6 || dpad == 7) state.buttons.digitalMask |= protocol::buttons::kDpadLeft;
        }

        void ApplyButtons(std::uint8_t buttons0, std::uint8_t buttons1, std::uint8_t buttons2, std::uint8_t buttons3, PadState& state)
        {
            if (buttons0 & raw::kSquare) state.buttons.digitalMask |= protocol::buttons::kSquare;
            if (buttons0 & raw::kCross) state.buttons.digitalMask |= protocol::buttons::kCross;
            if (buttons0 & raw::kCircle) state.buttons.digitalMask |= protocol::buttons::kCircle;
            if (buttons0 & raw::kTriangle) state.buttons.digitalMask |= protocol::buttons::kTriangle;

            if (buttons1 & raw::kL1) state.buttons.digitalMask |= protocol::buttons::kL1;
            if (buttons1 & raw::kR1) state.buttons.digitalMask |= protocol::buttons::kR1;
            if (buttons1 & raw::kL2Button) state.buttons.digitalMask |= protocol::buttons::kL2Button;
            if (buttons1 & raw::kR2Button) state.buttons.digitalMask |= protocol::buttons::kR2Button;
            if (buttons1 & raw::kCreate) state.buttons.digitalMask |= protocol::buttons::kCreate;
            if (buttons1 & raw::kOptions) state.buttons.digitalMask |= protocol::buttons::kOptions;
            if (buttons1 & raw::kL3) state.buttons.digitalMask |= protocol::buttons::kL3;
            if (buttons1 & raw::kR3) state.buttons.digitalMask |= protocol::buttons::kR3;

            state.buttons.ps = (buttons2 & raw::kPS) != 0;
            state.buttons.touchpadClick = (buttons2 & raw::kTouchpadClick) != 0;
            state.buttons.mute = (buttons2 & raw::kMute) != 0;

            if (state.buttons.ps) state.buttons.digitalMask |= protocol::buttons::kPS;
            if (state.buttons.touchpadClick) state.buttons.digitalMask |= protocol::buttons::kTouchpadClick;
            if (state.buttons.mute) state.buttons.digitalMask |= protocol::buttons::kMute;

            if (buttons2 & raw::kExtraFnLeft || buttons3 & 0x01) state.buttons.digitalMask |= protocol::buttons::kFnLeft;
            if (buttons2 & raw::kExtraFnRight || buttons3 & 0x02) state.buttons.digitalMask |= protocol::buttons::kFnRight;
            if (buttons2 & raw::kExtraBackLeft || buttons3 & 0x04) state.buttons.digitalMask |= protocol::buttons::kBackLeft;
            if (buttons2 & raw::kExtraBackRight || buttons3 & 0x08) state.buttons.digitalMask |= protocol::buttons::kBackRight;

            ApplyDpad(buttons0, state);
        }

        void ApplyBattery(std::uint8_t status0, std::uint8_t status1, PadState& state)
        {
            const auto level = static_cast<std::uint8_t>((std::min)(static_cast<unsigned>(status0 & 0x0F), 10u));
            state.battery = static_cast<std::uint8_t>(level * 10);
            if (status1 & 0x20) {
                state.battery = 100;
            }
            state.batteryValid = true;
        }

        void ApplyTouchData(const RawInputPacket& packet, PadState& state)
        {
            if (packet.size > (raw::kTouch2 + 3)) {
                const auto touch1 = ParseTouchPoint(packet.data + raw::kTouch1);
                const auto touch2 = ParseTouchPoint(packet.data + raw::kTouch2);
                if (IsPlausibleTouchPoint(touch1) && IsPlausibleTouchPoint(touch2)) {
                    state.touch1 = touch1;
                    state.touch2 = touch2;
                    return;
                }
            }

            if (packet.size > (raw::kLegacyTouch2 + 3)) {
                state.touch1 = ParseTouchPoint(packet.data + raw::kLegacyTouch1);
                state.touch2 = ParseTouchPoint(packet.data + raw::kLegacyTouch2);
            }
        }
    }

    bool ParseDualSenseUsbInputPacket(const RawInputPacket& packet, PadState& outState)
    {
        if (!packet.data || packet.size < raw::kMinPacketSize) {
            LogParseFailure(packet, "USB report too short");
            return false;
        }

        if (packet.reportId != protocol::report::kUsbInput01) {
            LogParseFailure(packet, "Unsupported USB report id");
            return false;
        }

        PadState state{};
        state.connected = true;
        state.transport = TransportType::USB;
        state.reportId = packet.reportId;
        state.timestampUs = packet.timestampUs;
        state.sequence = packet.sequence;

        state.leftStick.rawX = packet.data[raw::kLeftStickX];
        state.leftStick.rawY = packet.data[raw::kLeftStickY];
        state.rightStick.rawX = packet.data[raw::kRightStickX];
        state.rightStick.rawY = packet.data[raw::kRightStickY];
        state.leftTrigger.raw = packet.data[raw::kLeftTrigger];
        state.rightTrigger.raw = packet.data[raw::kRightTrigger];

        const std::uint8_t buttons3 = packet.size > raw::kButtons3 ? packet.data[raw::kButtons3] : 0;
        ApplyButtons(
            packet.data[raw::kButtons0],
            packet.data[raw::kButtons1],
            packet.data[raw::kButtons2],
            buttons3,
            state);

        if (packet.size > (raw::kAccelZ + 1)) {
            state.imu.gyroX = ReadI16LE(packet.data + raw::kGyroX);
            state.imu.gyroY = ReadI16LE(packet.data + raw::kGyroY);
            state.imu.gyroZ = ReadI16LE(packet.data + raw::kGyroZ);
            state.imu.accelX = ReadI16LE(packet.data + raw::kAccelX);
            state.imu.accelY = ReadI16LE(packet.data + raw::kAccelY);
            state.imu.accelZ = ReadI16LE(packet.data + raw::kAccelZ);
            state.imu.valid = true;
        }

        if (packet.size > raw::kStatus1) {
            ApplyBattery(packet.data[raw::kStatus0], packet.data[raw::kStatus1], state);
        }

        ApplyTouchData(packet, state);

        outState = state;
        return true;
    }
}
