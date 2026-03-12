#pragma once

#include <cstdint>
#include <string_view>

namespace dualpad::input::backend
{
    enum class ButtonCommitGateClass : std::uint8_t
    {
        None = 0,
        GameplayBroad,
        GameplayJumping,
        GameplayActivate,
        GameplayMovement,
        GameplaySneaking,
        GameplayFighting,
        MenuControls
    };

    inline constexpr std::string_view ToString(ButtonCommitGateClass gateClass)
    {
        switch (gateClass) {
        case ButtonCommitGateClass::GameplayBroad:
            return "GameplayBroad";
        case ButtonCommitGateClass::GameplayJumping:
            return "GameplayJumping";
        case ButtonCommitGateClass::GameplayActivate:
            return "GameplayActivate";
        case ButtonCommitGateClass::GameplayMovement:
            return "GameplayMovement";
        case ButtonCommitGateClass::GameplaySneaking:
            return "GameplaySneaking";
        case ButtonCommitGateClass::GameplayFighting:
            return "GameplayFighting";
        case ButtonCommitGateClass::MenuControls:
            return "MenuControls";
        case ButtonCommitGateClass::None:
        default:
            return "None";
        }
    }
}
