#pragma once
#include <cstdint>

namespace dualpad::input
{
    enum class PadProfile : std::uint8_t
    {
        PC = 0,
        X360 = 1,
        PS3 = 2
    };

    struct PadBits
    {
        std::uint32_t dpadUp{ 0x00010000 };
        std::uint32_t dpadDown{ 0x00020000 };
        std::uint32_t dpadLeft{ 0x00040000 };
        std::uint32_t dpadRight{ 0x00080000 };

        std::uint32_t activate{ 0x00000002 };   // Cross
        std::uint32_t cancel{ 0x00000004 };     // Circle
        std::uint32_t jump{ 0x00000008 };       // Triangle
        std::uint32_t sneak{ 0x00000800 };      // R3

        std::uint32_t l1{ 0x00000010 };
        std::uint32_t r1{ 0x00000020 };
        std::uint32_t sprint{ 0x00000400 };     // L3
        std::uint32_t shout{ 0x00000020 };      // R1
    };

    inline const PadBits& GetPadBits(PadProfile)
    {
        static const PadBits kBits{};
        return kBits;
    }

    inline PadProfile GetActivePadProfile()
    {
        return PadProfile::PC;
    }
}