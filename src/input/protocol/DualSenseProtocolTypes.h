#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace dualpad::input
{
    enum class TransportType : std::uint8_t
    {
        USB,
        Bluetooth,
        Unknown
    };

    inline constexpr std::string_view ToString(TransportType transport)
    {
        switch (transport) {
        case TransportType::USB:
            return "USB";
        case TransportType::Bluetooth:
            return "Bluetooth";
        default:
            return "Unknown";
        }
    }

    // Packet view returned by the HID reader before any protocol parsing occurs.
    struct RawInputPacket
    {
        TransportType transport{ TransportType::Unknown };
        std::uint8_t reportId{ 0 };
        const std::uint8_t* data{ nullptr };
        std::size_t size{ 0 };
        std::uint64_t timestampUs{ 0 };
        std::uint64_t sequence{ 0 };
    };
}
