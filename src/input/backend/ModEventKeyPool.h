#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace dualpad::input::backend
{
    struct ModEventKeySlot
    {
        std::string_view actionId;
        std::string_view helperActionId;
        // Raw DirectInput/DIK scancode emitted by the simulated keyboard bridge.
        std::uint8_t directInputScancode;
    };

    inline constexpr std::array<ModEventKeySlot, 24> kModEventKeySlots{ {
        { "ModEvent1", "VirtualKey.DIK_F13", 100 },
        { "ModEvent2", "VirtualKey.DIK_F14", 101 },
        { "ModEvent3", "VirtualKey.DIK_F15", 102 },
        { "ModEvent4", "VirtualKey.DIK_KANA", 112 },
        { "ModEvent5", "VirtualKey.DIK_ABNT_C1", 115 },
        { "ModEvent6", "VirtualKey.DIK_CONVERT", 121 },
        { "ModEvent7", "VirtualKey.DIK_NOCONVERT", 123 },
        { "ModEvent8", "VirtualKey.DIK_ABNT_C2", 126 },
        { "ModEvent9", "VirtualKey.NumPadEqual", 141 },
        { "ModEvent10", "VirtualKey.L-Windows", 219 },
        { "ModEvent11", "VirtualKey.R-Windows", 220 },
        { "ModEvent12", "VirtualKey.Apps", 221 },
        { "ModEvent13", "VirtualKey.Power", 222 },
        { "ModEvent14", "VirtualKey.Sleep", 223 },
        { "ModEvent15", "VirtualKey.Wake", 227 },
        { "ModEvent16", "VirtualKey.WebSearch", 229 },
        { "ModEvent17", "VirtualKey.WebFavorites", 230 },
        { "ModEvent18", "VirtualKey.WebRefresh", 231 },
        { "ModEvent19", "VirtualKey.WebStop", 232 },
        { "ModEvent20", "VirtualKey.WebForward", 233 },
        { "ModEvent21", "VirtualKey.WebBack", 234 },
        { "ModEvent22", "VirtualKey.MyComputer", 235 },
        { "ModEvent23", "VirtualKey.Mail", 236 },
        { "ModEvent24", "VirtualKey.MediaSelect", 237 },
    } };

    inline constexpr const ModEventKeySlot* FindModEventKeySlot(std::string_view actionId)
    {
        for (const auto& slot : kModEventKeySlots) {
            if (slot.actionId == actionId) {
                return &slot;
            }
        }

        return nullptr;
    }
}
