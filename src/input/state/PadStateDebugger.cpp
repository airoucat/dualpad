#include "pch.h"
#include "input/state/PadStateDebugger.h"
#include "input/RuntimeConfig.h"

#include <algorithm>
#include <array>
#include <chrono>

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        std::string_view BoolString(bool value)
        {
            return value ? "1" : "0";
        }

        bool ShouldEmitPeriodicInputProbe(std::uint64_t sequence)
        {
            return sequence <= 20 || (sequence % 120) == 0;
        }

        bool HasControlStateChanged(const PadState& previous, const PadState& current)
        {
            return previous.buttons.digitalMask != current.buttons.digitalMask ||
                previous.leftStick.rawX != current.leftStick.rawX ||
                previous.leftStick.rawY != current.leftStick.rawY ||
                previous.rightStick.rawX != current.rightStick.rawX ||
                previous.rightStick.rawY != current.rightStick.rawY ||
                previous.leftTrigger.raw != current.leftTrigger.raw ||
                previous.rightTrigger.raw != current.rightTrigger.raw;
        }

        bool HasPacketPrefixChanged(const RawInputPacket& previous, const RawInputPacket& current)
        {
            if (!previous.data || !current.data) {
                return true;
            }

            constexpr std::size_t kProbeBytes = 32;
            const auto previousSize = (std::min)(previous.size, kProbeBytes);
            const auto currentSize = (std::min)(current.size, kProbeBytes);
            if (previousSize != currentSize) {
                return true;
            }
            for (std::size_t index = 0; index < currentSize; ++index) {
                if (previous.data[index] != current.data[index]) {
                    return true;
                }
            }
            return false;
        }
    }

    std::uint64_t CaptureInputTimestampUs()
    {
        using namespace std::chrono;
        return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
    }

    bool IsInputDebugLogEnabled(InputDebugLog log)
    {
        const auto& config = RuntimeConfig::GetSingleton();
        switch (log) {
        case InputDebugLog::PacketSummary:
            return config.LogInputPackets();
        case InputDebugLog::PacketHexDump:
            return config.LogInputHex();
        case InputDebugLog::StateSummary:
            return config.LogInputState();
        case InputDebugLog::ParseFailure:
            return true;
        default:
            return false;
        }
    }

    void LogPacketSummary(const RawInputPacket& packet)
    {
        if (!IsInputDebugLogEnabled(InputDebugLog::PacketSummary)) {
            return;
        }

        if (!ShouldEmitPeriodicInputProbe(packet.sequence)) {
            return;
        }

        logger::info(
            "[DualPad][Input][Packet] transport={} confidence={} report=0x{:02X} size={} seq={} tsUs={}",
            ToString(packet.transport),
            ToString(packet.transportConfidence),
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

        static std::array<std::uint8_t, 32> lastPrefix{};
        static RawInputPacket lastPacket{};
        static bool hasLastPacket = false;

        RawInputPacket prefixPacket = packet;
        std::array<std::uint8_t, 32> currentPrefix{};
        const auto prefixSize = (std::min)(packet.size, currentPrefix.size());
        for (std::size_t index = 0; index < prefixSize; ++index) {
            currentPrefix[index] = packet.data[index];
        }
        prefixPacket.data = currentPrefix.data();
        prefixPacket.size = prefixSize;

        if (!ShouldEmitPeriodicInputProbe(packet.sequence) &&
            hasLastPacket &&
            !HasPacketPrefixChanged(lastPacket, prefixPacket)) {
            return;
        }

        lastPrefix = currentPrefix;
        lastPacket = prefixPacket;
        lastPacket.data = lastPrefix.data();
        hasLastPacket = true;

        constexpr char kHex[] = "0123456789ABCDEF";
        std::array<char, 128 * 3 + 1> buffer{};

        const std::size_t clampedSize = (std::min)(packet.size, static_cast<std::size_t>(32));
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

        logger::info(
            "[DualPad][Input][Hex] transport={} report=0x{:02X} size={} data={}",
            ToString(packet.transport),
            packet.reportId,
            packet.size,
            buffer.data());
    }

    void LogParseSuccess(const PadState& state)
    {
        if (!IsInputDebugLogEnabled(InputDebugLog::PacketSummary)) {
            return;
        }
        if (!ShouldEmitPeriodicInputProbe(state.sequence)) {
            return;
        }

        if (state.transport == TransportType::Bluetooth && state.reportId == 0x01) {
            logger::info("[DualPad][Input][Parse] Parsed DualSense BT report 0x01 (partial support)");
            return;
        }

        if (state.transport == TransportType::Bluetooth && state.reportId == 0x31) {
            logger::info("[DualPad][Input][Parse] Parsed DualSense BT report 0x31");
            return;
        }

        if (state.transport == TransportType::USB && state.reportId == 0x01) {
            logger::info("[DualPad][Input][Parse] Parsed DualSense USB report 0x01");
        }
    }

    void LogStateSummary(const PadState& state)
    {
        if (!IsInputDebugLogEnabled(InputDebugLog::StateSummary)) {
            return;
        }

        static PadState lastState{};
        static bool hasLastState = false;
        if (!ShouldEmitPeriodicInputProbe(state.sequence) &&
            hasLastState &&
            !HasControlStateChanged(lastState, state)) {
            return;
        }

        lastState = state;
        hasLastState = true;

        logger::info(
            "[DualPad][Input][State] transport={} completeness={} report=0x{:02X} mask=0x{:08X} ls=({:.3f},{:.3f}) rs=({:.3f},{:.3f}) lt={:.3f} rt={:.3f} tp1={}({},{}) tp2={}({},{}) imu={} battery={} valid={}",
            ToString(state.transport),
            ToString(state.parseCompleteness),
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
