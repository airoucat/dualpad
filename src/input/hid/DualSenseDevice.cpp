#include "pch.h"
#include "input/hid/DualSenseDevice.h"

#include <SKSE/SKSE.h>

#include "input/state/PadStateDebugger.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        TransportType GuessTransportFromPath(std::string_view path)
        {
            if (path.empty()) {
                return TransportType::Unknown;
            }

            std::string lowered(path);
            std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });

            if (lowered.find("bth") != std::string::npos ||
                lowered.find("bluetooth") != std::string::npos) {
                return TransportType::Bluetooth;
            }

            if (lowered.find("usb") != std::string::npos) {
                return TransportType::USB;
            }

            return TransportType::Unknown;
        }

        TransportType VerifyTransportFromPacket(const RawInputPacket& packet)
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
        if (!_transport.OpenFirstDualSense()) {
            return false;
        }

        ResolveTransportFromPathHint();
        return true;
    }

    void DualSenseDevice::Close()
    {
        _transport.Close();
        _lastReadStatus = ReadStatus::None;
        _sequence = 0;
        _transportResolution = {};
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

        outPacket.transport = _transportResolution.type;
        outPacket.transportConfidence = _transportResolution.confidence;
        outPacket.reportId = _buffer[0];
        outPacket.data = _buffer.data();
        outPacket.size = bytesRead;
        outPacket.timestampUs = CaptureInputTimestampUs();
        outPacket.sequence = ++_sequence;

        MaybeVerifyTransport(outPacket);
        outPacket.transport = _transportResolution.type;
        outPacket.transportConfidence = _transportResolution.confidence;
        return true;
    }

    TransportType DualSenseDevice::GetTransportType() const
    {
        return _transportResolution.type;
    }

    const TransportResolution& DualSenseDevice::GetTransportResolution() const
    {
        return _transportResolution;
    }

    ReadStatus DualSenseDevice::GetLastReadStatus() const
    {
        return _lastReadStatus;
    }

    hid_device* DualSenseDevice::GetNativeHandle() const
    {
        return _transport.GetNativeHandle();
    }

    void DualSenseDevice::ResolveTransportFromPathHint()
    {
        const auto hinted = GuessTransportFromPath(_transport.GetDevicePath());
        if (hinted == TransportType::Unknown) {
            _transportResolution = {};
            logger::info("[DualPad][HID] Transport hint unavailable from device path");
            return;
        }

        _transportResolution.type = hinted;
        _transportResolution.confidence = TransportConfidence::PathHint;
        logger::info(
            "[DualPad][HID] Transport hint from device path: {} ({})",
            ToString(_transportResolution.type),
            ToString(_transportResolution.confidence));
    }

    void DualSenseDevice::MaybeVerifyTransport(const RawInputPacket& packet)
    {
        if (_transportResolution.confidence == TransportConfidence::PacketVerified) {
            return;
        }

        const auto verified = VerifyTransportFromPacket(packet);
        if (verified == TransportType::Unknown) {
            return;
        }

        if (_transportResolution.confidence == TransportConfidence::PathHint &&
            _transportResolution.type != TransportType::Unknown &&
            _transportResolution.type != verified) {
            logger::warn(
                "[DualPad][HID] Transport hint conflict: hint={} verified={} report=0x{:02X} size={}",
                ToString(_transportResolution.type),
                ToString(verified),
                packet.reportId,
                packet.size);
        }

        _transportResolution.type = verified;
        _transportResolution.confidence = TransportConfidence::PacketVerified;
        logger::info(
            "[DualPad][HID] Transport verified from report 0x{:02X} size={} -> {}",
            packet.reportId,
            packet.size,
            ToString(_transportResolution.type));
    }
}
