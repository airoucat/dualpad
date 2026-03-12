#pragma once

#include <cstdint>
#include <string_view>

namespace dualpad::input::backend
{
    enum class NativeControlCode : std::uint32_t
    {
        None = 0,
        Jump,
        Attack,
        Block,
        Activate,
        Sprint,
        Sneak,
        Shout,
        MenuConfirm,
        MenuCancel,
        MenuScrollUp,
        MenuScrollDown,
        MenuLeft,
        MenuRight,
        MenuPageUp,
        MenuPageDown,
        MoveStick,
        LookStick,
        MenuStick,
        LeftTriggerAxis,
        RightTriggerAxis
    };

    inline constexpr std::uint32_t ToVirtualButtonMask(NativeControlCode code)
    {
        switch (code) {
        case NativeControlCode::Jump:
            return 1u << 0;
        case NativeControlCode::Attack:
            return 1u << 1;
        case NativeControlCode::Block:
            return 1u << 2;
        case NativeControlCode::Activate:
            return 1u << 3;
        case NativeControlCode::Sprint:
            return 1u << 4;
        case NativeControlCode::Sneak:
            return 1u << 5;
        case NativeControlCode::Shout:
            return 1u << 6;
        case NativeControlCode::MenuConfirm:
            return 1u << 7;
        case NativeControlCode::MenuCancel:
            return 1u << 8;
        case NativeControlCode::MenuScrollUp:
            return 1u << 9;
        case NativeControlCode::MenuScrollDown:
            return 1u << 10;
        case NativeControlCode::MenuLeft:
            return 1u << 11;
        case NativeControlCode::MenuRight:
            return 1u << 12;
        case NativeControlCode::MenuPageUp:
            return 1u << 13;
        case NativeControlCode::MenuPageDown:
            return 1u << 14;
        case NativeControlCode::None:
        case NativeControlCode::MoveStick:
        case NativeControlCode::LookStick:
        case NativeControlCode::MenuStick:
        case NativeControlCode::LeftTriggerAxis:
        case NativeControlCode::RightTriggerAxis:
        default:
            return 0;
        }
    }

    inline constexpr std::string_view ToString(NativeControlCode code)
    {
        switch (code) {
        case NativeControlCode::Jump:
            return "Jump";
        case NativeControlCode::Attack:
            return "Attack";
        case NativeControlCode::Block:
            return "Block";
        case NativeControlCode::Activate:
            return "Activate";
        case NativeControlCode::Sprint:
            return "Sprint";
        case NativeControlCode::Sneak:
            return "Sneak";
        case NativeControlCode::Shout:
            return "Shout";
        case NativeControlCode::MenuConfirm:
            return "MenuConfirm";
        case NativeControlCode::MenuCancel:
            return "MenuCancel";
        case NativeControlCode::MenuScrollUp:
            return "MenuScrollUp";
        case NativeControlCode::MenuScrollDown:
            return "MenuScrollDown";
        case NativeControlCode::MenuLeft:
            return "MenuLeft";
        case NativeControlCode::MenuRight:
            return "MenuRight";
        case NativeControlCode::MenuPageUp:
            return "MenuPageUp";
        case NativeControlCode::MenuPageDown:
            return "MenuPageDown";
        case NativeControlCode::MoveStick:
            return "MoveStick";
        case NativeControlCode::LookStick:
            return "LookStick";
        case NativeControlCode::MenuStick:
            return "MenuStick";
        case NativeControlCode::LeftTriggerAxis:
            return "LeftTriggerAxis";
        case NativeControlCode::RightTriggerAxis:
            return "RightTriggerAxis";
        case NativeControlCode::None:
        default:
            return "None";
        }
    }

    inline constexpr bool IsDigitalNativeControl(NativeControlCode code)
    {
        switch (code) {
        case NativeControlCode::Jump:
        case NativeControlCode::Attack:
        case NativeControlCode::Block:
        case NativeControlCode::Activate:
        case NativeControlCode::Sprint:
        case NativeControlCode::Sneak:
        case NativeControlCode::Shout:
        case NativeControlCode::MenuConfirm:
        case NativeControlCode::MenuCancel:
        case NativeControlCode::MenuScrollUp:
        case NativeControlCode::MenuScrollDown:
        case NativeControlCode::MenuLeft:
        case NativeControlCode::MenuRight:
        case NativeControlCode::MenuPageUp:
        case NativeControlCode::MenuPageDown:
            return true;

        case NativeControlCode::None:
        case NativeControlCode::MoveStick:
        case NativeControlCode::LookStick:
        case NativeControlCode::MenuStick:
        case NativeControlCode::LeftTriggerAxis:
        case NativeControlCode::RightTriggerAxis:
        default:
            return false;
        }
    }
}
