#include "pch.h"
#include "input/injection/GameplayOwnershipCoordinator.h"

#include "input/GameplayKbmFactTracker.h"
#include "input/InputModalityTracker.h"
#include "input/RuntimeConfig.h"
#include "input/backend/FrameActionPlan.h"

#include <cmath>

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        constexpr float kMeaningfulRightStickEnterThreshold = 0.25f;
        constexpr float kMeaningfulRightStickSustainThreshold = 0.15f;
        constexpr float kMeaningfulLeftStickEnterThreshold = 0.25f;
        constexpr float kMeaningfulLeftStickSustainThreshold = 0.15f;
        constexpr float kMeaningfulTriggerEnterThreshold = 0.15f;
        constexpr float kMeaningfulTriggerSustainThreshold = 0.08f;
        constexpr std::uint64_t kKeyboardMouseExplicitLeaseMs = 1500;
        constexpr std::uint64_t kKeyboardMouseLookLeaseMs = 600;
        constexpr std::uint64_t kGamepadExplicitLeaseMs = 1500;
        constexpr std::uint64_t kGamepadMoveOnlyLeaseMs = 1200;

        std::uint64_t GetMonotonicMs()
        {
            return ::GetTickCount64();
        }

        bool ShouldLogGameplayOwnership()
        {
            const auto& config = RuntimeConfig::GetSingleton();
            return config.LogMappingEvents() || config.LogActionPlan() || config.LogNativeInjection();
        }
    }

    GameplayOwnershipCoordinator& GameplayOwnershipCoordinator::GetSingleton()
    {
        static GameplayOwnershipCoordinator instance;
        return instance;
    }

    void GameplayOwnershipCoordinator::Reset()
    {
        _lookOwner = ChannelOwner::KeyboardMouse;
        _moveOwner = ChannelOwner::KeyboardMouse;
        _combatOwner = ChannelOwner::KeyboardMouse;
        _digitalOwner = ChannelOwner::KeyboardMouse;
        _publishedLookOwner.store(ChannelOwner::KeyboardMouse, std::memory_order_relaxed);
        _publishedDigitalOwner.store(ChannelOwner::KeyboardMouse, std::memory_order_relaxed);
        _publishedGameplayPresentationOwner.store(ChannelOwner::KeyboardMouse, std::memory_order_relaxed);
        _publishedGameplayMenuEntryOwner.store(ChannelOwner::KeyboardMouse, std::memory_order_relaxed);
        _keyboardMouseExplicitLeaseUntilMs = 0;
        _keyboardMouseLookLeaseUntilMs = 0;
        _gamepadExplicitLeaseUntilMs = 0;
        _gamepadMoveOnlyLeaseUntilMs = 0;
    }

    GameplayOwnershipCoordinator::ChannelOwner GameplayOwnershipCoordinator::GetPublishedLookOwner() const
    {
        return _publishedLookOwner.load(std::memory_order_relaxed);
    }

    GameplayOwnershipCoordinator::ChannelOwner GameplayOwnershipCoordinator::GetPublishedDigitalOwner() const
    {
        return _publishedDigitalOwner.load(std::memory_order_relaxed);
    }

    GameplayOwnershipCoordinator::ChannelOwner GameplayOwnershipCoordinator::GetPublishedGameplayPresentationOwner() const
    {
        return _publishedGameplayPresentationOwner.load(std::memory_order_relaxed);
    }

    GameplayOwnershipCoordinator::ChannelOwner GameplayOwnershipCoordinator::GetPublishedGameplayMenuEntryOwner() const
    {
        return _publishedGameplayMenuEntryOwner.load(std::memory_order_relaxed);
    }

    GameplayOwnershipCoordinator::GameplayPresentationState
        GameplayOwnershipCoordinator::GetPublishedGameplayPresentationState() const
    {
        return GameplayPresentationState{
            .engineOwner = _publishedGameplayPresentationOwner.load(std::memory_order_relaxed),
            .menuEntryOwner = _publishedGameplayMenuEntryOwner.load(std::memory_order_relaxed)
        };
    }

    void GameplayOwnershipCoordinator::RefreshPublishedGameplayPresentation(InputContext context)
    {
        const auto facts = GameplayKbmFactTracker::GetSingleton().GetFacts();
        UpdatePublishedGameplayPresentationState(context, facts);
    }

    void GameplayOwnershipCoordinator::RecordGameplayPresentationHint(
        InputContext context,
        PresentationHint hint,
        std::string_view reason)
    {
        if (!IsGameplayDomainContext(context)) {
            return;
        }

        RefreshPresentationLease(hint, reason);
        RefreshPublishedGameplayPresentation(context);
    }

    GameplayOwnershipCoordinator::DigitalGatePlan GameplayOwnershipCoordinator::UpdateDigitalOwnership(
        InputContext context,
        const backend::FrameActionPlan& framePlan)
    {
        DigitalGatePlan plan{};
        if (!IsGameplayDomainContext(context)) {
            ResetForNonGameplay();
            return plan;
        }

        const auto facts = GameplayKbmFactTracker::GetSingleton().GetFacts();
        auto& tracker = InputModalityTracker::GetSingleton();
        const auto previous = _digitalOwner;
        if (facts.IsKeyboardMouseDigitalActive()) {
            SetChannelOwner(_digitalOwner, "Digital", ChannelOwner::KeyboardMouse, context, "kbm-digital-active");
        } else if (HasMeaningfulGamepadDigitalAction(framePlan)) {
            SetChannelOwner(_digitalOwner, "Digital", ChannelOwner::Gamepad, context, "planned-gamepad-digital");
            RefreshPresentationLease(PresentationHint::GamepadExplicit, "planned-gamepad-digital");
        } else if (tracker.IsGameplayUsingGamepad()) {
            SetChannelOwner(_digitalOwner, "Digital", ChannelOwner::Gamepad, context, "gameplay-owner-gamepad");
        }

        _publishedDigitalOwner.store(_digitalOwner, std::memory_order_relaxed);
        UpdatePublishedGameplayPresentationState(context, facts);
        plan.suppressNewTransientActions = _digitalOwner == ChannelOwner::KeyboardMouse;
        if (previous == ChannelOwner::Gamepad && _digitalOwner == ChannelOwner::KeyboardMouse) {
            plan.cancelExistingTransientActions = true;
        }
        return plan;
    }

    GameplayOwnershipCoordinator::OwnershipDecision GameplayOwnershipCoordinator::ApplyOwnership(
        const ProjectedAnalogState& analog,
        const SyntheticPadFrame& frame,
        InputContext context)
    {
        OwnershipDecision decision{};
        decision.analog = analog;
        decision.lookOwner = _lookOwner;
        decision.moveOwner = _moveOwner;
        decision.combatOwner = _combatOwner;
        decision.digitalOwner = _digitalOwner;

        if (!IsGameplayDomainContext(context)) {
            ResetForNonGameplay();
            decision.lookOwner = _lookOwner;
            decision.moveOwner = _moveOwner;
            decision.combatOwner = _combatOwner;
            decision.digitalOwner = _digitalOwner;
            return decision;
        }

        const auto facts = GameplayKbmFactTracker::GetSingleton().GetFacts();
        if (facts.mouseLookActive) {
            SetChannelOwner(_lookOwner, "Look", ChannelOwner::KeyboardMouse, context, "mouse-look-active");
        } else if (IsMeaningfulGamepadLook(frame)) {
            SetChannelOwner(_lookOwner, "Look", ChannelOwner::Gamepad, context, "meaningful-right-stick");
            RefreshPresentationLease(PresentationHint::GamepadExplicit, "meaningful-right-stick");
        }

        if (facts.IsKeyboardMoveActive()) {
            SetChannelOwner(_moveOwner, "Move", ChannelOwner::KeyboardMouse, context, "keyboard-move-active");
        } else if (IsMeaningfulGamepadMove(frame)) {
            SetChannelOwner(_moveOwner, "Move", ChannelOwner::Gamepad, context, "meaningful-left-stick");
            RefreshPresentationLease(PresentationHint::GamepadMoveOnly, "meaningful-left-stick");
        }

        if (facts.IsKeyboardMouseCombatActive()) {
            SetChannelOwner(_combatOwner, "Combat", ChannelOwner::KeyboardMouse, context, "kbm-combat-active");
        } else if (IsMeaningfulGamepadCombat(frame)) {
            SetChannelOwner(_combatOwner, "Combat", ChannelOwner::Gamepad, context, "meaningful-trigger");
            RefreshPresentationLease(PresentationHint::GamepadExplicit, "meaningful-trigger");
        }

        decision.lookOwner = _lookOwner;
        decision.moveOwner = _moveOwner;
        decision.combatOwner = _combatOwner;
        decision.digitalOwner = _digitalOwner;
        _publishedLookOwner.store(_lookOwner, std::memory_order_relaxed);
        UpdatePublishedGameplayPresentationState(context, facts);

        if (_lookOwner == ChannelOwner::KeyboardMouse &&
            (decision.analog.lookX != 0.0f || decision.analog.lookY != 0.0f)) {
            decision.analog.lookX = 0.0f;
            decision.analog.lookY = 0.0f;
            decision.lookSuppressed = true;
        }
        if (_moveOwner == ChannelOwner::KeyboardMouse &&
            (decision.analog.moveX != 0.0f || decision.analog.moveY != 0.0f)) {
            decision.analog.moveX = 0.0f;
            decision.analog.moveY = 0.0f;
            decision.moveSuppressed = true;
        }
        if (_combatOwner == ChannelOwner::KeyboardMouse && decision.analog.leftTrigger != 0.0f) {
            decision.analog.leftTrigger = 0.0f;
            decision.leftTriggerSuppressed = true;
        }
        if (_combatOwner == ChannelOwner::KeyboardMouse && decision.analog.rightTrigger != 0.0f) {
            decision.analog.rightTrigger = 0.0f;
            decision.rightTriggerSuppressed = true;
        }

        if ((decision.lookSuppressed || decision.moveSuppressed ||
                decision.leftTriggerSuppressed || decision.rightTriggerSuppressed) &&
            ShouldLogGameplayOwnership()) {
            logger::info(
                "[DualPad][GameplayOwner] Suppressed synthetic analog (ctx={}, ts={}, lookOwner={}, moveOwner={}, combatOwner={}, leftStick=({:.3f},{:.3f}), rightStick=({:.3f},{:.3f}), triggers=({:.3f},{:.3f}))",
                dualpad::input::ToString(context),
                frame.sourceTimestampUs,
                ToString(_lookOwner),
                ToString(_moveOwner),
                ToString(_combatOwner),
                frame.leftStickX.value,
                frame.leftStickY.value,
                frame.rightStickX.value,
                frame.rightStickY.value,
                frame.leftTrigger.value,
                frame.rightTrigger.value);
        }

        return decision;
    }

    void GameplayOwnershipCoordinator::ResetForNonGameplay()
    {
        _lookOwner = ChannelOwner::KeyboardMouse;
        _moveOwner = ChannelOwner::KeyboardMouse;
        _combatOwner = ChannelOwner::KeyboardMouse;
        _digitalOwner = ChannelOwner::KeyboardMouse;
        _publishedLookOwner.store(ChannelOwner::KeyboardMouse, std::memory_order_relaxed);
        _publishedDigitalOwner.store(ChannelOwner::KeyboardMouse, std::memory_order_relaxed);
        _publishedGameplayPresentationOwner.store(ChannelOwner::KeyboardMouse, std::memory_order_relaxed);
        _publishedGameplayMenuEntryOwner.store(ChannelOwner::KeyboardMouse, std::memory_order_relaxed);
        _keyboardMouseExplicitLeaseUntilMs = 0;
        _keyboardMouseLookLeaseUntilMs = 0;
        _gamepadExplicitLeaseUntilMs = 0;
        _gamepadMoveOnlyLeaseUntilMs = 0;
    }

    void GameplayOwnershipCoordinator::RefreshPresentationLease(PresentationHint hint, std::string_view)
    {
        const auto nowMs = GetMonotonicMs();
        switch (hint) {
        case PresentationHint::KeyboardMouseExplicit:
            _keyboardMouseExplicitLeaseUntilMs = nowMs + kKeyboardMouseExplicitLeaseMs;
            break;
        case PresentationHint::KeyboardMouseLookOnly:
            _keyboardMouseLookLeaseUntilMs = nowMs + kKeyboardMouseLookLeaseMs;
            break;
        case PresentationHint::GamepadExplicit:
            _gamepadExplicitLeaseUntilMs = nowMs + kGamepadExplicitLeaseMs;
            break;
        case PresentationHint::GamepadMoveOnly:
            _gamepadMoveOnlyLeaseUntilMs = nowMs + kGamepadMoveOnlyLeaseMs;
            break;
        default:
            break;
        }
    }

    bool GameplayOwnershipCoordinator::IsPresentationLeaseActive(
        PresentationHint hint,
        std::uint64_t nowMs) const
    {
        switch (hint) {
        case PresentationHint::KeyboardMouseExplicit:
            return nowMs <= _keyboardMouseExplicitLeaseUntilMs;
        case PresentationHint::KeyboardMouseLookOnly:
            return nowMs <= _keyboardMouseLookLeaseUntilMs;
        case PresentationHint::GamepadExplicit:
            return nowMs <= _gamepadExplicitLeaseUntilMs;
        case PresentationHint::GamepadMoveOnly:
            return nowMs <= _gamepadMoveOnlyLeaseUntilMs;
        default:
            return false;
        }
    }

    void GameplayOwnershipCoordinator::SetChannelOwner(
        ChannelOwner& channelSlot,
        std::string_view channelName,
        ChannelOwner owner,
        InputContext context,
        std::string_view reason)
    {
        if (channelSlot == owner) {
            return;
        }

        const auto previous = channelSlot;
        channelSlot = owner;

        if (ShouldLogGameplayOwnership()) {
            logger::info(
                "[DualPad][GameplayOwner] {} {} -> {} via {} (ctx={})",
                channelName,
                ToString(previous),
                ToString(owner),
                reason,
                dualpad::input::ToString(context));
        }
    }

    bool GameplayOwnershipCoordinator::IsMeaningfulGamepadLook(const SyntheticPadFrame& frame) const
    {
        const auto threshold =
            _lookOwner == ChannelOwner::Gamepad ?
            kMeaningfulRightStickSustainThreshold :
            kMeaningfulRightStickEnterThreshold;
        return std::fabs(frame.rightStickX.value) >= threshold ||
            std::fabs(frame.rightStickY.value) >= threshold;
    }

    bool GameplayOwnershipCoordinator::IsMeaningfulGamepadMove(const SyntheticPadFrame& frame) const
    {
        const auto threshold =
            _moveOwner == ChannelOwner::Gamepad ?
            kMeaningfulLeftStickSustainThreshold :
            kMeaningfulLeftStickEnterThreshold;
        return std::fabs(frame.leftStickX.value) >= threshold ||
            std::fabs(frame.leftStickY.value) >= threshold;
    }

    bool GameplayOwnershipCoordinator::IsMeaningfulGamepadCombat(const SyntheticPadFrame& frame) const
    {
        const auto threshold =
            _combatOwner == ChannelOwner::Gamepad ?
            kMeaningfulTriggerSustainThreshold :
            kMeaningfulTriggerEnterThreshold;
        return std::fabs(frame.leftTrigger.value) >= threshold ||
            std::fabs(frame.rightTrigger.value) >= threshold;
    }

    bool GameplayOwnershipCoordinator::HasMeaningfulGamepadDigitalAction(
        const backend::FrameActionPlan& framePlan) const
    {
        for (const auto& action : framePlan) {
            if (action.context != InputContext::Gameplay ||
                !action.gateAware ||
                action.backend != backend::PlannedBackend::NativeButtonCommit ||
                action.kind != backend::PlannedActionKind::NativeButton) {
                continue;
            }

            switch (action.digitalPolicy) {
            case backend::NativeDigitalPolicyKind::PulseMinDown:
            case backend::NativeDigitalPolicyKind::ToggleDebounced:
                if (action.phase == backend::PlannedActionPhase::Pulse ||
                    action.phase == backend::PlannedActionPhase::Press) {
                    return true;
                }
                break;

            case backend::NativeDigitalPolicyKind::HoldOwner:
            case backend::NativeDigitalPolicyKind::RepeatOwner:
                break;

            case backend::NativeDigitalPolicyKind::None:
            default:
                break;
            }
        }

        return false;
    }

    void GameplayOwnershipCoordinator::UpdatePublishedGameplayPresentationState(
        InputContext context,
        const GameplayKbmFacts& facts)
    {
        if (!IsGameplayDomainContext(context)) {
            _publishedGameplayPresentationOwner.store(ChannelOwner::KeyboardMouse, std::memory_order_relaxed);
            _publishedGameplayMenuEntryOwner.store(ChannelOwner::KeyboardMouse, std::memory_order_relaxed);
            return;
        }

        const auto nowMs = GetMonotonicMs();
        const bool keyboardMouseExplicitActive =
            IsPresentationLeaseActive(PresentationHint::KeyboardMouseExplicit, nowMs) ||
            facts.IsKeyboardMoveActive() ||
            facts.IsKeyboardMouseCombatActive() ||
            facts.IsKeyboardMouseDigitalActive() ||
            facts.IsKeyboardMouseSprintActive();
        const bool keyboardMouseLookActive =
            facts.mouseLookActive ||
            IsPresentationLeaseActive(PresentationHint::KeyboardMouseLookOnly, nowMs);
        const bool engineGamepadActive =
            IsPresentationLeaseActive(PresentationHint::GamepadExplicit, nowMs);
        const bool menuEntryGamepadActive =
            engineGamepadActive ||
            IsPresentationLeaseActive(PresentationHint::GamepadMoveOnly, nowMs);

        auto resolvedEngine =
            _publishedGameplayPresentationOwner.load(std::memory_order_relaxed);
        auto resolvedMenuEntry =
            _publishedGameplayMenuEntryOwner.load(std::memory_order_relaxed);
        if (keyboardMouseExplicitActive || keyboardMouseLookActive) {
            resolvedEngine = ChannelOwner::KeyboardMouse;
        } else if (engineGamepadActive) {
            resolvedEngine = ChannelOwner::Gamepad;
        }

        if (keyboardMouseExplicitActive) {
            resolvedMenuEntry = ChannelOwner::KeyboardMouse;
        } else if (menuEntryGamepadActive) {
            resolvedMenuEntry = ChannelOwner::Gamepad;
        } else if (keyboardMouseLookActive) {
            resolvedMenuEntry = ChannelOwner::KeyboardMouse;
        }

        const auto previousEngine =
            _publishedGameplayPresentationOwner.exchange(resolvedEngine, std::memory_order_relaxed);
        const auto previousMenuEntry =
            _publishedGameplayMenuEntryOwner.exchange(resolvedMenuEntry, std::memory_order_relaxed);
        if ((previousEngine != resolvedEngine || previousMenuEntry != resolvedMenuEntry) &&
            ShouldLogGameplayOwnership()) {
            logger::info(
                "[DualPad][GameplayOwner] Presentation engine {} -> {} menuEntry {} -> {} (ctx={}, look={}, move={}, combat={}, digital={}, mouseLook={}, kbmMove={}, kbmCombat={}, kbmDigital={}, kbmSprint={}, gameplayOwner={})",
                ToString(previousEngine),
                ToString(resolvedEngine),
                ToString(previousMenuEntry),
                ToString(resolvedMenuEntry),
                dualpad::input::ToString(context),
                ToString(_lookOwner),
                ToString(_moveOwner),
                ToString(_combatOwner),
                ToString(_digitalOwner),
                facts.mouseLookActive,
                facts.IsKeyboardMoveActive(),
                facts.IsKeyboardMouseCombatActive(),
                facts.IsKeyboardMouseDigitalActive(),
                facts.IsKeyboardMouseSprintActive(),
                InputModalityTracker::GetSingleton().IsGameplayUsingGamepad() ? "Gamepad" : "KeyboardMouse");
        }
    }

    bool GameplayOwnershipCoordinator::IsGameplayDomainContext(InputContext context) const
    {
        const auto value = static_cast<std::uint16_t>(context);
        return !((value >= 100 && value < 2000) || context == InputContext::Console);
    }

    std::string_view GameplayOwnershipCoordinator::ToString(ChannelOwner owner)
    {
        switch (owner) {
        case ChannelOwner::Gamepad:
            return "Gamepad";
        case ChannelOwner::KeyboardMouse:
        default:
            return "KeyboardMouse";
        }
    }

    std::string_view GameplayOwnershipCoordinator::ToString(PresentationHint hint)
    {
        switch (hint) {
        case PresentationHint::KeyboardMouseExplicit:
            return "KeyboardMouseExplicit";
        case PresentationHint::KeyboardMouseLookOnly:
            return "KeyboardMouseLookOnly";
        case PresentationHint::GamepadExplicit:
            return "GamepadExplicit";
        case PresentationHint::GamepadMoveOnly:
            return "GamepadMoveOnly";
        default:
            return "Unknown";
        }
    }
}
