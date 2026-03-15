#include "pch.h"
#include "input/backend/ActionBackendPolicy.h"

#include "input/Action.h"

using namespace std::literals;

namespace dualpad::input::backend
{
    namespace
    {
        constexpr bool IsRepeatActionId(std::string_view actionId)
        {
            return actionId == actions::MenuScrollUp ||
                actionId == actions::MenuScrollDown ||
                actionId == actions::MenuLeft ||
                actionId == actions::MenuRight ||
                actionId == actions::DialoguePreviousOption ||
                actionId == actions::DialogueNextOption ||
                actionId == actions::FavoritesPreviousItem ||
                actionId == actions::FavoritesNextItem ||
                actionId == actions::ConsoleHistoryUp ||
                actionId == actions::ConsoleHistoryDown;
        }

        constexpr bool IsPluginActionId(std::string_view actionId)
        {
            return actionId == actions::OpenInventory ||
                actionId == actions::OpenMagic ||
                actionId == actions::OpenMap ||
                actionId == actions::OpenJournal ||
                actionId == actions::OpenFavorites ||
                actionId == actions::OpenSkills ||
                actionId == actions::ToggleHUD ||
                actionId == actions::Screenshot ||
                actionId == actions::Wait ||
                actionId == actions::QuickSave ||
                actionId == actions::QuickLoad;
        }

        constexpr NativeControlCode TryMapButtonEventButton(std::string_view actionId)
        {
            if (actionId == actions::Jump) {
                return NativeControlCode::Jump;
            }
            if (actionId == actions::Activate) {
                return NativeControlCode::Activate;
            }
            if (actionId == actions::Sprint) {
                return NativeControlCode::Sprint;
            }
            if (actionId == actions::Sneak) {
                return NativeControlCode::Sneak;
            }
            if (actionId == actions::Shout) {
                return NativeControlCode::Shout;
            }
            if (actionId == actions::TogglePOV) {
                return NativeControlCode::TogglePOV;
            }

            // Console.Execute currently reuses the same physical A/Cross
            // materialization as MenuConfirm. This is a project-side
            // approximation, not a vanilla one-to-one user-event identity.
            if (actionId == actions::MenuConfirm || actionId == actions::ConsoleExecute) {
                return NativeControlCode::MenuConfirm;
            }
            if (actionId == actions::MenuCancel || actionId == actions::BookClose) {
                return NativeControlCode::MenuCancel;
            }
            // Console history navigation currently reuses the DPad Up/Down
            // physical materialization from menu scroll. This is a project-side
            // approximation of the same hardware bit, not a native handler
            // identity claim.
            if (actionId == actions::MenuScrollUp ||
                actionId == actions::DialoguePreviousOption ||
                actionId == actions::FavoritesPreviousItem ||
                actionId == actions::ConsoleHistoryUp) {
                return NativeControlCode::MenuScrollUp;
            }
            if (actionId == actions::MenuScrollDown ||
                actionId == actions::DialogueNextOption ||
                actionId == actions::FavoritesNextItem ||
                actionId == actions::ConsoleHistoryDown) {
                return NativeControlCode::MenuScrollDown;
            }
            if (actionId == actions::MenuLeft) {
                return NativeControlCode::MenuLeft;
            }
            if (actionId == actions::MenuRight) {
                return NativeControlCode::MenuRight;
            }
            if (actionId == actions::MenuPageUp ||
                actionId == actions::MenuSortByName) {
                return NativeControlCode::MenuPageUp;
            }
            if (actionId == actions::MenuPageDown ||
                actionId == actions::MenuSortByValue) {
                return NativeControlCode::MenuPageDown;
            }
            if (actionId == actions::BookPreviousPage) {
                return NativeControlCode::BookPreviousPage;
            }
            if (actionId == actions::BookNextPage) {
                return NativeControlCode::BookNextPage;
            }

            return NativeControlCode::None;
        }

        constexpr NativeControlCode TryMapCompatibilityNativeButton(std::string_view actionId)
        {
            if (actionId == actions::Attack) {
                return NativeControlCode::Attack;
            }
            if (actionId == actions::Block) {
                return NativeControlCode::Block;
            }
            return NativeControlCode::None;
        }

        constexpr bool IsKeyboardHelperActionId(std::string_view actionId)
        {
            return actionId.starts_with("VirtualKey."sv) ||
                actionId.starts_with("FKey."sv);
        }

        constexpr bool IsModEventActionId(std::string_view actionId)
        {
            return actionId.starts_with("Mod."sv) ||
                actionId.starts_with("ModEvent"sv);
        }

        constexpr NativeControlCode TryMapNativeAxis(std::string_view actionId)
        {
            if (actionId == "Game.Move"sv) {
                return NativeControlCode::MoveStick;
            }
            if (actionId == "Game.Look"sv) {
                return NativeControlCode::LookStick;
            }
            if (actionId == "Menu.LeftStick"sv) {
                return NativeControlCode::MenuStick;
            }
            if (actionId == "Game.LeftTrigger"sv) {
                return NativeControlCode::LeftTriggerAxis;
            }
            if (actionId == "Game.RightTrigger"sv) {
                return NativeControlCode::RightTriggerAxis;
            }

            return NativeControlCode::None;
        }

