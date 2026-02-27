#include "pch.h"
#include "input/GameActions.h"

#include <array>
#include <utility>

namespace dualpad::input::actions
{
    namespace
    {
        using BPair = std::pair<std::string_view, ButtonAction>;
        using APair = std::pair<std::string_view, AxisAction>;

        static constexpr BPair kButtonMap[] = {
            { Confirm, ButtonAction::Confirm },
            { Back, ButtonAction::Back },
            { CloseMenu, ButtonAction::CloseMenu },
            { NavigateUp, ButtonAction::NavigateUp },
            { NavigateDown, ButtonAction::NavigateDown },
            { NavigateLeft, ButtonAction::NavigateLeft },
            { NavigateRight, ButtonAction::NavigateRight },
            { TabSwitch, ButtonAction::TabSwitch },
            { PageUp, ButtonAction::PageUp },
            { PageDown, ButtonAction::PageDown },

            { OpenInventory, ButtonAction::OpenInventory },
            { OpenMagic, ButtonAction::OpenMagic },
            { OpenMap, ButtonAction::OpenMap },
            { OpenLocalMap, ButtonAction::OpenLocalMap },
            { OpenJournal, ButtonAction::OpenJournal },
            { OpenFavorites, ButtonAction::OpenFavorites },
            { OpenTweenMenu, ButtonAction::OpenTweenMenu },
            { OpenPauseMenu, ButtonAction::OpenPauseMenu },
            { OpenOptions, ButtonAction::OpenOptions },

            { Activate, ButtonAction::Activate },
            { Jump, ButtonAction::Jump },
            { Shout, ButtonAction::Shout },
            { ToggleSneak, ButtonAction::ToggleSneak },
            { SprintHold, ButtonAction::SprintHold },
            { ToggleRun, ButtonAction::ToggleRun },
            { LeftEquip, ButtonAction::LeftEquip },
            { RightEquip, ButtonAction::RightEquip },
            { ChargeItem, ButtonAction::ChargeItem },
            { Wait, ButtonAction::Wait },

            { QuickSave, ButtonAction::QuickSave },
            { QuickLoad, ButtonAction::QuickLoad },
            { Screenshot, ButtonAction::Screenshot },
            { ToggleConsole, ButtonAction::ToggleConsole },
        };

        static constexpr APair kAxisMap[] = {
            { MoveX, AxisAction::MoveX },
            { MoveY, AxisAction::MoveY },
            { LookX, AxisAction::LookX },
            { LookY, AxisAction::LookY },
            { TriggerL, AxisAction::TriggerL },
            { TriggerR, AxisAction::TriggerR },
        };
    }

    ButtonAction ParseButtonAction(std::string_view id)
    {
        for (auto& [k, v] : kButtonMap) {
            if (id == k) return v;
        }
        return ButtonAction::Unknown;
    }

    AxisAction ParseAxisAction(std::string_view id)
    {
        for (auto& [k, v] : kAxisMap) {
            if (id == k) return v;
        }
        return AxisAction::Unknown;
    }

    std::string_view ToActionId(ButtonAction a)
    {
        for (auto& [k, v] : kButtonMap) {
            if (v == a) return k;
        }
        return {};
    }

    std::string_view ToActionId(AxisAction a)
    {
        switch (a) {
        case AxisAction::MoveX:    return MoveX;
        case AxisAction::MoveY:    return MoveY;
        case AxisAction::LookX:    return LookX;
        case AxisAction::LookY:    return LookY;
        case AxisAction::TriggerL: return TriggerL;
        case AxisAction::TriggerR: return TriggerR;
        default:                   return {};
        }
    }
}