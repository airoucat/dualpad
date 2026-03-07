#include "pch.h"
#include "input/hid/DualSenseDevice.h"

#include <SKSE/SKSE.h>

#include "input/state/PadStateDebugger.h"

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        TransportType InferTransport(const RawInputPacket& packet)
        {
            if (packet.reportId == 0x31) {
                return TransportType::Bluetooth;
            }

            if (packet.reportId == 0x01 && packet.size >= 50) {
                return TransportType::USB;
            }

            if (packet.reportId == 0x01 && packet.size <= 16) {
                return TransportType::Bluetooth;
            }

            return TransportType::Unknown;
        }
    }

    bool DualSenseDevice::Open()
    {
        return _transport.OpenFirstDualSense();
    }

    void DualSenseDevice::Close()
    {
        _transport.Close();
        _lastReadStatus = ReadStatus::None;
        _sequence = 0;
    }

    bool DualSenseDevice::IsOpen() const
    {
        return _transport.IsOpen();
    }

    bool DualSenseDevice::ReadPacket(RawInputPacket& outPacket)
    {
        std::size_t bytesRead = 0;
        _lastReadStatus = _transport.Read(_buffer, bytesRead);
        if (_lastReadStatus != ReadStatus::Ok || bytesRead == 0) {
            return false;
        }

        outPacket.transport = _transport.GetTransportType();
        outPacket.reportId = _buffer[0];
        outPacket.data = _buffer.data();
        outPacket.size = bytesRead;
        outPacket.timestampUs = CaptureInputTimestampUs();
        outPacket.sequence = ++_sequence;

        MaybeInferTransport(outPacket);
        outPacket.transport = _transport.GetTransportType();
        return true;
    }

    TransportType DualSenseDevice::GetTransportType() const
    {
        return _transport.GetTransportType();
    }

    ReadStatus DualSenseDevice::GetLastReadStatus() const
    {
        return _lastReadStatus;
    }

    hid_device* DualSenseDevice::GetNativeHandle() const
    {
        return _transport.GetNativeHandle();
    }

    void DualSenseDevice::MaybeInferTransport(const RawInputPacket& packet)
    {
        if (_transport.GetTransportType() != TransportType::Unknown) {
            return;
        }

        const auto inferred = InferTransport(packet);
        if (inferred == TransportType::Unknown) {
            return;
        }

        logger::info("[DualPad][HID] Transport inferred from report stream: {}", ToString(inferred));
        _transport.SetTransportType(inferred);
    }
}
