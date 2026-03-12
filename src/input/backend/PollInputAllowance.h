#pragma once

#include "input/InputContext.h"
#include "input/backend/ButtonCommitGate.h"

namespace dualpad::input::backend
{
    struct PollInputAllowanceSnapshot
    {
        bool gameplayBroadAllowed{ true };
        bool gameplayJumpingAllowed{ true };
        bool gameplayActivateAllowed{ true };
        bool gameplayMovementAllowed{ true };
        bool gameplaySneakingAllowed{ true };
        bool gameplayFightingAllowed{ true };
        bool menuControlsAllowed{ true };

        [[nodiscard]] bool IsAllowed(ButtonCommitGateClass gateClass) const
        {
            switch (gateClass) {
            case ButtonCommitGateClass::GameplayBroad:
                return gameplayBroadAllowed;
            case ButtonCommitGateClass::GameplayJumping:
                return gameplayJumpingAllowed;
            case ButtonCommitGateClass::GameplayActivate:
                return gameplayActivateAllowed;
            case ButtonCommitGateClass::GameplayMovement:
                return gameplayMovementAllowed;
            case ButtonCommitGateClass::GameplaySneaking:
                return gameplaySneakingAllowed;
            case ButtonCommitGateClass::GameplayFighting:
                return gameplayFightingAllowed;
            case ButtonCommitGateClass::MenuControls:
                return menuControlsAllowed;
            case ButtonCommitGateClass::None:
            default:
                return true;
            }
        }
    };

    PollInputAllowanceSnapshot SamplePollInputAllowance(InputContext context);
}
