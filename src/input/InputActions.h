#pragma once
#include <cstdint>
#include <string_view>

namespace dualpad::input
{
    enum class TriggerCode : std::uint16_t
    {
        None = 0,

        // face
        Square, Cross, Circle, Triangle,

        // shoulder / misc
        L1, R1, L2Button, R2Button, Create, Options, L3, R3, PS, Mic, TouchpadClick,

        // dpad (8-way)
        DpadUp, DpadUpRight, DpadRight, DpadDownRight,
        DpadDown, DpadDownLeft, DpadLeft, DpadUpLeft,

        // edge extra
        FnLeft, FnRight, BackLeft, BackRight,

        // virtual touchpad
        TpLeftPress, TpMidPress, TpRightPress,
        TpSwipeUp, TpSwipeDown, TpSwipeLeft, TpSwipeRight,

        LStickUp, LStickDown, LStickLeft, LStickRight,
        RStickUp, RStickDown, RStickLeft, RStickRight
    };

    enum class TriggerPhase : std::uint8_t
    {
        Press = 0,
        Release = 1,
        Pulse = 2   // 单次事件（如 Swipe）
    };

    enum class AxisCode : std::uint8_t
    {
        LStickX, LStickY, RStickX, RStickY, L2, R2
    };

    inline constexpr std::string_view ToString(AxisCode a)
    {
        switch (a) {
        case AxisCode::LStickX: return "LStickX";
        case AxisCode::LStickY: return "LStickY";
        case AxisCode::RStickX: return "RStickX";
        case AxisCode::RStickY: return "RStickY";
        case AxisCode::L2: return "L2";
        case AxisCode::R2: return "R2";
        default: return "Unknown";
        }
    }

    inline constexpr std::string_view ToString(TriggerCode c)
    {
        switch (c) {
        case TriggerCode::Square: return "Square";
        case TriggerCode::Cross: return "Cross";
        case TriggerCode::Circle: return "Circle";
        case TriggerCode::Triangle: return "Triangle";
        case TriggerCode::L1: return "L1";
        case TriggerCode::R1: return "R1";
        case TriggerCode::L2Button: return "L2_Button";
        case TriggerCode::R2Button: return "R2_Button";
        case TriggerCode::Create: return "Create";
        case TriggerCode::Options: return "Options";
        case TriggerCode::L3: return "L3";
        case TriggerCode::R3: return "R3";
        case TriggerCode::PS: return "PS";
        case TriggerCode::Mic: return "Mic";
        case TriggerCode::TouchpadClick: return "TouchpadClick";
        case TriggerCode::DpadUp: return "DPadUp";
        case TriggerCode::DpadUpRight: return "DPadUpRight";
        case TriggerCode::DpadRight: return "DPadRight";
        case TriggerCode::DpadDownRight: return "DPadDownRight";
        case TriggerCode::DpadDown: return "DPadDown";
        case TriggerCode::DpadDownLeft: return "DPadDownLeft";
        case TriggerCode::DpadLeft: return "DPadLeft";
        case TriggerCode::DpadUpLeft: return "DPadUpLeft";
        case TriggerCode::FnLeft: return "FnLeft";
        case TriggerCode::FnRight: return "FnRight";
        case TriggerCode::BackLeft: return "BackLeft";
        case TriggerCode::BackRight: return "BackRight";
        case TriggerCode::TpLeftPress: return "TP_LEFT_PRESS";
        case TriggerCode::TpMidPress: return "TP_MID_PRESS";
        case TriggerCode::TpRightPress: return "TP_RIGHT_PRESS";
        case TriggerCode::TpSwipeUp: return "TP_SWIPE_UP";
        case TriggerCode::TpSwipeDown: return "TP_SWIPE_DOWN";
        case TriggerCode::TpSwipeLeft: return "TP_SWIPE_LEFT";
        case TriggerCode::TpSwipeRight: return "TP_SWIPE_RIGHT";
        case TriggerCode::LStickUp: return "LStickUp";
        case TriggerCode::LStickDown: return "LStickDown";
        case TriggerCode::LStickLeft: return "LStickLeft";
        case TriggerCode::LStickRight: return "LStickRight";
        case TriggerCode::RStickUp: return "RStickUp";
        case TriggerCode::RStickDown: return "RStickDown";
        case TriggerCode::RStickLeft: return "RStickLeft";
        case TriggerCode::RStickRight: return "RStickRight";
        default: return "None";
        }
    }

    inline constexpr std::string_view ToString(TriggerPhase p)
    {
        switch (p) {
        case TriggerPhase::Press: return "Press";
        case TriggerPhase::Release: return "Release";
        case TriggerPhase::Pulse: return "Pulse";
        default: return "Unknown";
        }
    }
}