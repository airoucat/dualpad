#include "pch.h"
#include "input/protocol/DualSenseProtocol.h"
#include "input/protocol/DualSenseReportIds.h"
#include "input/state/PadStateDebugger.h"

namespace dualpad::input
{
    bool ParseDualSenseUsbInputPacket(const RawInputPacket& packet, PadState& outState);
    bool ParseDualSenseBtInputPacket(const RawInputPacket& packet, PadState& outState);

    namespace
    {
        TransportType ResolveTransport(const RawInputPacket& packet)
        {
            if (packet.transport != TransportType::Unknown) {
                return packet.transport;
            }

            if (packet.reportId == protocol::report::kBtInput31) {
                return TransportType::Bluetooth;
            }

            if (packet.reportId == protocol::report::kUsbInput01 && packet.size >= 50) {
                return TransportType::USB;
            }

            if (packet.reportId == protocol::report::kBtInput01 && packet.size <= 16) {
                return TransportType::Bluetooth;
            }

            return TransportType::Unknown;
        }
    }

    bool ParseDualSenseInputPacket(const RawInputPacket& packet, PadState& outState)
    {
        switch (ResolveTransport(packet)) {
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
