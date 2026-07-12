#pragma once

#include "input_v2/ingress/InputResetReason.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace dualpad::input_v2::gameplay
{
    enum class EngineInputMode : std::uint8_t
    {
        Original = 0,
        KeyboardMouse,
        Gamepad
    };

    enum class EngineQueryDomain : std::uint8_t
    {
        Unknown = 0,
        DeviceOperational,
        GameplayLookTransform,
        GameplayMoveTransform,
        GameplayOtherTransform,
        MenuPointerTransform,
        MenuNavigationTransform,
        MenuSetPlatform,
        Remap
    };

    enum class EngineDecisionCausality : std::uint8_t
    {
        AfterOwnerPublication = 0,
        EventLocalSource,
        OriginalOnly
    };

    struct EngineCallerRule
    {
        std::uintptr_t callerRva{ 0 };
        EngineQueryDomain domain{ EngineQueryDomain::Unknown };
        std::optional<ingress::GameplayChannel> channel;
        EngineDecisionCausality causality{ EngineDecisionCausality::OriginalOnly };
        bool overrideEnabled{ false };
    };

    struct EngineModeDecision
    {
        EngineQueryDomain domain{ EngineQueryDomain::Unknown };
        EngineInputMode mode{ EngineInputMode::Original };
        EngineDecisionCausality causality{ EngineDecisionCausality::OriginalOnly };
        std::uint64_t ownerTickToken{ 0 };
        std::uint64_t inputStateEpoch{ 0 };
        std::uint32_t contextRevision{ 0 };
        std::uint64_t runtimeGeneration{ 0 };

        friend bool operator==(const EngineModeDecision&, const EngineModeDecision&) = default;
    };

    struct EngineModeDecisionSnapshot
    {
        std::uint64_t generation{ 0 };
        std::array<EngineModeDecision, 9> byDomain{};

        friend bool operator==(const EngineModeDecisionSnapshot&, const EngineModeDecisionSnapshot&) = default;
    };

    struct EngineModeRecommendation
    {
        EngineQueryDomain domain{ EngineQueryDomain::Unknown };
        EngineInputMode mode{ EngineInputMode::Original };
        EngineDecisionCausality causality{ EngineDecisionCausality::OriginalOnly };
    };

    struct EngineModeProjectionInput
    {
        std::uint64_t ownerTickToken{ 0 };
        std::uint64_t inputStateEpoch{ 0 };
        std::uint32_t contextRevision{ 0 };
        std::uint64_t runtimeGeneration{ 0 };
        std::vector<EngineModeRecommendation> recommendations;
    };

    [[nodiscard]] EngineModeDecisionSnapshot ProjectOriginalEngineModes(
        std::uint64_t ownerTickToken,
        std::uint64_t inputStateEpoch,
        std::uint32_t contextRevision,
        std::uint64_t runtimeGeneration) noexcept;

    [[nodiscard]] EngineModeDecisionSnapshot ProjectEngineModeShadow(
        const EngineModeProjectionInput& input) noexcept;
}
