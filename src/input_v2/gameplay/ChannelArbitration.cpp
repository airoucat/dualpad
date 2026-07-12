#include "pch.h"

#include "input_v2/gameplay/ChannelArbitration.h"

#include <algorithm>

namespace dualpad::input_v2::gameplay
{
    ChannelArbitrationDecision ResolveChannelArbitration(const ChannelArbitrationInput& input) noexcept
    {
        ChannelArbitrationDecision decision{};

        if (input.resetMode == ChannelArbitrationResetMode::Global) {
            return decision;
        }

        auto next = input.previous;
        if (input.resetMode == ChannelArbitrationResetMode::GamepadSource) {
            next.gamepadCandidate = false;
            next.lastKeyboardMouseActivityUs = std::max(
                next.lastKeyboardMouseActivityUs,
                input.lastKeyboardMouseActivityUs);
            if (input.keyboardMouseActive || input.keyboardMouseActivation) {
                next.owner = ChannelOwner::KeyboardMouse;
                decision.reason = input.keyboardMouseReason;
            }
            if (next.owner == ChannelOwner::Gamepad) {
                next.owner = ChannelOwner::None;
            }
            decision.owner = next.owner;
            decision.gateGamepad = true;
            decision.next = next;
            return decision;
        }

        if (!input.gameplayContext) {
            decision.reason = GameplayReasonCode::NonGameplayContext;
            decision.next = {};
            return decision;
        }

        const auto lastKeyboardMouseActivityUs = std::max(
            input.previous.lastKeyboardMouseActivityUs,
            input.lastKeyboardMouseActivityUs);
        next.lastKeyboardMouseActivityUs = lastKeyboardMouseActivityUs;

        const bool gamepadAtEnter = input.gamepadMagnitude >= input.gamepadEnterThreshold;
        const bool gamepadAtSustain = input.gamepadMagnitude >= input.gamepadSustainThreshold;
        next.gamepadCandidate = gamepadAtSustain &&
            (input.previous.gamepadCandidate ||
             input.previous.owner == ChannelOwner::Gamepad ||
             gamepadAtEnter);

        bool keyboardMouseWithinQuietWindow = false;
        if (lastKeyboardMouseActivityUs != 0 && input.nowUs >= lastKeyboardMouseActivityUs) {
            keyboardMouseWithinQuietWindow =
                input.nowUs - lastKeyboardMouseActivityUs <= input.keyboardMouseQuietWindowUs;
        }
        const bool keyboardMouseOwns = input.keyboardMouseActive ||
            input.keyboardMouseActivation ||
            keyboardMouseWithinQuietWindow;

        if (keyboardMouseOwns) {
            next.owner = ChannelOwner::KeyboardMouse;
            decision.owner = next.owner;
            decision.gateGamepad = true;
            decision.reason = input.keyboardMouseReason;
            decision.next = next;
            return decision;
        }

        if ((input.previous.owner == ChannelOwner::Gamepad || input.previous.gamepadCandidate) && gamepadAtSustain) {
            next.owner = ChannelOwner::Gamepad;
            decision.reason = input.gamepadReason;
        } else if (gamepadAtEnter) {
            next.owner = ChannelOwner::Gamepad;
            decision.reason = input.gamepadReason;
        } else {
            next.owner = ChannelOwner::None;
            decision.reason = GameplayReasonCode::None;
        }

        decision.owner = next.owner;
        decision.gateGamepad = decision.owner != ChannelOwner::Gamepad;
        decision.next = next;
        return decision;
    }
}
