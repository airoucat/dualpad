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
        // D-Pad
        std::uint32_t dpadUp{ 0x00010000 };
        std::uint32_t dpadDown{ 0x00020000 };
        std::uint32_t dpadLeft{ 0x00040000 };
        std::uint32_t dpadRight{ 0x00080000 };

        // 面键
        std::uint32_t square{ 0x00000001 };
        std::uint32_t cross{ 0x00000002 };
        std::uint32_t circle{ 0x00000004 };
        std::uint32_t triangle{ 0x00000008 };

        // 肩键
        std::uint32_t l1{ 0x00000010 };
        std::uint32_t r1{ 0x00000020 };
        std::uint32_t l2Button{ 0x00000040 };
        std::uint32_t r2Button{ 0x00000080 };

        // 功能键
        std::uint32_t create{ 0x00000100 };
        std::uint32_t options{ 0x00000200 };
        std::uint32_t l3{ 0x00000400 };
        std::uint32_t r3{ 0x00000800 };

        // 其他
        std::uint32_t ps{ 0x00001000 };
        std::uint32_t mic{ 0x00002000 };
        std::uint32_t touchpadClick{ 0x00004000 };

        // === DualSense Edge 扩展按键 ===
        std::uint32_t fnLeft{ 0x00100000 };
        std::uint32_t fnRight{ 0x00200000 };
        std::uint32_t backLeft{ 0x00400000 };
        std::uint32_t backRight{ 0x00800000 };

        // 触摸板虚拟按键
        std::uint32_t tpLeftPress{ 0x01000000 };
        std::uint32_t tpMidPress{ 0x02000000 };
        std::uint32_t tpRightPress{ 0x04000000 };
        std::uint32_t tpSwipeUp{ 0x08000000 };
        std::uint32_t tpSwipeDown{ 0x10000000 };
        std::uint32_t tpSwipeLeft{ 0x20000000 };
        std::uint32_t tpSwipeRight{ 0x40000000 };

        // 语义映射
        std::uint32_t activate{ cross };
        std::uint32_t jump{ cross };
        std::uint32_t cancel{ circle };
        std::uint32_t sneak{ circle };
        std::uint32_t readyWeapon{ square };
        std::uint32_t togglePOV{ triangle };
        std::uint32_t sprint{ l1 };
        std::uint32_t attack{ r1 };
        std::uint32_t menu{ options };
        std::uint32_t wait{ create };

        // 扩展功能语义映射（示例）
        std::uint32_t quickSave{ backLeft };
        std::uint32_t quickLoad{ backRight };
        std::uint32_t openInventory{ tpLeftPress };
        std::uint32_t openMap{ tpSwipeUp };
        std::uint32_t openMagic{ tpRightPress };
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