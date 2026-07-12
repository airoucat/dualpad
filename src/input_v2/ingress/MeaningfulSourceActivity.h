#pragma once

#include "input_v2/ingress/InputResetReason.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace dualpad::input_v2::actions
{
    struct ResolvedActionFrame;
}

namespace dualpad::input_v2::context
{
    struct ResolvedContextSnapshot;
}

namespace dualpad::input_v2::ingress
{
    enum class SourceActivityKind : std::uint8_t
    {
        KeyboardPress = 0,
        KeyboardCharacter,
        MouseButtonPress,
        MouseWheel,
        MouseDelta,
        GamepadButtonPress,
        GamepadAnalogEnter,
        GamepadAnalogChange,
        GamepadTouchPress,
        ExplicitResync
    };

    enum class SourceActivityDomain : std::uint8_t
    {
        Unclassified = 0,
        Gameplay,
        MenuNavigation,
        MenuPointer
    };

    struct MeaningfulSourceActivityDraft
    {
        PhysicalInputSource source{ PhysicalInputSource::None };
        SourceActivityKind kind{ SourceActivityKind::KeyboardPress };
        std::uint32_t controlCode{ 0 };
        std::int32_t deltaX{ 0 };
        std::int32_t deltaY{ 0 };
        std::uint64_t producerTimestampUs{ 0 };
    };

    struct MeaningfulSourceActivity : MeaningfulSourceActivityDraft
    {
        std::uint64_t ingressSeq{ 0 };
        std::uint64_t inputStateEpoch{ 0 };
        std::uint64_t gamepadSessionId{ 0 };
        std::uint32_t contextRevision{ 0 };
    };

    struct SourceActivityRoutingState
    {
        std::int32_t mouseAccumulatedManhattan{ 0 };
        std::uint64_t mouseWindowStartOwnerMs{ 0 };
        std::uint64_t lastAcceptedPointerSeq{ 0 };
        std::uint64_t lastCompetingStrongSeq{ 0 };
        std::optional<MeaningfulSourceActivity> pendingPointerActivity;
        bool pointerPromotionEmitted{ false };
    };

    struct RoutedSourceActivity
    {
        MeaningfulSourceActivity activity{};
        SourceActivityDomain domain{ SourceActivityDomain::Unclassified };
        std::optional<GameplayChannel> gameplayChannel;
        bool qualifiesForPrompt{ false };
        bool strongForMenuOwner{ false };
        bool qualifiesForCursor{ false };
        bool qualifiedByOwnerTimer{ false };
    };

    struct RoutedSourceActivityFrame
    {
        std::vector<RoutedSourceActivity> activities;
        SourceActivityRoutingState next{};
    };

    RoutedSourceActivityFrame RouteSourceActivities(
        std::span<const MeaningfulSourceActivity> orderedActivities,
        const SourceActivityRoutingState& previous,
        const context::ResolvedContextSnapshot& context,
        const actions::ResolvedActionFrame& resolvedActions,
        std::uint64_t ownerNowMs);
}
