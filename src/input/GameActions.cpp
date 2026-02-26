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

        static constexpr std::array<BPair, 4> kButtonMap{ {
            { OpenInventory, ButtonAction::OpenInventory },
            { OpenMagic,     ButtonAction::OpenMagic },
            { OpenMap,       ButtonAction::OpenMap },
            { OpenJournal,   ButtonAction::OpenJournal },
        } };

        static constexpr std::array<APair, 6> kAxisMap{ {
            { MoveX,    AxisAction::MoveX },
            { MoveY,    AxisAction::MoveY },
            { LookX,    AxisAction::LookX },
            { LookY,    AxisAction::LookY },
            { TriggerL, AxisAction::TriggerL },
            { TriggerR, AxisAction::TriggerR },
        } };
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
        switch (a) {
        case ButtonAction::OpenInventory: return OpenInventory;
        case ButtonAction::OpenMagic:     return OpenMagic;
        case ButtonAction::OpenMap:       return OpenMap;
        case ButtonAction::OpenJournal:   return OpenJournal;
        default:                          return {};
        }
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