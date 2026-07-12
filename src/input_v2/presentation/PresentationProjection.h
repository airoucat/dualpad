#pragma once

#include "input_v2/actions/ActionSetResolver.h"
#include "input_v2/context/ContextResolver.h"
#include "input_v2/ingress/MeaningfulSourceActivity.h"
#include "input_v2/presentation/SourceEvidenceCollector.h"

#include <cstdint>
#include <optional>
#include <span>

namespace dualpad::input_v2::presentation
{
    enum class PresentationOwner : std::uint8_t
    {
        KeyboardMouse = 0,
        Gamepad
    };

    enum class NavigationOwner : std::uint8_t
    {
        None = 0,
        KeyboardMouse,
        Gamepad
    };

    enum class CursorOwner : std::uint8_t
    {
        KeyboardMouse = 0,
        Gamepad
    };

    enum class PointerIntent : std::uint8_t
    {
        None = 0,
        HoverOnly,
        PointerActive
    };

    enum class GameplayPresentationReasonCode : std::uint8_t
    {
        None = 0,
        CoordinatorPublished,
        ExplicitResync,
        CarryLookOwner,
        CarryMoveOwner,
        CarryCombatOwner,
        CarryDigitalOwner,
        NonGameplayContext,
        RecoveryRepublish
    };

    enum class PresentationDecisionReason : std::uint8_t
    {
        None = 0,
        GameplayEngineOwner,
        GameplayMenuEntryOwner,
        MenuSourceEvidence,
        RollbackCompatibilitySurface
    };

    enum class PresentationDirtyFlags : std::uint8_t
    {
        None = 0,
        Family = 1 << 0,
        Owner = 1 << 1,
        Cursor = 1 << 2,
        Context = 1 << 3,
        ActionSets = 1 << 4,
        Policy = 1 << 5
    };

    enum class PromptFamilyDecisionReason : std::uint8_t
    {
        CarryPrevious = 0,
        KeyboardMouseMeaningfulActivity,
        GamepadMeaningfulActivity,
        GamepadUnavailableFallback,
        RecoveryFrozen,
        ExplicitResync
    };

    enum class MenuOwnerDecisionReason : std::uint8_t
    {
        CarryPrevious = 0,
        GameplayMenuEntryOwner,
        KeyboardMouseStrongActivity,
        GamepadStrongActivity,
        RecoveryFrozen
    };

    enum class CursorOwnerDecisionReason : std::uint8_t
    {
        CarryPrevious = 0,
        MousePointerThreshold,
        MenuOwnerFallback,
        HandoffPending,
        HandoffCommitted,
        RecoveryFrozen
    };

    enum class CursorPositionSyncPolicy : std::uint8_t
    {
        MappingUnverified = 0,
        NotRequired,
        Required
    };

    struct PromptFamilyDecision
    {
        DeviceFamily family{ DeviceFamily::KeyboardMouse };
        std::uint32_t revision{ 0 };
        std::uint64_t acceptedActivitySeq{ 0 };
        PromptFamilyDecisionReason reason{ PromptFamilyDecisionReason::CarryPrevious };

        friend bool operator==(const PromptFamilyDecision&, const PromptFamilyDecision&) = default;
    };

    struct MenuPresentationDecision
    {
        PresentationOwner owner{ PresentationOwner::KeyboardMouse };
        NavigationOwner navigationOwner{ NavigationOwner::KeyboardMouse };
        std::uint64_t acceptedActivitySeq{ 0 };
        MenuOwnerDecisionReason reason{ MenuOwnerDecisionReason::CarryPrevious };
        std::uint32_t contextRevision{ 0 };
        std::uint32_t epoch{ 0 };

        friend bool operator==(const MenuPresentationDecision&, const MenuPresentationDecision&) = default;
    };

    struct CursorDecision
    {
        CursorOwner requestedOwner{ CursorOwner::KeyboardMouse };
        CursorOwner committedOwner{ CursorOwner::KeyboardMouse };
        std::uint64_t acceptedActivitySeq{ 0 };
        CursorOwnerDecisionReason reason{ CursorOwnerDecisionReason::CarryPrevious };
        bool positionSyncRequired{ false };
        std::uint64_t pendingToken{ 0 };
        std::uint32_t contextRevision{ 0 };
        std::uint32_t epoch{ 0 };

        friend bool operator==(const CursorDecision&, const CursorDecision&) = default;
    };

    struct CursorHandoffPlan
    {
        std::uint64_t token{ 0 };
        CursorOwner from{ CursorOwner::KeyboardMouse };
        CursorOwner to{ CursorOwner::KeyboardMouse };
        std::uint32_t contextRevision{ 0 };
        std::uint32_t presentationEpoch{ 0 };
        std::uint64_t targetMenuInstanceId{ 0 };
        std::uintptr_t targetMenuPtr{ 0 };
        std::uintptr_t targetMoviePtr{ 0 };
    };

    enum class CursorHandoffFailure : std::uint8_t
    {
        None = 0,
        MenuIdentityMismatch,
        MovieIdentityMismatch,
        MappingUnverified,
        PositionUnavailable,
        CoordinateOutOfRange,
        WriteVerificationFailed
    };

    struct CursorHandoffAck
    {
        std::uint64_t token{ 0 };
        std::uint64_t targetMenuInstanceId{ 0 };
        std::uint32_t contextRevision{ 0 };
        std::uint32_t presentationEpoch{ 0 };
        bool positionSynchronized{ false };
        float appliedNativeX{ 0.0F };
        float appliedNativeY{ 0.0F };
        CursorHandoffFailure failure{ CursorHandoffFailure::None };
    };

