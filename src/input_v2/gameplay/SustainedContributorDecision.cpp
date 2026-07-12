#include "pch.h"

#include "input_v2/gameplay/SustainedContributorDecision.h"

#include <array>

namespace dualpad::input_v2::gameplay
{
    namespace
    {
        struct OrdinalSource
        {
            SustainedContributorBit source{ SustainedContributorBit::None };
            std::uint64_t ordinal{ 0 };
        };

        SustainedContributorBit EarliestSource(
            SustainedContributorMaskType mask,
            const SustainedContributorInput& input) noexcept
        {
            const auto normalize = [](std::uint64_t ordinal) {
                return ordinal == 0 ?
                    std::numeric_limits<std::uint64_t>::max() : ordinal;
            };
            const std::array sources{
                OrdinalSource{ SustainedContributorBit::KeyboardPhysical, normalize(input.keyboardEventOrdinal) },
                OrdinalSource{ SustainedContributorBit::MousePhysical, normalize(input.mouseEventOrdinal) },
                OrdinalSource{ SustainedContributorBit::Gamepad, normalize(input.gamepadEventOrdinal) }
            };
            auto winner = SustainedContributorBit::None;
            auto ordinal = std::numeric_limits<std::uint64_t>::max();
            for (const auto& candidate : sources) {
                if ((mask & SustainedContributorMask(candidate.source)) == 0) {
                    continue;
                }
                if (winner == SustainedContributorBit::None || candidate.ordinal < ordinal) {
                    winner = candidate.source;
                    ordinal = candidate.ordinal;
                }
            }
            return winner;
        }

        SustainedEffectiveEmitter ResolvePhysicalEmitter(
            SustainedContributorMaskType mask,
            const SustainedContributorInput& input) noexcept
        {
            switch (EarliestSource(mask, input)) {
            case SustainedContributorBit::KeyboardPhysical:
                return SustainedEffectiveEmitter::KeyboardPhysical;
            case SustainedContributorBit::MousePhysical:
                return SustainedEffectiveEmitter::MousePhysical;
            case SustainedContributorBit::Gamepad:
            case SustainedContributorBit::None:
            default:
                return SustainedEffectiveEmitter::None;
            }
        }
    }

    SustainedContributorDecision ResolveSustainedContributor(
        const SustainedContributorInput& input) noexcept
    {
        const auto gamepadMask = SustainedContributorMask(SustainedContributorBit::Gamepad);
        const auto previousMask = input.previous.activeSourceMask;
        const auto nextMask = input.activeSourceMask;
        const bool previouslyHeld = previousMask != 0;
        const bool aggregateHeld = nextMask != 0;
        const auto added = static_cast<SustainedContributorMaskType>(nextMask & ~previousMask);
        const auto removed = static_cast<SustainedContributorMaskType>(previousMask & ~nextMask);

        SustainedContributorDecision decision{};
        decision.aggregateHeld = aggregateHeld;
        decision.virtualBridgeDesired = aggregateHeld &&
            (((nextMask & gamepadMask) != 0) || input.previous.virtualMaterialized);

        if (added != 0) {
            if (previouslyHeld) {
                decision.joiningPressSuppressionMask = added;
            } else {
                decision.winningPressSource = EarliestSource(added, input);
                decision.joiningPressSuppressionMask = static_cast<SustainedContributorMaskType>(
                    added & ~SustainedContributorMask(decision.winningPressSource));
            }
        }
        if (removed != 0 && aggregateHeld) {
            decision.nonFinalReleaseSuppressionMask = removed;
        }
        decision.requiresCurrentCycleMutation =
            decision.joiningPressSuppressionMask != 0 ||
            decision.nonFinalReleaseSuppressionMask != 0;

        decision.finalRelease = previouslyHeld && !aggregateHeld &&
            input.previous.virtualMaterialized;
        decision.releaseToken = decision.finalRelease ?
            input.previous.lastReleaseToken + 1 : 0;

        decision.next = input.previous;
        decision.next.activeSourceMask = nextMask;
        decision.next.virtualMaterialized = decision.virtualBridgeDesired;
        decision.next.effectiveEmitter = decision.virtualBridgeDesired ?
            SustainedEffectiveEmitter::GamepadVirtualBridge :
            ResolvePhysicalEmitter(nextMask, input);
        decision.next.lastReleaseToken = decision.finalRelease ?
            decision.releaseToken : input.previous.lastReleaseToken;
        decision.next.inputStateEpoch = input.inputStateEpoch;
        decision.next.contextRevision = input.contextRevision;
        decision.next.runtimeGeneration = input.runtimeGeneration;
        if (!aggregateHeld) {
            decision.next.effectiveEmitter = SustainedEffectiveEmitter::None;
        }
        return decision;
    }
}
