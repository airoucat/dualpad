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

        enum class TouchDataLayout
        {
            None,
            Main,
            Legacy
        };

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

        }

        TouchDataLayout ApplyTouchData(const RawInputPacket& packet, PadState& state)
        {
            if (packet.size > (raw::kTouch2 + 3)) {
                const auto touch1 = protocol::common::ParseTouchPoint(packet.data + raw::kTouch1);
                const auto touch2 = protocol::common::ParseTouchPoint(packet.data + raw::kTouch2);
                if (protocol::common::IsPlausibleTouchPoint(touch1) &&
                    protocol::common::IsPlausibleTouchPoint(touch2)) {
                    state.touch1 = touch1;
                    state.touch2 = touch2;
                    return TouchDataLayout::Main;
                }
            }

            if (packet.size > (raw::kLegacyTouch2 + 3)) {
                const auto touch1 = protocol::common::ParseTouchPoint(packet.data + raw::kLegacyTouch1);
                const auto touch2 = protocol::common::ParseTouchPoint(packet.data + raw::kLegacyTouch2);
                if (protocol::common::IsPlausibleTouchPoint(touch1) &&
                    protocol::common::IsPlausibleTouchPoint(touch2)) {
                    state.touch1 = touch1;
                    state.touch2 = touch2;
                    return TouchDataLayout::Legacy;
                }
            }

            return TouchDataLayout::None;
        }

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

        const auto buttons0 = packet.data[raw::kButtons0];
        const auto buttons1 = packet.data[raw::kButtons1];
        const auto buttons2 = packet.data[raw::kButtons2];
        const std::uint8_t buttons3 = packet.size > raw::kButtons3 ? packet.data[raw::kButtons3] : 0;
        state.buttons = protocol::common::BuildPadButtons(
            buttons0,
            buttons1,
            buttons2,
            buttons3);
        MaybeLogRawButtons(packet, buttons0, buttons1, buttons2, buttons3, state.buttons.digitalMask);

        if (packet.size > (raw::kAccelZ + 1)) {
            state.imu.gyroX = protocol::common::ReadI16LE(packet.data + raw::kGyroX);
            state.imu.gyroY = protocol::common::ReadI16LE(packet.data + raw::kGyroY);
            state.imu.gyroZ = protocol::common::ReadI16LE(packet.data + raw::kGyroZ);
            state.imu.accelX = protocol::common::ReadI16LE(packet.data + raw::kAccelX);
            state.imu.accelY = protocol::common::ReadI16LE(packet.data + raw::kAccelY);
            state.imu.accelZ = protocol::common::ReadI16LE(packet.data + raw::kAccelZ);
            state.imu.valid = true;
        }

        const auto touchLayout = ApplyTouchData(packet, state);

        // The legacy touch fallback overlaps the bytes currently used for battery
        // parsing. When that legacy layout wins, prefer touch correctness and skip
        // battery decoding rather than reading touch bytes as fake battery data.
        if (packet.size > raw::kStatus1 && touchLayout != TouchDataLayout::Legacy) {
            protocol::common::ApplyBattery(state, packet.data[raw::kStatus0], packet.data[raw::kStatus1]);
        }

        outState = state;
        return true;
    }
}
