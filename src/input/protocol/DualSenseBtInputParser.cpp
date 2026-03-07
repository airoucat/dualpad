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

            inline constexpr std::size_t kBt01LeftStickX = 1;
            inline constexpr std::size_t kBt01LeftStickY = 2;
            inline constexpr std::size_t kBt01RightStickX = 3;
            inline constexpr std::size_t kBt01RightStickY = 4;
            inline constexpr std::size_t kBt01LeftTrigger = 5;
            inline constexpr std::size_t kBt01RightTrigger = 6;
            inline constexpr std::size_t kBt01Buttons0 = 8;
            inline constexpr std::size_t kBt01Buttons1 = 9;
            inline constexpr std::size_t kBt01Buttons2 = 10;

            inline constexpr std::size_t kBt31LeftStickX = 2;
            inline constexpr std::size_t kBt31LeftStickY = 3;
            inline constexpr std::size_t kBt31RightStickX = 4;
            inline constexpr std::size_t kBt31RightStickY = 5;
            inline constexpr std::size_t kBt31LeftTrigger = 6;
            inline constexpr std::size_t kBt31RightTrigger = 7;
            inline constexpr std::size_t kBt31Buttons0 = 9;
            inline constexpr std::size_t kBt31Buttons1 = 10;
            inline constexpr std::size_t kBt31Buttons2 = 11;
            inline constexpr std::size_t kBt31Buttons3 = 12;

            inline constexpr std::size_t kBt31GyroX = 17;
            inline constexpr std::size_t kBt31GyroY = 19;
            inline constexpr std::size_t kBt31GyroZ = 21;
            inline constexpr std::size_t kBt31AccelX = 23;
            inline constexpr std::size_t kBt31AccelY = 25;
            inline constexpr std::size_t kBt31AccelZ = 27;
            inline constexpr std::size_t kBt31Status0 = 34;
            inline constexpr std::size_t kBt31Status1 = 35;
            inline constexpr std::size_t kBt31Touch1 = 44;
            inline constexpr std::size_t kBt31Touch2 = 48;

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

        bool ParseBt01(const RawInputPacket& packet, PadState& outState)
        {
            if (packet.size < raw::kMinPacketSize) {
                LogParseFailure(packet, "Bluetooth report 0x01 too short");
                return false;
            }

            PadState state{};
            state.connected = true;
            state.transport = TransportType::Bluetooth;
            state.reportId = packet.reportId;
            state.timestampUs = packet.timestampUs;
            state.sequence = packet.sequence;

            state.leftStick.rawX = packet.data[raw::kBt01LeftStickX];
            state.leftStick.rawY = packet.data[raw::kBt01LeftStickY];
            state.rightStick.rawX = packet.data[raw::kBt01RightStickX];
            state.rightStick.rawY = packet.data[raw::kBt01RightStickY];
            state.leftTrigger.raw = packet.data[raw::kBt01LeftTrigger];
            state.rightTrigger.raw = packet.data[raw::kBt01RightTrigger];

            // TODO(protocol verification): verify 0x01 Bluetooth captures from Windows
            // against real hardware logs; current offsets intentionally mirror the common
            // gameplay subset that is stable across known community parsers.
            ApplyButtons(
                packet.data[raw::kBt01Buttons0],
                packet.data[raw::kBt01Buttons1],
                packet.data[raw::kBt01Buttons2],
                0,
                state);

            outState = state;
            return true;
        }

        bool ParseBt31(const RawInputPacket& packet, PadState& outState)
        {
            if (packet.size <= raw::kBt31Touch2 + 3) {
                LogParseFailure(packet, "Bluetooth report 0x31 too short");
                return false;
            }

            PadState state{};
            state.connected = true;
            state.transport = TransportType::Bluetooth;
            state.reportId = packet.reportId;
            state.timestampUs = packet.timestampUs;
            state.sequence = packet.sequence;

            state.leftStick.rawX = packet.data[raw::kBt31LeftStickX];
            state.leftStick.rawY = packet.data[raw::kBt31LeftStickY];
            state.rightStick.rawX = packet.data[raw::kBt31RightStickX];
            state.rightStick.rawY = packet.data[raw::kBt31RightStickY];
            state.leftTrigger.raw = packet.data[raw::kBt31LeftTrigger];
            state.rightTrigger.raw = packet.data[raw::kBt31RightTrigger];

            ApplyButtons(
                packet.data[raw::kBt31Buttons0],
                packet.data[raw::kBt31Buttons1],
                packet.data[raw::kBt31Buttons2],
                packet.data[raw::kBt31Buttons3],
                state);

            state.imu.gyroX = ReadI16LE(packet.data + raw::kBt31GyroX);
            state.imu.gyroY = ReadI16LE(packet.data + raw::kBt31GyroY);
            state.imu.gyroZ = ReadI16LE(packet.data + raw::kBt31GyroZ);
            state.imu.accelX = ReadI16LE(packet.data + raw::kBt31AccelX);
            state.imu.accelY = ReadI16LE(packet.data + raw::kBt31AccelY);
            state.imu.accelZ = ReadI16LE(packet.data + raw::kBt31AccelZ);
            state.imu.valid = true;

            ApplyBattery(packet.data[raw::kBt31Status0], packet.data[raw::kBt31Status1], state);
            state.touch1 = ParseTouchPoint(packet.data + raw::kBt31Touch1);
            state.touch2 = ParseTouchPoint(packet.data + raw::kBt31Touch2);

            outState = state;
            return true;
        }
    }

    bool ParseDualSenseBtInputPacket(const RawInputPacket& packet, PadState& outState)
    {
        switch (packet.reportId) {
        case protocol::report::kBtInput01:
            return ParseBt01(packet, outState);
        case protocol::report::kBtInput31:
            return ParseBt31(packet, outState);
        default:
            LogParseFailure(packet, "Unsupported Bluetooth report id");
            return false;
        }
    }
}
