#pragma once

#include "input/protocol/DualSenseProtocolTypes.h"
#include "input/state/PadState.h"

#include <string_view>

namespace dualpad::input
{
    enum class InputDebugLog : std::uint8_t
    {
        PacketSummary,
        PacketHexDump,
        StateSummary,
        ParseFailure
    };

    std::uint64_t CaptureInputTimestampUs();
    bool IsInputDebugLogEnabled(InputDebugLog log);

    void LogPacketSummary(const RawInputPacket& packet);
    void LogPacketHexDump(const RawInputPacket& packet);
    void LogStateSummary(const PadState& state);
    void LogParseFailure(const RawInputPacket& packet, std::string_view reason);
}
