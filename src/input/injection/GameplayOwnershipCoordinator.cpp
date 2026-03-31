#include "pch.h"
#include "input/injection/GameplayOwnershipCoordinator.h"

#include "input/InputModalityTracker.h"
#include "input/RuntimeConfig.h"
#include "input/backend/FrameActionPlan.h"
#include "input/backend/NativeButtonCommitBackend.h"

#include <cmath>

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        constexpr float kMeaningfulRightStickThreshold = 0.25f;
        constexpr float kMeaningfulLeftStickThreshold = 0.25f;
        constexpr float kMeaningfulTriggerThreshold = 0.15f;

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
    }

    GameplayOwnershipCoordinator::ChannelOwner GameplayOwnershipCoordinator::GetPublishedLookOwner() const
    {
        return _publishedLookOwner.load(std::memory_order_relaxed);
    }

    GameplayOwnershipCoordinator::ChannelOwner GameplayOwnershipCoordinator::GetPublishedDigitalOwner() const
    {
        return _publishedDigitalOwner.load(std::memory_order_relaxed);
    }

    void GameplayOwnershipCoordinator::UpdateDigitalOwnership(
        InputContext context,
        const backend::FrameActionPlan& framePlan)
    {
        if (!IsGameplayDomainContext(context)) {
            ResetForNonGameplay();
            return;
        }

        auto& tracker = InputModalityTracker::GetSingleton();
        const auto previous = _digitalOwner;
        if (tracker.IsGameplayKeyboardMouseDigitalActive()) {
            SetChannelOwner(_digitalOwner, "Digital", ChannelOwner::KeyboardMouse, context, "kbm-digital-active");
        } else if (HasMeaningfulGamepadDigitalAction(framePlan)) {
            SetChannelOwner(_digitalOwner, "Digital", ChannelOwner::Gamepad, context, "planned-gamepad-digital");
        } else if (tracker.IsGameplayUsingGamepad()) {
            SetChannelOwner(_digitalOwner, "Digital", ChannelOwner::Gamepad, context, "gameplay-owner-gamepad");
        }

        _publishedDigitalOwner.store(_digitalOwner, std::memory_order_relaxed);
        if (previous == ChannelOwner::Gamepad && _digitalOwner == ChannelOwner::KeyboardMouse) {
            backend::NativeButtonCommitBackend::GetSingleton().ForceCancelGateAwareGameplayTransientActions();
        }
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

        auto& tracker = InputModalityTracker::GetSingleton();
        if (tracker.IsGameplayMouseLookActive()) {
            SetChannelOwner(_lookOwner, "Look", ChannelOwner::KeyboardMouse, context, "mouse-look-active");
        } else if (IsMeaningfulGamepadLook(frame)) {
            SetChannelOwner(_lookOwner, "Look", ChannelOwner::Gamepad, context, "meaningful-right-stick");
        }

        if (tracker.IsGameplayKeyboardMoveActive()) {
            SetChannelOwner(_moveOwner, "Move", ChannelOwner::KeyboardMouse, context, "keyboard-move-active");
        } else if (IsMeaningfulGamepadMove(frame)) {
            SetChannelOwner(_moveOwner, "Move", ChannelOwner::Gamepad, context, "meaningful-left-stick");
        }

        if (tracker.IsGameplayKeyboardMouseCombatActive()) {
            SetChannelOwner(_combatOwner, "Combat", ChannelOwner::KeyboardMouse, context, "kbm-combat-active");
        } else if (IsMeaningfulGamepadCombat(frame)) {
            SetChannelOwner(_combatOwner, "Combat", ChannelOwner::Gamepad, context, "meaningful-trigger");
        }

        decision.lookOwner = _lookOwner;
        decision.moveOwner = _moveOwner;
        decision.combatOwner = _combatOwner;
        decision.digitalOwner = _digitalOwner;
        _publishedLookOwner.store(_lookOwner, std::memory_order_relaxed);
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
        return std::fabs(frame.rightStickX.value) >= kMeaningfulRightStickThreshold ||
            std::fabs(frame.rightStickY.value) >= kMeaningfulRightStickThreshold;
    }

    bool GameplayOwnershipCoordinator::IsMeaningfulGamepadMove(const SyntheticPadFrame& frame) const
    {
        return std::fabs(frame.leftStickX.value) >= kMeaningfulLeftStickThreshold ||
            std::fabs(frame.leftStickY.value) >= kMeaningfulLeftStickThreshold;
    }

    bool GameplayOwnershipCoordinator::IsMeaningfulGamepadCombat(const SyntheticPadFrame& frame) const
    {
        return std::fabs(frame.leftTrigger.value) >= kMeaningfulTriggerThreshold ||
            std::fabs(frame.rightTrigger.value) >= kMeaningfulTriggerThreshold;
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
}
