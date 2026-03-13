#include "pch.h"
#include "input/ActionDispatcher.h"

#include "input/Action.h"
#include "input/ActionExecutor.h"
#include "input/PadProfile.h"
#include "input/backend/ActionBackendPolicy.h"
#include "input/backend/ButtonEventBackend.h"
#include "input/backend/KeyboardNativeBackend.h"
#include "input/injection/CompatibilityInputInjector.h"
#include "input/injection/NativeInputInjector.h"

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        bool ShouldPreferCompatibilityState(std::string_view actionId)
        {
            return actionId == actions::Sprint ||
                actionId == actions::MenuScrollUp ||
                actionId == actions::MenuScrollDown ||
                actionId == actions::MenuPageUp ||
                actionId == actions::MenuPageDown;
        }

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

            if (actionId == actions::MenuConfirm || actionId == "Console.Execute"sv) {
                return bits.cross;
            }

            if (actionId == actions::MenuCancel || actionId == "Book.Close"sv) {
                return bits.circle;
            }

            if (actionId == actions::MenuScrollUp ||
                actionId == "Dialogue.PreviousOption"sv ||
                actionId == "Favorites.PreviousItem"sv ||
                actionId == "Console.HistoryUp"sv) {
                return bits.dpadUp;
            }

            if (actionId == actions::MenuScrollDown ||
                actionId == "Dialogue.NextOption"sv ||
                actionId == "Favorites.NextItem"sv ||
                actionId == "Console.HistoryDown"sv) {
                return bits.dpadDown;
            }

            if (actionId == actions::MenuPageUp ||
                actionId == "Book.PreviousPage"sv ||
                actionId == "Menu.SortByName"sv) {
                return bits.l1;
            }

            if (actionId == actions::MenuPageDown ||
                actionId == "Book.NextPage"sv ||
                actionId == "Menu.SortByValue"sv) {
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
    }

    ActionDispatcher::ActionDispatcher(
        CompatibilityInputInjector& compatibilityInjector,
        NativeInputInjector& nativeInjector) :
        _compatibilityInjector(compatibilityInjector),
        _nativeInjector(nativeInjector)
    {
    }

    void ActionDispatcher::DispatchDirectPadEvent(const PadEvent& event) const
    {
        if (!IsSyntheticTouchpadPadEvent(event)) {
            return;
        }

        _compatibilityInjector.PulseButton(event.code, ToString(event.type));
    }

    ActionDispatchResult ActionDispatcher::Dispatch(
        std::string_view actionId,
        InputContext context) const
    {
        const auto routingDecision = backend::ActionBackendPolicy::Decide(actionId);
        if (routingDecision.backend == backend::PlannedBackend::ButtonEvent) {
            if (const auto result = TryDispatchLifecycleBackend(
                    backend::ButtonEventBackend::GetSingleton(),
                    actionId,
                    routingDecision.contract,
                    context,
                    ActionDispatchTarget::ButtonEvent);
                result.handled) {
                return result;
            }
        }

        if (routingDecision.backend == backend::PlannedBackend::KeyboardNative) {
            if (const auto result = TryDispatchLifecycleBackend(
                    backend::KeyboardNativeBackend::GetSingleton(),
                    actionId,
                    routingDecision.contract,
                    context,
                    ActionDispatchTarget::KeyboardNative);
                result.handled) {
                return result;
            }
        }

        if (const auto pulseBit = ResolveCompatibilityPulseBit(actionId); pulseBit != 0) {
            if (_nativeInjector.ShouldUseForButtonActions() &&
                _nativeInjector.IsAvailable() &&
                _nativeInjector.CanHandleAction(actionId) &&
                _nativeInjector.PulseButtonAction(actionId)) {
                logger::info("[DualPad][Dispatch] Routed action '{}' through native pulse injection", actionId);
                return { true, ActionDispatchTarget::NativePulse };
            }

            _compatibilityInjector.PulseButton(pulseBit, actionId);
            return { true, ActionDispatchTarget::CompatibilityPulse };
        }

        if (ActionExecutor::GetSingleton().Execute(actionId, context)) {
            return { true, ActionDispatchTarget::Plugin };
        }

        return {};
    }

    ActionDispatchResult ActionDispatcher::DispatchButtonState(
        std::string_view actionId,
        bool down,
        float heldSeconds,
        InputContext context) const
    {
        const auto routingDecision = backend::ActionBackendPolicy::Decide(actionId);
        if (routingDecision.backend == backend::PlannedBackend::ButtonEvent) {
            if (const auto result = TryDispatchLifecycleBackendState(
                    backend::ButtonEventBackend::GetSingleton(),
                    actionId,
                    routingDecision.contract,
                    down,
                    heldSeconds,
                    context,
                    ActionDispatchTarget::ButtonEvent);
                result.handled) {
                return result;
            }
        }

        if (routingDecision.backend == backend::PlannedBackend::KeyboardNative) {
            if (const auto result = TryDispatchLifecycleBackendState(
                    backend::KeyboardNativeBackend::GetSingleton(),
                    actionId,
                    routingDecision.contract,
                    down,
                    heldSeconds,
                    context,
                    ActionDispatchTarget::KeyboardNative);
                result.handled) {
                return result;
            }
        }

        if (const auto pulseBit = ResolveCompatibilityPulseBit(actionId); pulseBit != 0) {
            if (!ShouldPreferCompatibilityState(actionId) &&
                _nativeInjector.ShouldUseForButtonActions() &&
                _nativeInjector.IsAvailable() &&
                _nativeInjector.CanHandleAction(actionId) &&
                _nativeInjector.QueueButtonAction(actionId, down, heldSeconds)) {
                return { true, ActionDispatchTarget::NativeState };
            }

            _compatibilityInjector.SetButtonState(pulseBit, down, actionId);
            return { true, ActionDispatchTarget::CompatibilityState };
        }

        if (down && ActionExecutor::GetSingleton().Execute(actionId, context)) {
            return { true, ActionDispatchTarget::Plugin };
        }

        return {};
    }

    std::uint32_t ActionDispatcher::ResolveCompatibilityPulseBit(std::string_view actionId) const
    {
        const auto& bits = GetPadBits(GetActivePadProfile());
        return ResolveSemanticCompatibilityBit(actionId, bits);
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
        case ActionDispatchTarget::NativePulse:
            return "NativePulse";
        case ActionDispatchTarget::NativeState:
            return "NativeState";
        case ActionDispatchTarget::Plugin:
            return "Plugin";
        default:
            return "None";
        }
    }
}
