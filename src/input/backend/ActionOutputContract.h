#pragma once

#include <cstdint>
#include <string_view>

namespace dualpad::input::backend
{
    enum class ActionOutputContract : std::uint8_t
    {
        None = 0,
        Pulse,
        Hold,
        Toggle,
        Repeat,
        Axis
    };

    inline constexpr std::string_view ToString(ActionOutputContract contract)
    {
        switch (contract) {
        case ActionOutputContract::Pulse:
            return "Pulse";
        case ActionOutputContract::Hold:
            return "Hold";
        case ActionOutputContract::Toggle:
            return "Toggle";
        case ActionOutputContract::Repeat:
            return "Repeat";
        case ActionOutputContract::Axis:
            return "Axis";
        case ActionOutputContract::None:
        default:
            return "None";
        }
    }
}
