#include "pch.h"
#include "input/protocol/DualSenseProtocol.h"
#include "input/state/PadStateDebugger.h"

namespace dualpad::input
{
    bool ParseDualSenseUsbInputPacket(const RawInputPacket& packet, PadState& outState);
    bool ParseDualSenseBtInputPacket(const RawInputPacket& packet, PadState& outState);

    bool ParseDualSenseInputPacket(const RawInputPacket& packet, PadState& outState)
    {
        switch (packet.transport) {
        case TransportType::USB:
            return ParseDualSenseUsbInputPacket(packet, outState);
        case TransportType::Bluetooth:
            return ParseDualSenseBtInputPacket(packet, outState);
        default:
            LogParseFailure(packet, "Unsupported transport");
            return false;
        }
    }
}
