#include "pch.h"
#include "input/state/PadStateDebugger.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        struct DebugConfig
        {
            bool packetSummary{ false };
            bool packetHexDump{ false };
            bool stateSummary{ false };
            bool parseFailure{ true };
        };

        bool EnvEnabled(const char* name)
        {
            char* value = nullptr;
            std::size_t length = 0;
            if (_dupenv_s(&value, &length, name) != 0 || !value || value[0] == '\0') {
                std::free(value);
                return false;
            }

            const bool enabled = value[0] != '0';
            std::free(value);
            return enabled;
        }

        const DebugConfig& GetDebugConfig()
        {
            static const DebugConfig config{
                .packetSummary = EnvEnabled("DUALPAD_LOG_INPUT_PACKETS"),
                .packetHexDump = EnvEnabled("DUALPAD_LOG_INPUT_HEX"),
                .stateSummary = EnvEnabled("DUALPAD_LOG_INPUT_STATE"),
                .parseFailure = true
            };

            return config;
        }

        std::string_view BoolString(bool value)
        {
            return value ? "1" : "0";
        }
    }

    std::uint64_t CaptureInputTimestampUs()
    {
        using namespace std::chrono;
        return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
    }

    bool IsInputDebugLogEnabled(InputDebugLog log)
    {
        const auto& config = GetDebugConfig();
        switch (log) {
        case InputDebugLog::PacketSummary:
            return config.packetSummary;
        case InputDebugLog::PacketHexDump:
            return config.packetHexDump;
        case InputDebugLog::StateSummary:
            return config.stateSummary;
        case InputDebugLog::ParseFailure:
            return config.parseFailure;
        default:
            return false;
        }
    }

    void LogPacketSummary(const RawInputPacket& packet)
    {
        if (!IsInputDebugLogEnabled(InputDebugLog::PacketSummary)) {
            return;
        }

        logger::debug(
            "[DualPad][Input][Packet] transport={} report=0x{:02X} size={} seq={} tsUs={}",
            ToString(packet.transport),
            packet.reportId,
            packet.size,
            packet.sequence,
            packet.timestampUs);
    }

    void LogPacketHexDump(const RawInputPacket& packet)
    {
        if (!IsInputDebugLogEnabled(InputDebugLog::PacketHexDump) || !packet.data || packet.size == 0) {
            return;
        }

        constexpr char kHex[] = "0123456789ABCDEF";
        std::array<char, 128 * 3 + 1> buffer{};

        const std::size_t clampedSize = (std::min)(packet.size, static_cast<std::size_t>(128));
        std::size_t cursor = 0;
        for (std::size_t i = 0; i < clampedSize && (cursor + 2) < buffer.size(); ++i) {
            const auto byte = packet.data[i];
            buffer[cursor++] = kHex[(byte >> 4) & 0x0F];
            buffer[cursor++] = kHex[byte & 0x0F];
            if ((cursor + 1) < buffer.size()) {
                buffer[cursor++] = ' ';
            }
        }

        if (cursor > 0) {
            buffer[cursor - 1] = '\0';
        }

        logger::debug(
            "[DualPad][Input][Hex] transport={} report=0x{:02X} size={} data={}",
            ToString(packet.transport),
            packet.reportId,
            packet.size,
            buffer.data());
    }

    void LogStateSummary(const PadState& state)
    {
        if (!IsInputDebugLogEnabled(InputDebugLog::StateSummary)) {
            return;
        }

        logger::debug(
            "[DualPad][Input][State] transport={} report=0x{:02X} mask=0x{:08X} ls=({:.3f},{:.3f}) rs=({:.3f},{:.3f}) lt={:.3f} rt={:.3f} tp1={}({},{}) tp2={}({},{}) imu={} battery={} valid={}",
            ToString(state.transport),
            state.reportId,
            state.buttons.digitalMask,
            state.leftStick.x,
            state.leftStick.y,
            state.rightStick.x,
            state.rightStick.y,
            state.leftTrigger.normalized,
            state.rightTrigger.normalized,
            BoolString(state.touch1.active),
            state.touch1.x,
            state.touch1.y,
            BoolString(state.touch2.active),
            state.touch2.x,
            state.touch2.y,
            BoolString(state.imu.valid),
            state.battery,
            BoolString(state.batteryValid));
    }

    void LogParseFailure(const RawInputPacket& packet, std::string_view reason)
    {
        if (!IsInputDebugLogEnabled(InputDebugLog::ParseFailure)) {
            return;
        }

        logger::warn(
            "[DualPad][Input][Parse] {} transport={} report=0x{:02X} size={} seq={}",
            reason,
            ToString(packet.transport),
            packet.reportId,
            packet.size,
            packet.sequence);
    }
}