        constexpr ActionOutputContract ResolveDigitalContract(std::string_view actionId)
        {
            if (actionId == actions::Sprint ||
                actionId == actions::Attack ||
                actionId == actions::Block ||
                actionId == actions::Shout) {
                return ActionOutputContract::Hold;
            }
            if (actionId == actions::Sneak) {
                return ActionOutputContract::Toggle;
            }
            if (IsRepeatActionId(actionId)) {
                return ActionOutputContract::Repeat;
            }

            return ActionOutputContract::Pulse;
        }

        constexpr ActionLifecyclePolicy ResolveLifecyclePolicy(std::string_view actionId, ActionOutputContract contract)
        {
            switch (contract) {
            case ActionOutputContract::Hold:
                return ActionLifecyclePolicy::HoldOwner;
            case ActionOutputContract::Toggle:
                return ActionLifecyclePolicy::ToggleOwner;
            case ActionOutputContract::Repeat:
                return ActionLifecyclePolicy::RepeatOwner;
            case ActionOutputContract::Axis:
                return ActionLifecyclePolicy::AxisValue;
            case ActionOutputContract::Pulse:
                return actionId == actions::Activate ?
                    ActionLifecyclePolicy::MinDownWindowPulse :
                    ActionLifecyclePolicy::DeferredPulse;
            case ActionOutputContract::None:
            default:
                return ActionLifecyclePolicy::None;
            }
        }

        constexpr bool ShouldOwnLifecycle(ActionLifecyclePolicy policy)
        {
            switch (policy) {
            case ActionLifecyclePolicy::HoldOwner:
            case ActionLifecyclePolicy::ToggleOwner:
            case ActionLifecyclePolicy::RepeatOwner:
            case ActionLifecyclePolicy::AxisValue:
                return true;
            case ActionLifecyclePolicy::DeferredPulse:
            case ActionLifecyclePolicy::MinDownWindowPulse:
            case ActionLifecyclePolicy::None:
            default:
                return false;
            }
        }
    }

    ActionRoutingDecision ActionBackendPolicy::Decide(std::string_view actionId)
    {
        if (IsPluginAction(actionId)) {
            return {
                .backend = PlannedBackend::Plugin,
                .kind = PlannedActionKind::PluginAction,
                .contract = ActionOutputContract::Pulse,
                .lifecyclePolicy = ActionLifecyclePolicy::None,
                .nativeCode = NativeControlCode::None,
                .ownsLifecycle = false
            };
        }

        if (IsKeyboardHelperActionId(actionId)) {
            return {
                .backend = PlannedBackend::KeyboardNative,
                .kind = PlannedActionKind::KeyboardKey,
                .contract = ActionOutputContract::Pulse,
                .lifecyclePolicy = ActionLifecyclePolicy::None,
                .nativeCode = NativeControlCode::None,
                .ownsLifecycle = false
            };
        }

        if (IsModEventActionId(actionId)) {
            return {
                .backend = PlannedBackend::ModEvent,
                .kind = PlannedActionKind::ModEvent,
                .contract = ActionOutputContract::Pulse,
                .lifecyclePolicy = ActionLifecyclePolicy::None,
                .nativeCode = NativeControlCode::None,
                .ownsLifecycle = false
            };
        }

        if (const auto nativeButton = TryMapButtonEventButton(actionId); nativeButton != NativeControlCode::None) {
            const auto contract = ResolveDigitalContract(actionId);
            const auto lifecyclePolicy = ResolveLifecyclePolicy(actionId, contract);
            return {
                .backend = PlannedBackend::ButtonEvent,
                .kind = PlannedActionKind::NativeButton,
                .contract = contract,
                .lifecyclePolicy = lifecyclePolicy,
                .nativeCode = nativeButton,
                .ownsLifecycle = ShouldOwnLifecycle(lifecyclePolicy)
            };
        }

        if (const auto nativeButton = TryMapCompatibilityNativeButton(actionId); nativeButton != NativeControlCode::None) {
            const auto contract = ResolveDigitalContract(actionId);
            const auto lifecyclePolicy = ResolveLifecyclePolicy(actionId, contract);
            return {
                .backend = PlannedBackend::CompatibilityFallback,
                .kind = PlannedActionKind::NativeButton,
                .contract = contract,
                .lifecyclePolicy = lifecyclePolicy,
                .nativeCode = nativeButton,
                .ownsLifecycle = ShouldOwnLifecycle(lifecyclePolicy)
            };
        }

        if (const auto axis = TryMapNativeAxis(actionId); axis != NativeControlCode::None) {
            const auto kind = (axis == NativeControlCode::LeftTriggerAxis || axis == NativeControlCode::RightTriggerAxis) ?
                PlannedActionKind::NativeAxis1D :
                PlannedActionKind::NativeAxis2D;
            return {
                .backend = PlannedBackend::NativeState,
                .kind = kind,
                .contract = ActionOutputContract::Axis,
                .lifecyclePolicy = ActionLifecyclePolicy::AxisValue,
                .nativeCode = axis,
                .ownsLifecycle = true
            };
        }

        return {
            .backend = PlannedBackend::CompatibilityFallback,
            .kind = PlannedActionKind::NativeButton,
            .contract = ActionOutputContract::Pulse,
            .lifecyclePolicy = ActionLifecyclePolicy::None,
            .nativeCode = NativeControlCode::None,
            .ownsLifecycle = false
        };
    }

    bool ActionBackendPolicy::IsPluginAction(std::string_view actionId)
    {
        return IsPluginActionId(actionId);
    }

    bool ActionBackendPolicy::IsLikelyModAction(std::string_view actionId)
    {
        return IsModEventActionId(actionId) || IsKeyboardHelperActionId(actionId);
    }
}
