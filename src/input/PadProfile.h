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
        // D-Pad (对应 HidReader.cpp 里的 BtnCode)
        std::uint32_t dpadUp{ 0x00010000 };
        std::uint32_t dpadDown{ 0x00020000 };
        std::uint32_t dpadLeft{ 0x00040000 };
        std::uint32_t dpadRight{ 0x00080000 };

        // 面键
        std::uint32_t square{ 0x00000001 };     // Square
        std::uint32_t cross{ 0x00000002 };      // Cross (×)
        std::uint32_t circle{ 0x00000004 };     // Circle (○)
        std::uint32_t triangle{ 0x00000008 };   // Triangle (△)

        // 肩键
        std::uint32_t l1{ 0x00000010 };
        std::uint32_t r1{ 0x00000020 };
        std::uint32_t l2Button{ 0x00000040 };
        std::uint32_t r2Button{ 0x00000080 };

        // 功能键
        std::uint32_t create{ 0x00000100 };     // Create (Share)
        std::uint32_t options{ 0x00000200 };    // Options (菜单键)
        std::uint32_t l3{ 0x00000400 };         // L3 (左摇杆按下)
        std::uint32_t r3{ 0x00000800 };         // R3 (右摇杆按下)

        // 其他
        std::uint32_t ps{ 0x00001000 };
        std::uint32_t mic{ 0x00002000 };

        // 语义映射（用于 IAT Hook）
        std::uint32_t activate{ cross };        // 激活 = ×
        std::uint32_t cancel{ circle };         // 取消 = ○
        std::uint32_t jump{ triangle };         // 跳跃 = △
        std::uint32_t menu{ options };          // 菜单 = Options
        std::uint32_t sneak{ r3 };              // 潜行 = R3
        std::uint32_t sprint{ l3 };             // 冲刺 = L3
        std::uint32_t shout{ r1 };              // 吼叫 = R1
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