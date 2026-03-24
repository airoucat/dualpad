#include "pch.h"
#include "input/ActionDispatcher.h"

#include "input/Action.h"
#include "input/ActionExecutor.h"
#include "input/PadProfile.h"
#include "input/backend/ActionBackendPolicy.h"
#include "input/backend/NativeButtonCommitBackend.h"
#include "input/backend/KeyboardHelperBackend.h"
#include "input/backend/ModEventKeyPool.h"

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        ActionDispatchResult DispatchKeyboardHelperAction(
            const backend::PlannedAction& action)
        {
            auto& keyboardHelper = backend::KeyboardHelperBackend::GetSingleton();
            if (!keyboardHelper.IsRouteActive() ||
                !keyboardHelper.CanHandleAction(action.actionId)) {
                return {};
            }

            bool handled = false;
            switch (action.phase) {
            case backend::PlannedActionPhase::Pulse:
                handled = keyboardHelper.TriggerAction(
                    action.actionId,
                    action.contract,
                    action.context);
                break;

            case backend::PlannedActionPhase::Press:
            case backend::PlannedActionPhase::Hold:
                handled = keyboardHelper.SubmitActionState(
                    action.actionId,
                    action.contract,
                    true,
                    action.heldSeconds,
                    action.context);
                break;

            case backend::PlannedActionPhase::Release:
                handled = keyboardHelper.SubmitActionState(
                    action.actionId,
                    action.contract,
                    false,
                    action.heldSeconds,
                    action.context);
                break;

            case backend::PlannedActionPhase::Value:
            case backend::PlannedActionPhase::None:
            default:
                handled = false;
                break;
            }

            return handled ? ActionDispatchResult{ true, ActionDispatchTarget::KeyboardHelper } : ActionDispatchResult{};
        }

        std::optional<std::string_view> ResolveModEventKeyPoolAction(std::string_view actionId)
        {
            if (const auto* slot = backend::FindModEventKeySlot(actionId)) {
                return slot->helperActionId;
            }

            return std::nullopt;
        }
    }

    ActionDispatchResult ActionDispatcher::DispatchPlannedAction(const backend::PlannedAction& action) const
    {
        switch (action.backend) {
        case backend::PlannedBackend::None:
            return {};

        case backend::PlannedBackend::NativeButtonCommit:
            if (backend::NativeButtonCommitBackend::GetSingleton().IsRouteActive() &&
                backend::NativeButtonCommitBackend::GetSingleton().ApplyPlannedAction(action)) {
                return { true, ActionDispatchTarget::NativeButtonCommit };
            }
            return {};

        case backend::PlannedBackend::KeyboardHelper:
            return DispatchKeyboardHelperAction(action);

        case backend::PlannedBackend::Plugin:
            if ((action.phase == backend::PlannedActionPhase::Pulse ||
                 action.phase == backend::PlannedActionPhase::Press) &&
                ActionExecutor::GetSingleton().Execute(action.actionId, action.context)) {
                return { true, ActionDispatchTarget::Plugin };
            }
            return {};

        case backend::PlannedBackend::ModEvent:
            if (const auto helperAction = ResolveModEventKeyPoolAction(action.actionId)) {
                auto translatedAction = action;
                translatedAction.backend = backend::PlannedBackend::KeyboardHelper;
                translatedAction.kind = backend::PlannedActionKind::KeyboardKey;
                translatedAction.actionId = *helperAction;
                return DispatchKeyboardHelperAction(translatedAction);
            }

            logger::warn(
                "[DualPad][Dispatch] Unmapped mod event action={} phase={} context={}",
                action.actionId,
                backend::ToString(action.phase),
                ToString(action.context));
            return {};

        case backend::PlannedBackend::NativeState:
        default:
            return {};
        }
    }

    std::string_view ToString(ActionDispatchTarget target)
    {
        switch (target) {
        case ActionDispatchTarget::NativeButtonCommit:
            return "NativeButtonCommit";
        case ActionDispatchTarget::KeyboardHelper:
            return "KeyboardHelper";
        case ActionDispatchTarget::Plugin:
            return "Plugin";
        default:
            return "None";
        }
    }
}

