#include "pch.h"
#include "input/protocol/DualSenseProtocol.h"

#include "input/protocol/DualSenseCommonFields.h"
#include "input/protocol/DualSenseButtons.h"
#include "input/protocol/DualSenseReportIds.h"
#include "input/state/PadStateDebugger.h"

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

        }

        void ApplyTouchData(const RawInputPacket& packet, PadState& state)
        {
            if (packet.size > (raw::kTouch2 + 3)) {
                const auto touch1 = protocol::common::ParseTouchPoint(packet.data + raw::kTouch1);
                const auto touch2 = protocol::common::ParseTouchPoint(packet.data + raw::kTouch2);
                if (protocol::common::IsPlausibleTouchPoint(touch1) &&
                    protocol::common::IsPlausibleTouchPoint(touch2)) {
                    state.touch1 = touch1;
                    state.touch2 = touch2;
                    return;
                }
            }

            if (packet.size > (raw::kLegacyTouch2 + 3)) {
                state.touch1 = protocol::common::ParseTouchPoint(packet.data + raw::kLegacyTouch1);
                state.touch2 = protocol::common::ParseTouchPoint(packet.data + raw::kLegacyTouch2);
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
        state.buttons = protocol::common::BuildPadButtons(
            packet.data[raw::kButtons0],
            packet.data[raw::kButtons1],
            packet.data[raw::kButtons2],
            buttons3);

        if (packet.size > (raw::kAccelZ + 1)) {
            state.imu.gyroX = protocol::common::ReadI16LE(packet.data + raw::kGyroX);
            state.imu.gyroY = protocol::common::ReadI16LE(packet.data + raw::kGyroY);
            state.imu.gyroZ = protocol::common::ReadI16LE(packet.data + raw::kGyroZ);
            state.imu.accelX = protocol::common::ReadI16LE(packet.data + raw::kAccelX);
            state.imu.accelY = protocol::common::ReadI16LE(packet.data + raw::kAccelY);
            state.imu.accelZ = protocol::common::ReadI16LE(packet.data + raw::kAccelZ);
            state.imu.valid = true;
        }

        if (packet.size > raw::kStatus1) {
            protocol::common::ApplyBattery(state, packet.data[raw::kStatus0], packet.data[raw::kStatus1]);
        }

        ApplyTouchData(packet, state);

        outState = state;
        return true;
    }
}
