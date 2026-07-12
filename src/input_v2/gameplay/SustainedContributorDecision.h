#pragma once

#include <cstdint>
#include <limits>

namespace dualpad::input_v2::gameplay
{
    enum class SustainedContributorBit : std::uint8_t
    {
        None = 0,
        Gamepad = 1 << 0,
        KeyboardPhysical = 1 << 1,
        MousePhysical = 1 << 2
    };

    using SustainedContributorMaskType = std::uint8_t;

    constexpr SustainedContributorMaskType SustainedContributorMask(
        SustainedContributorBit source) noexcept
    {
        return static_cast<SustainedContributorMaskType>(source);
    }

    enum class SustainedEffectiveEmitter : std::uint8_t
    {
        None = 0,
        GamepadVirtualBridge,
        KeyboardPhysical,
        MousePhysical
    };

    struct SustainedContributorState
    {
        SustainedContributorMaskType activeSourceMask{ 0 };
        bool virtualMaterialized{ false };
        SustainedEffectiveEmitter effectiveEmitter{ SustainedEffectiveEmitter::None };
        std::uint64_t lastReleaseToken{ 0 };
        std::uint64_t inputStateEpoch{ 0 };
        std::uint32_t contextRevision{ 0 };
        std::uint64_t runtimeGeneration{ 0 };
    };

    struct SustainedContributorInput
    {
        SustainedContributorState previous{};
        SustainedContributorMaskType activeSourceMask{ 0 };
        std::uint64_t gamepadEventOrdinal{ std::numeric_limits<std::uint64_t>::max() };
        std::uint64_t keyboardEventOrdinal{ std::numeric_limits<std::uint64_t>::max() };
        std::uint64_t mouseEventOrdinal{ std::numeric_limits<std::uint64_t>::max() };
        std::uint64_t inputStateEpoch{ 0 };
        std::uint32_t contextRevision{ 0 };
        std::uint64_t runtimeGeneration{ 0 };
    };

    struct SustainedContributorDecision
    {
        SustainedContributorState next{};
        bool aggregateHeld{ false };
        bool virtualBridgeDesired{ false };
        SustainedContributorBit winningPressSource{ SustainedContributorBit::None };
        SustainedContributorMaskType joiningPressSuppressionMask{ 0 };
        SustainedContributorMaskType nonFinalReleaseSuppressionMask{ 0 };
        bool requiresCurrentCycleMutation{ false };
        bool finalRelease{ false };
        std::uint64_t releaseToken{ 0 };
    };

    SustainedContributorDecision ResolveSustainedContributor(
        const SustainedContributorInput& input) noexcept;
}
