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
                actionId == actions::MenuPageUp ||
                actionId == actions::MenuPageDown ||
                actionId == "Dialogue.PreviousOption"sv ||
                actionId == "Dialogue.NextOption"sv ||
                actionId == "Favorites.PreviousItem"sv ||
                actionId == "Favorites.NextItem"sv ||
                actionId == "Console.HistoryUp"sv ||
                actionId == "Console.HistoryDown"sv ||
                actionId == "Book.PreviousPage"sv ||
                actionId == "Book.NextPage"sv;
        }

        constexpr bool IsPluginActionId(std::string_view actionId)
        {
            return actionId == actions::OpenInventory ||
                actionId == actions::OpenMagic ||
                actionId == actions::OpenMap ||
                actionId == actions::OpenJournal ||
                actionId == actions::OpenFavorites ||
                actionId == actions::OpenSkills ||
                actionId == actions::TogglePOV ||
                actionId == actions::ToggleHUD ||
                actionId == actions::Screenshot ||
                actionId == actions::Wait ||
                actionId == actions::QuickSave ||
                actionId == actions::QuickLoad;
        }

        constexpr NativeControlCode TryMapNativeButton(std::string_view actionId)
        {
            if (actionId == actions::Jump) {
                return NativeControlCode::Jump;
            }
            if (actionId == actions::Attack) {
                return NativeControlCode::Attack;
            }
            if (actionId == actions::Block) {
                return NativeControlCode::Block;
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

            if (actionId == actions::MenuConfirm || actionId == "Console.Execute"sv) {
                return NativeControlCode::MenuConfirm;
            }
            if (actionId == actions::MenuCancel || actionId == "Book.Close"sv) {
                return NativeControlCode::MenuCancel;
            }
            if (actionId == actions::MenuScrollUp ||
                actionId == "Dialogue.PreviousOption"sv ||
                actionId == "Favorites.PreviousItem"sv ||
                actionId == "Console.HistoryUp"sv) {
                return NativeControlCode::MenuScrollUp;
            }
            if (actionId == actions::MenuScrollDown ||
                actionId == "Dialogue.NextOption"sv ||
                actionId == "Favorites.NextItem"sv ||
                actionId == "Console.HistoryDown"sv) {
                return NativeControlCode::MenuScrollDown;
            }
            if (actionId == actions::MenuPageUp ||
                actionId == "Book.PreviousPage"sv ||
                actionId == "Menu.SortByName"sv) {
                return NativeControlCode::MenuPageUp;
            }
            if (actionId == actions::MenuPageDown ||
                actionId == "Book.NextPage"sv ||
                actionId == "Menu.SortByValue"sv) {
                return NativeControlCode::MenuPageDown;
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
            if (actionId == actions::Sprint) {
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

        constexpr bool ShouldOwnLifecycle(std::string_view actionId, ActionOutputContract contract)
        {
            switch (contract) {
            case ActionOutputContract::Hold:
            case ActionOutputContract::Toggle:
            case ActionOutputContract::Repeat:
            case ActionOutputContract::Axis:
                return true;
            case ActionOutputContract::Pulse:
                return actionId == actions::Activate;
            case ActionOutputContract::None:
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
                .nativeCode = NativeControlCode::None,
                .ownsLifecycle = false
            };
        }

        if (IsKeyboardHelperActionId(actionId)) {
            return {
                .backend = PlannedBackend::KeyboardNative,
                .kind = PlannedActionKind::KeyboardKey,
                .contract = ActionOutputContract::Pulse,
                .nativeCode = NativeControlCode::None,
                .ownsLifecycle = false
            };
        }

        if (IsModEventActionId(actionId)) {
            return {
                .backend = PlannedBackend::ModEvent,
                .kind = PlannedActionKind::ModEvent,
                .contract = ActionOutputContract::Pulse,
                .nativeCode = NativeControlCode::None,
                .ownsLifecycle = false
            };
        }

        if (const auto nativeButton = TryMapNativeButton(actionId); nativeButton != NativeControlCode::None) {
            const auto contract = ResolveDigitalContract(actionId);
            return {
                .backend = PlannedBackend::CompatibilityFallback,
                .kind = PlannedActionKind::NativeButton,
                .contract = contract,
                .nativeCode = nativeButton,
                .ownsLifecycle = ShouldOwnLifecycle(actionId, contract)
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
                .nativeCode = axis,
                .ownsLifecycle = true
            };
        }

        return {
            .backend = PlannedBackend::CompatibilityFallback,
            .kind = PlannedActionKind::NativeButton,
            .contract = ActionOutputContract::Pulse,
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
