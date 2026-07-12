#pragma once

#include "input/backend/ActionLifecyclePolicy.h"
#include "input/backend/ActionOutputContract.h"
#include "input/backend/NativeActionDescriptor.h"
#include "input/backend/NativeControlCode.h"
#include "input_v2/actions/InteractionEngine.h"
#include "input_v2/gameplay/ChannelArbitration.h"
#include "input_v2/gameplay/RecoveryPlan.h"
#include "input_v2/gameplay/SustainedContributorDecision.h"
#include "input_v2/presentation/PresentationProjection.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace dualpad::input_v2::gameplay
{
    enum class LegacyInputContextCompat : std::uint8_t
    {
        Unknown = 0,
        Gameplay,
        Menu,
        Console
    };

    enum class AnalogGateMode : std::uint8_t
    {
        Open = 0,
        ZeroedByKeyboardMouse
    };

    enum class DigitalGateMode : std::uint8_t
    {
        Open = 0,
        SuppressNewTransient,
        CancelAndSuppressNewTransient
    };

    template <class T, std::size_t N>
    struct FixedCommandList
    {
        std::array<T, N> items{};
        std::size_t count{ 0 };
    };

    enum class HelperOutputKind : std::uint8_t
    {
        KeyboardKey = 0,
        ModEvent
    };

    enum class SustainedSourceBit : std::uint8_t
    {
        None = 0,
        GamepadResolved = 1 << 0,
        KeyboardPhysical = 1 << 1,
        MousePhysical = 1 << 2
    };

    struct NativeTransientCommand
    {
        actions::ActionId actionId{};
        dualpad::input::backend::NativeControlCode control{ dualpad::input::backend::NativeControlCode::None };
        actions::ActionPhase phase{ actions::ActionPhase::Press };
        dualpad::input::backend::ActionOutputContract contract{ dualpad::input::backend::ActionOutputContract::None };
        dualpad::input::backend::ActionLifecyclePolicy lifecyclePolicy{ dualpad::input::backend::ActionLifecyclePolicy::None };
        bool gateAware{ false };
        dualpad::input::backend::NativePresentationHandoff presentationHandoff{
            dualpad::input::backend::NativePresentationHandoff::None
        };
        std::uint32_t contextRevision{ 0 };
    };

    struct NativeSustainedCommand
    {
        actions::ActionId actionId{};
        dualpad::input::backend::NativeControlCode control{ dualpad::input::backend::NativeControlCode::None };
        std::uint8_t activeSourceMask{ 0 };
        bool virtualBridgeDesired{ false };
        std::uint8_t joiningPressSuppressionMask{ 0 };
        std::uint8_t nonFinalReleaseSuppressionMask{ 0 };
        std::uint64_t releaseToken{ 0 };
        dualpad::input::backend::ActionOutputContract contract{ dualpad::input::backend::ActionOutputContract::None };
        dualpad::input::backend::ActionLifecyclePolicy lifecyclePolicy{ dualpad::input::backend::ActionLifecyclePolicy::None };
        std::uint32_t contextRevision{ 0 };
    };

    struct HelperOutputCommand
    {
        actions::ActionId actionId{};
        HelperOutputKind kind{ HelperOutputKind::KeyboardKey };
        std::uint16_t helperCode{ 0 };
        actions::ActionPhase phase{ actions::ActionPhase::Press };
        dualpad::input::backend::ActionOutputContract contract{ dualpad::input::backend::ActionOutputContract::None };
        float heldSeconds{ 0.0f };
        std::uint32_t contextRevision{ 0 };
    };

    struct ProjectedAnalogState
    {
        float lookX{ 0.0f };
        float lookY{ 0.0f };
        float moveX{ 0.0f };
        float moveY{ 0.0f };
        float leftTrigger{ 0.0f };
        float rightTrigger{ 0.0f };
    };

    struct GamepadOutputPlan
    {
        ProjectedAnalogState analog{};
        FixedCommandList<NativeTransientCommand, 32> transientDigital{};
        FixedCommandList<NativeSustainedCommand, 8> sustainedDigital{};
    };

    struct KeyboardHelperOutputPlan
    {
        FixedCommandList<HelperOutputCommand, 24> commands{};
        bool enqueueBridgeResetBeforeApply{ false };
    };

    struct GatePlan
    {
        AnalogGateMode lookGate{ AnalogGateMode::Open };
        AnalogGateMode moveGate{ AnalogGateMode::Open };
        AnalogGateMode leftTriggerGate{ AnalogGateMode::Open };
        AnalogGateMode rightTriggerGate{ AnalogGateMode::Open };
        DigitalGateMode transientDigitalGate{ DigitalGateMode::Open };
    };

    struct DecisionReasonByChannel
    {
        GameplayReasonCode look{ GameplayReasonCode::None };
        GameplayReasonCode move{ GameplayReasonCode::None };
        GameplayReasonCode combat{ GameplayReasonCode::None };
        GameplayReasonCode digital{ GameplayReasonCode::None };
        GameplayReasonCode recovery{ GameplayReasonCode::None };
    };

    struct GameplayPresentationPlan
    {
        presentation::PresentationOwner engineOwner{ presentation::PresentationOwner::KeyboardMouse };
        presentation::PresentationOwner menuEntryOwner{ presentation::PresentationOwner::KeyboardMouse };
        presentation::GameplayPresentationReasonCode reason{ presentation::GameplayPresentationReasonCode::None };
        bool preOutputPresentationHandoff{ false };
    };

    struct GameplayProjectionFrame
    {
        LegacyInputContextCompat context{ LegacyInputContextCompat::Gameplay };
        std::uint32_t contextRevision{ 0 };
        ChannelOwner lookOwner{ ChannelOwner::KeyboardMouse };
        ChannelOwner moveOwner{ ChannelOwner::KeyboardMouse };
        ChannelOwner combatOwner{ ChannelOwner::KeyboardMouse };
        ChannelOwner digitalOwner{ ChannelOwner::KeyboardMouse };
        GamepadOutputPlan gamepadPlan{};
        SustainedContributorDecision sprintDecision{};
        KeyboardHelperOutputPlan helperPlan{};
        GatePlan gatePlan{};
        RecoveryPlan recoveryPlan{};
        GameplayPresentationPlan presentationPlan{};
        DecisionReasonByChannel reasons{};
        ChannelArbitrationStateSet nextArbitration{};
    };

    struct GameplayPolicy
    {
        float lookEnterThreshold{ 0.25f };
        float lookSustainThreshold{ 0.15f };
        float moveEnterThreshold{ 0.25f };
        float moveSustainThreshold{ 0.15f };
        float triggerEnterThreshold{ 0.15f };
        float triggerSustainThreshold{ 0.08f };
        std::uint64_t outputTickUs{ 0 };
        std::uint64_t lastPhysicalMouseMoveOwnerUs{ 0 };
        std::uint64_t mouseLookQuietWindowUs{ 200'000 };
        bool gameplayContext{ true };
        bool mouseLookActive{ false };
        bool mouseLookActivatedThisFrame{ false };
        bool keyboardMoveActive{ false };
        bool keyboardMoveActivatedThisFrame{ false };
        bool keyboardMouseCombatActive{ false };
        bool keyboardMouseCombatActivatedThisFrame{ false };
        bool keyboardMouseDigitalActive{ false };
        bool keyboardMouseDigitalActivatedThisFrame{ false };
        bool keyboardPhysicalSustainedActive{ false };
        bool mousePhysicalSustainedActive{ false };
        std::uint64_t gamepadSustainedEventOrdinal{ 0 };
        std::uint64_t keyboardSustainedEventOrdinal{ 0 };
        std::uint64_t mouseSustainedEventOrdinal{ 0 };
        bool clearGamepadSustainedContributor{ false };
        ChannelArbitrationResetMode arbitrationResetMode{ ChannelArbitrationResetMode::None };
    };

    GameplayProjectionFrame ResolveGameplayProjection(
        const actions::KernelFrame& kernel,
        const actions::ResolvedActionFrame& resolved,
        const GameplayPolicy& policy,
        const GameplayProjectionFrame& previous,
        const GameplayRecoveryInput& recoveryInput);

}
