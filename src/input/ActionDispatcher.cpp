#include "pch.h"
#include "input/ActionDispatcher.h"

#include "input/Action.h"
#include "input/ActionExecutor.h"
#include "input/PadProfile.h"
#include "input/backend/ActionBackendPolicy.h"
#include "input/backend/ButtonEventBackend.h"
#include "input/backend/KeyboardNativeBackend.h"
#include "input/backend/NativeControlCode.h"
#include "input/injection/CompatibilityInputInjector.h"

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        bool IsSyntheticTouchpadPadEvent(const PadEvent& event)
        {
            switch (event.type) {
            case PadEventType::Gesture:
            case PadEventType::TouchpadPress:
            case PadEventType::TouchpadSlide:
                return IsSyntheticPadBitCode(event.code);
            default:
                return false;
            }
        }

        // Compatibility fallback injects synthetic pad bits, so it must use the
        // semantic virtual gamepad button for the resolved action instead of the
        // user's preferred source binding alias.
        std::uint32_t ResolveSemanticCompatibilityBit(
            std::string_view actionId,
            const PadBits& bits)
        {
            if (actionId == actions::Jump) {
                return bits.triangle;
            }
            if (actionId == actions::Activate) {
                return bits.cross;
            }
            if (actionId == actions::Attack) {
                return bits.r2Button;
            }
            if (actionId == actions::Block) {
                return bits.l2Button;
            }
            if (actionId == actions::Sprint) {
                return bits.l1;
            }
            if (actionId == actions::Sneak) {
                return bits.l3;
            }
            if (actionId == actions::Shout) {
                return bits.r1;
            }
            if (actionId == actions::TogglePOV) {
                return bits.r3;
            }

            if (actionId == actions::MenuConfirm || actionId == actions::ConsoleExecute) {
                return bits.cross;
            }

            if (actionId == actions::MenuCancel || actionId == actions::BookClose) {
                return bits.circle;
            }

            if (actionId == actions::MenuScrollUp ||
                actionId == actions::DialoguePreviousOption ||
                actionId == actions::FavoritesPreviousItem ||
                actionId == actions::ConsoleHistoryUp) {
                return bits.dpadUp;
            }

            if (actionId == actions::MenuScrollDown ||
                actionId == actions::DialogueNextOption ||
                actionId == actions::FavoritesNextItem ||
                actionId == actions::ConsoleHistoryDown) {
                return bits.dpadDown;
            }
            if (actionId == actions::MenuLeft ||
                actionId == actions::BookPreviousPage) {
                return bits.dpadLeft;
            }
            if (actionId == actions::MenuRight ||
                actionId == actions::BookNextPage) {
                return bits.dpadRight;
            }

            if (actionId == actions::MenuPageUp ||
                actionId == actions::MenuSortByName) {
                return bits.l1;
            }

            if (actionId == actions::MenuPageDown ||
                actionId == actions::MenuSortByValue) {
                return bits.r1;
            }

            return 0;
        }

        ActionDispatchResult TryDispatchLifecycleBackend(
            backend::IActionLifecycleBackend& backend,
            std::string_view actionId,
            backend::ActionOutputContract contract,
            InputContext context,
            ActionDispatchTarget target)
        {
            if (!backend.IsRouteActive() ||
                !backend.CanHandleAction(actionId) ||
                !backend.TriggerAction(actionId, contract, context)) {
                return {};
            }

            return { true, target };
        }

        ActionDispatchResult TryDispatchLifecycleBackendState(
            backend::IActionLifecycleBackend& backend,
            std::string_view actionId,
            backend::ActionOutputContract contract,
            bool down,
            float heldSeconds,
            InputContext context,
            ActionDispatchTarget target)
        {
            if (!backend.IsRouteActive() ||
                !backend.CanHandleAction(actionId) ||
                !backend.SubmitActionState(actionId, contract, down, heldSeconds, context)) {
                return {};
            }

            return { true, target };
        }

        ActionDispatchResult TryDispatchPlannedLifecycleBackend(
            backend::IActionLifecycleBackend& lifecycleBackend,
            const backend::PlannedAction& action,
            ActionDispatchTarget target)
        {
            switch (action.phase) {
            case backend::PlannedActionPhase::Pulse:
                return TryDispatchLifecycleBackend(
                    lifecycleBackend,
                    action.actionId,
                    action.contract,
                    action.context,
                    target);

            case backend::PlannedActionPhase::Press:
            case backend::PlannedActionPhase::Hold:
                return TryDispatchLifecycleBackendState(
                    lifecycleBackend,
                    action.actionId,
                    action.contract,
                    true,
                    action.heldSeconds,
                    action.context,
                    target);

            case backend::PlannedActionPhase::Release:
                return TryDispatchLifecycleBackendState(
                    lifecycleBackend,
                    action.actionId,
                    action.contract,
                    false,
                    action.heldSeconds,
                    action.context,
                    target);

            case backend::PlannedActionPhase::Value:
            case backend::PlannedActionPhase::None:
            default:
                return {};
            }
        }

        std::uint32_t ResolveSemanticBitFromNativeCode(
            backend::NativeControlCode code,
            const PadBits& bits)
        {
            switch (code) {
            case backend::NativeControlCode::Jump:
                return bits.triangle;
            case backend::NativeControlCode::Activate:
            case backend::NativeControlCode::MenuConfirm:
                return bits.cross;
            case backend::NativeControlCode::Attack:
                return bits.r2Button;
            case backend::NativeControlCode::Block:
                return bits.l2Button;
            case backend::NativeControlCode::Sprint:
            case backend::NativeControlCode::MenuPageUp:
                return bits.l1;
            case backend::NativeControlCode::Sneak:
                return bits.l3;
            case backend::NativeControlCode::Shout:
            case backend::NativeControlCode::MenuPageDown:
                return bits.r1;
            case backend::NativeControlCode::TogglePOV:
                return bits.r3;
            case backend::NativeControlCode::MenuCancel:
                return bits.circle;
            case backend::NativeControlCode::MenuScrollUp:
                return bits.dpadUp;
            case backend::NativeControlCode::MenuScrollDown:
                return bits.dpadDown;
            case backend::NativeControlCode::MenuLeft:
            case backend::NativeControlCode::BookPreviousPage:
                return bits.dpadLeft;
            case backend::NativeControlCode::MenuRight:
            case backend::NativeControlCode::BookNextPage:
                return bits.dpadRight;
            case backend::NativeControlCode::None:
            case backend::NativeControlCode::MoveStick:
            case backend::NativeControlCode::LookStick:
            case backend::NativeControlCode::MenuStick:
            case backend::NativeControlCode::LeftTriggerAxis:
            case backend::NativeControlCode::RightTriggerAxis:
            default:
                return 0;
            }
        }

        std::uint32_t ResolvePlannedCompatibilityBit(const backend::PlannedAction& action)
        {
            const auto& bits = GetPadBits(GetActivePadProfile());
            const auto plannedCode = static_cast<backend::NativeControlCode>(action.outputCode);
            if (const auto semanticBit = ResolveSemanticBitFromNativeCode(plannedCode, bits);
                semanticBit != 0) {
                return semanticBit;
            }

            return ResolveSemanticCompatibilityBit(action.actionId, bits);
        }

        ActionDispatchResult TryDispatchPlannedCompatibilityAction(
            const backend::PlannedAction& action,
            CompatibilityInputInjector& compatibilityInjector)
        {
            const auto pulseBit = ResolvePlannedCompatibilityBit(action);
            if (pulseBit == 0) {
                return {};
            }

            switch (action.phase) {
            case backend::PlannedActionPhase::Pulse:
                compatibilityInjector.PulseButton(pulseBit, action.actionId);
                return { true, ActionDispatchTarget::CompatibilityPulse };

            case backend::PlannedActionPhase::Press:
            case backend::PlannedActionPhase::Hold:
                compatibilityInjector.SetButtonState(pulseBit, true, action.actionId);
                return { true, ActionDispatchTarget::CompatibilityState };

            case backend::PlannedActionPhase::Release:
                compatibilityInjector.SetButtonState(pulseBit, false, action.actionId);
                return { true, ActionDispatchTarget::CompatibilityState };

            case backend::PlannedActionPhase::Value:
            case backend::PlannedActionPhase::None:
            default:
                return {};
            }
        }
    }

    ActionDispatcher::ActionDispatcher(
        CompatibilityInputInjector& compatibilityInjector) :
        _compatibilityInjector(compatibilityInjector)
    {
    }

    void ActionDispatcher::DispatchDirectPadEvent(const PadEvent& event) const
    {
        if (!IsSyntheticTouchpadPadEvent(event)) {
            return;
        }

        _compatibilityInjector.PulseButton(event.code, ToString(event.type));
    }

    ActionDispatchResult ActionDispatcher::DispatchPlannedAction(const backend::PlannedAction& action) const
    {
        switch (action.backend) {
        case backend::PlannedBackend::ButtonEvent:
            if (backend::ButtonEventBackend::GetSingleton().IsRouteActive() &&
                backend::ButtonEventBackend::GetSingleton().ApplyPlannedAction(action)) {
                return { true, ActionDispatchTarget::ButtonEvent };
            }
            return {};

        case backend::PlannedBackend::KeyboardNative:
            return TryDispatchPlannedLifecycleBackend(
                backend::KeyboardNativeBackend::GetSingleton(),
                action,
                ActionDispatchTarget::KeyboardNative);

        case backend::PlannedBackend::Plugin:
            if ((action.phase == backend::PlannedActionPhase::Pulse ||
                 action.phase == backend::PlannedActionPhase::Press) &&
                ActionExecutor::GetSingleton().Execute(action.actionId, action.context)) {
                return { true, ActionDispatchTarget::Plugin };
            }
            return {};

        case backend::PlannedBackend::CompatibilityFallback:
            return TryDispatchPlannedCompatibilityAction(
                action,
                _compatibilityInjector);

        case backend::PlannedBackend::NativeState:
        case backend::PlannedBackend::ModEvent:
        default:
            return {};
        }
    }

    std::string_view ToString(ActionDispatchTarget target)
    {
        switch (target) {
        case ActionDispatchTarget::ButtonEvent:
            return "ButtonEvent";
        case ActionDispatchTarget::KeyboardNative:
            return "KeyboardNative";
        case ActionDispatchTarget::CompatibilityPulse:
            return "CompatibilityPulse";
        case ActionDispatchTarget::CompatibilityState:
            return "CompatibilityState";
        case ActionDispatchTarget::Plugin:
            return "Plugin";
        default:
            return "None";
        }
    }
}
