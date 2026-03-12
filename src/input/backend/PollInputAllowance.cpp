#include "pch.h"
#include "input/backend/PollInputAllowance.h"

#include "input/injection/NativeInputPreControlMapHook.h"

namespace dualpad::input::backend
{
    namespace
    {
        bool UsesGameplayBroadGate(InputContext context)
        {
            switch (context) {
            case InputContext::Gameplay:
            case InputContext::Combat:
            case InputContext::Sneaking:
            case InputContext::Riding:
            case InputContext::Werewolf:
            case InputContext::VampireLord:
            case InputContext::Death:
            case InputContext::Bleedout:
            case InputContext::Ragdoll:
            case InputContext::KillMove:
                return true;
            default:
                return false;
            }
        }
    }

    PollInputAllowanceSnapshot SamplePollInputAllowance(InputContext context)
    {
        PollInputAllowanceSnapshot snapshot{};

        if (auto* controlMap = RE::ControlMap::GetSingleton(); controlMap) {
            // The gameplay root allow gate is useful staging for future defer,
            // but first-press commit timing tracks the narrower ControlMap
            // families more closely than the broad root signal.
            snapshot.gameplayJumpingAllowed = controlMap->IsJumpingControlsEnabled();
            snapshot.gameplayActivateAllowed = controlMap->IsActivateControlsEnabled();
            snapshot.gameplayMovementAllowed = controlMap->IsMovementControlsEnabled();
            snapshot.gameplaySneakingAllowed = controlMap->IsSneakingControlsEnabled();
            snapshot.gameplayFightingAllowed = controlMap->IsFightingControlsEnabled();
            snapshot.menuControlsAllowed = controlMap->IsMenuControlsEnabled();
        }

        if (UsesGameplayBroadGate(context)) {
            snapshot.gameplayBroadAllowed =
                input::NativeInputPreControlMapHook::GetSingleton().IsGameplayInputGateOpen();
        }

        return snapshot;
    }
}
