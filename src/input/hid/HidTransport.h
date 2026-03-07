#pragma once

#include "input/protocol/DualSenseProtocolTypes.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

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

    // Thin hidapi wrapper that owns the native handle and exposes raw device metadata.
    class HidTransport
    {
    public:
        static bool InitializeApi();
        static void ShutdownApi();

        bool OpenFirstDualSense();
        void Close();
        bool IsOpen() const;

        ReadStatus Read(std::span<std::uint8_t> buffer, std::size_t& outBytes);

        hid_device* GetNativeHandle() const;
        std::string_view GetDevicePath() const;

    private:
        hid_device* _device{ nullptr };
        std::string _devicePath{};
        std::uint16_t _vendorId{ 0 };
        std::uint16_t _productId{ 0 };
    };
}
