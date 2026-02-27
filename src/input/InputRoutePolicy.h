#pragma once
#include "input/InputActions.h"

namespace dualpad::input
{
    inline bool IsExtendedTrigger(TriggerCode c)
    {
        switch (c) {
        case TriggerCode::FnLeft:
        case TriggerCode::FnRight:
        case TriggerCode::BackLeft:
        case TriggerCode::BackRight:

        case TriggerCode::TpLeftPress:
        case TriggerCode::TpMidPress:
        case TriggerCode::TpRightPress:
        case TriggerCode::TpSwipeUp:
        case TriggerCode::TpSwipeDown:
        case TriggerCode::TpSwipeLeft:
        case TriggerCode::TpSwipeRight:

        case TriggerCode::LStickUp:
        case TriggerCode::LStickDown:
        case TriggerCode::LStickLeft:
        case TriggerCode::LStickRight:
        case TriggerCode::RStickUp:
        case TriggerCode::RStickDown:
        case TriggerCode::RStickLeft:
        case TriggerCode::RStickRight:
            return true;
        default:
            return false;
        }
    }

    inline bool IsNativeMappableTrigger(TriggerCode c)
    {
        switch (c) {
        case TriggerCode::Square:
        case TriggerCode::Cross:
        case TriggerCode::Circle:
        case TriggerCode::Triangle:
        case TriggerCode::L1:
        case TriggerCode::R1:
        case TriggerCode::L2Button:
        case TriggerCode::R2Button:
        case TriggerCode::Create:
        case TriggerCode::Options:
        case TriggerCode::L3:
        case TriggerCode::R3:
        case TriggerCode::DpadUp:
        case TriggerCode::DpadUpRight:
        case TriggerCode::DpadRight:
        case TriggerCode::DpadDownRight:
        case TriggerCode::DpadDown:
        case TriggerCode::DpadDownLeft:
        case TriggerCode::DpadLeft:
        case TriggerCode::DpadUpLeft:
            return true;
        default:
            return false;
        }
    }
}