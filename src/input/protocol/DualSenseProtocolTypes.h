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

    enum class TransportConfidence : std::uint8_t
    {
        Unknown,
        PathHint,
        PacketVerified
    };

    inline constexpr std::string_view ToString(TransportConfidence confidence)
    {
        switch (confidence) {
        case TransportConfidence::PathHint:
            return "PathHint";
        case TransportConfidence::PacketVerified:
            return "PacketVerified";
        default:
            return "Unknown";
        }
    }

    struct TransportResolution
    {
        TransportType type{ TransportType::Unknown };
        TransportConfidence confidence{ TransportConfidence::Unknown };
    };

    // Packet view returned by the HID reader before any protocol parsing occurs.
    struct RawInputPacket
    {
        TransportType transport{ TransportType::Unknown };
        TransportConfidence transportConfidence{ TransportConfidence::Unknown };
        std::uint8_t reportId{ 0 };
        const std::uint8_t* data{ nullptr };
        std::size_t size{ 0 };
        std::uint64_t timestampUs{ 0 };
        std::uint64_t sequence{ 0 };
    };
}
