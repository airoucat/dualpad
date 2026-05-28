#include "pch.h"
#include "input/protocol/DualSenseProtocol.h"

#include "input/protocol/DualSenseCommonFields.h"
#include "input/protocol/DualSenseButtons.h"
#include "input/protocol/DualSenseReportIds.h"
#include "input/RuntimeConfig.h"
#include "input/state/PadStateDebugger.h"

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        constexpr std::uint32_t kInterestingMenuBits =
            protocol::buttons::kCross |
            protocol::buttons::kCircle |
            protocol::buttons::kTriangle |
            protocol::buttons::kDpadUp |
            protocol::buttons::kDpadDown |
            protocol::buttons::kDpadLeft |
            protocol::buttons::kDpadRight;

        void MaybeLogRawButtons(
            const RawInputPacket& packet,
            std::uint8_t buttons0,
            std::uint8_t buttons1,
            std::uint8_t buttons2,
            std::uint8_t buttons3,
            std::uint32_t digitalMask)
        {
            if (!RuntimeConfig::GetSingleton().LogMappingEvents()) {
                return;
            }

            const auto dpadNibble = static_cast<std::uint8_t>(buttons0 & 0x0F);
            const bool interesting =
                (digitalMask & kInterestingMenuBits) != 0 ||
                dpadNibble != 0x08;
            if (!interesting) {
                return;
            }

            logger::debug(
                "[DualPad][MenuProbe] raw-buttons transport={} report=0x{:02X} seq={} buttons0=0x{:02X} buttons1=0x{:02X} buttons2=0x{:02X} buttons3=0x{:02X} dpadNibble=0x{:X} mask=0x{:08X}",
                ToString(packet.transport),
                packet.reportId,
                packet.sequence,
                buttons0,
                buttons1,
                buttons2,
                buttons3,
                dpadNibble,
                digitalMask);
        }

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
            state.parseCompleteness = ParseCompleteness::Partial;

            state.leftStick.rawX = packet.data[raw::kBt01LeftStickX];
            state.leftStick.rawY = packet.data[raw::kBt01LeftStickY];
            state.rightStick.rawX = packet.data[raw::kBt01RightStickX];
            state.rightStick.rawY = packet.data[raw::kBt01RightStickY];
            state.leftTrigger.raw = packet.data[raw::kBt01LeftTrigger];
            state.rightTrigger.raw = packet.data[raw::kBt01RightTrigger];

            // Bluetooth report 0x01 is intentionally kept as a gameplay-subset parser.
            // The current offsets preserve the validated minimal controls, but several
            // fields still need Windows-side capture verification before this path can
            // be treated as fully verified.
            // TODO(protocol verification): validate the 0x01 Windows Bluetooth layout
            // with real hardware logs before promoting this parser beyond partial support.
            const auto buttons0 = packet.data[raw::kBt01Buttons0];
            const auto buttons1 = packet.data[raw::kBt01Buttons1];
            const auto buttons2 = packet.data[raw::kBt01Buttons2];
            state.buttons = protocol::common::BuildPadButtons(
                buttons0,
                buttons1,
                buttons2,
                0);
            MaybeLogRawButtons(packet, buttons0, buttons1, buttons2, 0, state.buttons.digitalMask);

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

            const auto buttons0 = packet.data[raw::kBt31Buttons0];
            const auto buttons1 = packet.data[raw::kBt31Buttons1];
            const auto buttons2 = packet.data[raw::kBt31Buttons2];
            const auto buttons3 = packet.data[raw::kBt31Buttons3];
            state.buttons = protocol::common::BuildPadButtons(
                buttons0,
                buttons1,
                buttons2,
                buttons3);
            MaybeLogRawButtons(packet, buttons0, buttons1, buttons2, buttons3, state.buttons.digitalMask);

            state.imu.gyroX = protocol::common::ReadI16LE(packet.data + raw::kBt31GyroX);
            state.imu.gyroY = protocol::common::ReadI16LE(packet.data + raw::kBt31GyroY);
            state.imu.gyroZ = protocol::common::ReadI16LE(packet.data + raw::kBt31GyroZ);
            state.imu.accelX = protocol::common::ReadI16LE(packet.data + raw::kBt31AccelX);
            state.imu.accelY = protocol::common::ReadI16LE(packet.data + raw::kBt31AccelY);
            state.imu.accelZ = protocol::common::ReadI16LE(packet.data + raw::kBt31AccelZ);
            state.imu.valid = true;

            protocol::common::ApplyBattery(state, packet.data[raw::kBt31Status0], packet.data[raw::kBt31Status1]);
            state.touch1 = protocol::common::ParseTouchPoint(packet.data + raw::kBt31Touch1);
            state.touch2 = protocol::common::ParseTouchPoint(packet.data + raw::kBt31Touch2);

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
