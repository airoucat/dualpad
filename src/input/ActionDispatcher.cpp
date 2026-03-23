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
            return TryDispatchPlannedLifecycleBackend(
                backend::KeyboardHelperBackend::GetSingleton(),
                action,
                ActionDispatchTarget::KeyboardHelper);

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
                return TryDispatchPlannedLifecycleBackend(
                    backend::KeyboardHelperBackend::GetSingleton(),
                    translatedAction,
                    ActionDispatchTarget::KeyboardHelper);
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

