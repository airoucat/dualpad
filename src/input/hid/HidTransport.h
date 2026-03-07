#pragma once

#include "input/protocol/DualSenseProtocolTypes.h"

#include <cstddef>
#include <cstdint>
#include <span>

struct hid_device_;
using hid_device = struct hid_device_;

namespace dualpad::input
{
    enum class ReadStatus : std::uint8_t
    {
        None,
        Ok,
        Timeout,
        Disconnected,
        Error
    };

    // Thin hidapi wrapper that owns the native handle and reports transport hints.
    class HidTransport
    {
    public:
        static bool InitializeApi();
        static void ShutdownApi();

        bool OpenFirstDualSense();
        void Close();
        bool IsOpen() const;

        ReadStatus Read(std::span<std::uint8_t> buffer, std::size_t& outBytes);

        TransportType GetTransportType() const;
        void SetTransportType(TransportType transport);
        hid_device* GetNativeHandle() const;

    private:
        hid_device* _device{ nullptr };
        TransportType _transport{ TransportType::Unknown };
        std::uint16_t _vendorId{ 0 };
        std::uint16_t _productId{ 0 };
    };
}
