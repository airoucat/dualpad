#include "pch.h"
#include "input/ActionDispatcher.h"

#include "input/Action.h"
#include "input/ActionExecutor.h"
#include "input/PadProfile.h"
#include "input/RuntimeConfig.h"
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
            return actionId == actions::Sprint;
        }

        bool ShouldPreferKeyboardNative(std::string_view actionId)
        {
            return RuntimeConfig::GetSingleton().TestKeyboardAcceptDumpRoute() &&
                actionId == actions::MenuConfirm;
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
        auto& keyboardBackend = backend::KeyboardNativeBackend::GetSingleton();
        if (ShouldPreferKeyboardNative(actionId) &&
            keyboardBackend.IsRouteActive() &&
            keyboardBackend.CanHandleAction(actionId) &&
            keyboardBackend.PulseAction(actionId, context)) {
            return { true, ActionDispatchTarget::KeyboardNative };
        }

        if (const auto pulseBit = ResolveCompatibilityPulseBit(actionId); pulseBit != 0) {
            if (_nativeInjector.ShouldUseForButtonActions() &&
                _nativeInjector.IsAvailable()) {
                if (_nativeInjector.CanHandleAction(actionId) &&
                    _nativeInjector.PulseButtonAction(actionId)) {
                    logger::info("[DualPad][Dispatch] Routed action '{}' through native mapped pulse injection", actionId);
                    return { true, ActionDispatchTarget::NativePulse };
                }

                if (_nativeInjector.CanHandleRawPadButton(pulseBit) &&
                    _nativeInjector.PulseRawPadButton(pulseBit, context)) {
                    logger::info("[DualPad][Dispatch] Routed action '{}' through native raw pulse injection", actionId);
                    return { true, ActionDispatchTarget::NativePulse };
                }
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
        auto& keyboardBackend = backend::KeyboardNativeBackend::GetSingleton();
        if (ShouldPreferKeyboardNative(actionId) &&
            keyboardBackend.IsRouteActive() &&
            keyboardBackend.CanHandleAction(actionId) &&
            keyboardBackend.QueueAction(actionId, down, heldSeconds, context)) {
            return { true, ActionDispatchTarget::KeyboardNative };
        }

        if (const auto pulseBit = ResolveCompatibilityPulseBit(actionId); pulseBit != 0) {
            if (!ShouldPreferCompatibilityState(actionId) &&
                _nativeInjector.ShouldUseForButtonActions() &&
                _nativeInjector.IsAvailable()) {
                if (_nativeInjector.CanHandleAction(actionId) &&
                    _nativeInjector.QueueButtonAction(actionId, down, heldSeconds)) {
                    return { true, ActionDispatchTarget::NativeState };
                }

                if (_nativeInjector.CanHandleRawPadButton(pulseBit) &&
                    _nativeInjector.QueueRawPadButton(pulseBit, context, down, heldSeconds)) {
                    return { true, ActionDispatchTarget::NativeState };
                }
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

        if (actionId == actions::Jump) {
            return bits.jump;
        }
        if (actionId == actions::Activate) {
            return bits.activate;
        }
        if (actionId == actions::Sprint) {
            return bits.sprint;
        }
        if (actionId == actions::Attack) {
            return bits.attack;
        }
        if (actionId == actions::Sneak) {
            return bits.sneak;
        }
        if (actionId == actions::Shout) {
            return bits.r2Button;
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

        if (actionId == actions::MenuLeft) {
            return bits.dpadLeft;
        }

        if (actionId == actions::MenuRight) {
            return bits.dpadRight;
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

    std::string_view ToString(ActionDispatchTarget target)
    {
        switch (target) {
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