    enum class MenuRefreshEligibility : std::uint8_t
    {
        NotMenu = 0,
        EligibleStableMenu,
        ObserverPartial,
        ObserverUnavailable,
        IdentityDegraded,
        NoStableTarget
    };

    struct PublishedGameplayPresentation
    {
        PresentationOwner engineOwner{ PresentationOwner::KeyboardMouse };
        PresentationOwner menuEntryOwner{ PresentationOwner::KeyboardMouse };
        std::uint32_t gameplayPresentationRevision{ 0 };
        GameplayPresentationReasonCode reason{ GameplayPresentationReasonCode::None };
        std::uint64_t publishedTick{ 0 };
    };

    struct PublishedPresentationState
    {
        PromptFamilyDecision prompt{};
        MenuPresentationDecision menu{};
        CursorDecision cursor{};
        ingress::SourceActivityRoutingState activityRouting{};
        std::uint32_t presentationEpoch{ 0 };
        PresentationOwner gameplayMenuEntryIntentOwner{ PresentationOwner::KeyboardMouse };
        std::uint32_t gameplayMenuEntryIntentRevision{ 0 };

        // Compatibility mirrors for existing prompt/menu/hook consumers. They are
        // derived from the atomic decisions above and never form a second authority.
        DeviceFamily family{ DeviceFamily::KeyboardMouse };
        std::uint32_t deviceFamilyRevision{ 0 };
        PresentationOwner owner{ PresentationOwner::KeyboardMouse };
        NavigationOwner navigationOwner{ NavigationOwner::KeyboardMouse };
        CursorOwner cursorOwner{ CursorOwner::KeyboardMouse };
        PointerIntent pointerIntent{ PointerIntent::None };
        context::UiContextId uiContextId{ context::UiContextId::None };
        MenuRefreshEligibility menuRefreshEligibility{ MenuRefreshEligibility::NotMenu };
        std::string targetMenuName;
        menu::MenuInstanceId targetMenuInstanceId{ 0 };
        std::uintptr_t targetMenuPtr{ 0 };
        std::uintptr_t targetMenuMoviePtr{ 0 };
        std::uint32_t menuStackRevision{ 0 };
        actions::ActionSetStack actionSetStack;
        context::PresentationPolicyId presentationPolicyId;
        std::uint32_t contextRevision{ 0 };
        std::uint32_t gameplayPresentationRevision{ 0 };
        std::uint32_t epoch{ 0 };
        PresentationDirtyFlags dirty{ PresentationDirtyFlags::None };
        PresentationDecisionReason reason{ PresentationDecisionReason::None };
    };

    struct PresentationProjectionInput
    {
        const PublishedPresentationState& previous;
        const context::ResolvedContextSnapshot& context;
        PresentationOwner gameplayMenuEntryOwner{ PresentationOwner::KeyboardMouse };
        std::span<const ingress::RoutedSourceActivity> routedActivities;
        std::optional<CursorHandoffAck> cursorAck;
        ingress::InputResetReasonMask resetReasons{ 0 };
        std::uint64_t inputStateEpoch{ 0 };
        std::uint64_t ownerTickToken{ 0 };
        CursorPositionSyncPolicy keyboardMouseToGamepadSync{ CursorPositionSyncPolicy::MappingUnverified };
        CursorPositionSyncPolicy gamepadToKeyboardMouseSync{ CursorPositionSyncPolicy::MappingUnverified };
    };

    struct PresentationProjectionDecision
    {
        PublishedPresentationState state{};
        std::optional<CursorHandoffPlan> cursorPlan;
        bool refreshTargetMenu{ false };
    };

    [[nodiscard]] PresentationProjectionDecision ProjectPresentation(
        const PresentationProjectionInput& input) noexcept;

    PresentationDirtyFlags operator|(PresentationDirtyFlags lhs, PresentationDirtyFlags rhs);
    PresentationDirtyFlags& operator|=(PresentationDirtyFlags& lhs, PresentationDirtyFlags rhs);
    bool HasDirtyFlag(PresentationDirtyFlags flags, PresentationDirtyFlags flag);

    class PresentationProjection
    {
    public:
        PublishedPresentationState Project(
            const SourceEvidenceSnapshot& evidence,
            const context::ResolvedContextSnapshot& contextSnapshot,
            const PublishedGameplayPresentation& gameplay,
            std::uint64_t ownerNowMs = 0);
        PublishedPresentationState ProjectOrdered(
            const SourceEvidenceSnapshot& evidence,
            const context::ResolvedContextSnapshot& contextSnapshot,
            const PublishedGameplayPresentation& gameplay,
            std::span<const ingress::MeaningfulSourceActivity> orderedActivities,
            const actions::ResolvedActionFrame& resolvedActions,
            std::uint64_t ownerNowMs,
            std::uint64_t inputStateEpoch,
            std::uint64_t ownerTickToken,
            std::optional<CursorHandoffAck> cursorAck = std::nullopt);
        const PublishedPresentationState& GetPublished() const;
        const std::optional<CursorHandoffPlan>& GetPendingCursorPlan() const;
        void ResetForTests();

    private:
        PublishedPresentationState _published{};
        std::uint64_t _compatSyntheticSeq{ 0 };
        std::optional<SourceEvidenceSnapshot> _lastCompatEvidence;
        std::optional<CursorHandoffPlan> _pendingCursorPlan;
    };
}
