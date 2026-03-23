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
                actionId == actions::FavoritesUp ||
                actionId == actions::FavoritesDown ||
                actionId == actions::ConsolePickPrevious ||
                actionId == actions::ConsolePickNext ||
                actionId == actions::ConsoleHistoryUp ||
                actionId == actions::ConsoleHistoryDown ||
                actionId == actions::DebugOverlayUp ||
                actionId == actions::DebugOverlayDown ||
                actionId == actions::DebugOverlayLeft ||
                actionId == actions::DebugOverlayRight ||
                actionId == actions::CreationsUp ||
                actionId == actions::CreationsDown ||
                actionId == actions::CreationsLeft ||
                actionId == actions::CreationsRight;
        }

        constexpr bool IsPluginActionId(std::string_view actionId)
        {
            return actionId == actions::ToggleHUD ||
                actionId == actions::Screenshot ||
                false;
        }

        constexpr NativeControlCode TryMapButtonEventButton(std::string_view actionId)
        {
            if (actionId == actions::Jump) return NativeControlCode::Jump;
            if (actionId == actions::Activate) return NativeControlCode::Activate;
            if (actionId == actions::ReadyWeapon) return NativeControlCode::ReadyWeapon;
            if (actionId == actions::TweenMenu) return NativeControlCode::TweenMenu;
            if (actionId == actions::Sprint) return NativeControlCode::Sprint;
            if (actionId == actions::Sneak) return NativeControlCode::Sneak;
            if (actionId == actions::Shout) return NativeControlCode::Shout;
            if (actionId == actions::Favorites) return NativeControlCode::FavoritesCombo;
            if (actionId == actions::Hotkey1) return NativeControlCode::Hotkey1;
            if (actionId == actions::Hotkey2) return NativeControlCode::Hotkey2;
            if (actionId == actions::Hotkey3) return NativeControlCode::Hotkey3;
            if (actionId == actions::Hotkey4) return NativeControlCode::Hotkey4;
            if (actionId == actions::Hotkey5) return NativeControlCode::Hotkey5;
            if (actionId == actions::Hotkey6) return NativeControlCode::Hotkey6;
            if (actionId == actions::Hotkey7) return NativeControlCode::Hotkey7;
            if (actionId == actions::Hotkey8) return NativeControlCode::Hotkey8;
            if (actionId == actions::TogglePOV) return NativeControlCode::TogglePOV;
            if (actionId == actions::Wait) return NativeControlCode::Wait;
            if (actionId == actions::OpenJournal) return NativeControlCode::Journal;
            if (actionId == actions::Pause) return NativeControlCode::Pause;
            if (actionId == actions::NativeScreenshot) return NativeControlCode::NativeScreenshot;

            if (actionId == actions::MenuConfirm) return NativeControlCode::MenuConfirm;
            if (actionId == actions::MenuCancel) return NativeControlCode::MenuCancel;
            if (actionId == actions::MenuScrollUp) return NativeControlCode::MenuScrollUp;
            if (actionId == actions::MenuScrollDown) return NativeControlCode::MenuScrollDown;
            if (actionId == actions::MenuLeft) return NativeControlCode::MenuLeft;
            if (actionId == actions::MenuRight) return NativeControlCode::MenuRight;
            if (actionId == actions::MenuDownloadAll) return NativeControlCode::MenuDownloadAll;
            if (actionId == actions::MenuPageUp) return NativeControlCode::MenuPageUp;
            if (actionId == actions::MenuPageDown) return NativeControlCode::MenuPageDown;
            if (actionId == actions::MenuSortByName) return NativeControlCode::MenuSortByName;
            if (actionId == actions::MenuSortByValue) return NativeControlCode::MenuSortByValue;

            if (actionId == actions::DialoguePreviousOption) return NativeControlCode::DialoguePreviousOption;
            if (actionId == actions::DialogueNextOption) return NativeControlCode::DialogueNextOption;

            if (actionId == actions::FavoritesPreviousItem) return NativeControlCode::FavoritesPreviousItem;
            if (actionId == actions::FavoritesNextItem) return NativeControlCode::FavoritesNextItem;
            if (actionId == actions::FavoritesAccept) return NativeControlCode::FavoritesAccept;
            if (actionId == actions::FavoritesCancel) return NativeControlCode::FavoritesCancel;
            if (actionId == actions::FavoritesUp) return NativeControlCode::FavoritesUp;
            if (actionId == actions::FavoritesDown) return NativeControlCode::FavoritesDown;

            if (actionId == actions::ConsoleExecute) return NativeControlCode::ConsoleExecute;
            if (actionId == actions::ConsoleHistoryUp) return NativeControlCode::ConsoleHistoryUp;
            if (actionId == actions::ConsoleHistoryDown) return NativeControlCode::ConsoleHistoryDown;
            if (actionId == actions::ConsolePickPrevious) return NativeControlCode::ConsolePickPrevious;
            if (actionId == actions::ConsolePickNext) return NativeControlCode::ConsolePickNext;
            if (actionId == actions::ConsoleNextFocus) return NativeControlCode::ConsoleNextFocus;
            if (actionId == actions::ConsolePreviousFocus) return NativeControlCode::ConsolePreviousFocus;

            if (actionId == actions::ItemZoom) return NativeControlCode::ItemZoom;
            if (actionId == actions::ItemXButton) return NativeControlCode::ItemXButton;
            if (actionId == actions::ItemYButton) return NativeControlCode::ItemYButton;
            if (actionId == actions::InventoryChargeItem) return NativeControlCode::InventoryChargeItem;

            if (actionId == actions::BookClose) return NativeControlCode::BookClose;
            if (actionId == actions::BookPreviousPage) return NativeControlCode::BookPreviousPage;
            if (actionId == actions::BookNextPage) return NativeControlCode::BookNextPage;

            if (actionId == actions::MapCancel) return NativeControlCode::MapCancel;
            if (actionId == actions::MapClick) return NativeControlCode::MapClick;
            if (actionId == actions::MapOpenJournal) return NativeControlCode::MapOpenJournal;
            if (actionId == actions::MapPlayerPosition) return NativeControlCode::MapPlayerPosition;
            if (actionId == actions::MapLocalMap) return NativeControlCode::MapLocalMap;

            if (actionId == actions::CursorClick) return NativeControlCode::CursorClick;
            if (actionId == actions::JournalXButton) return NativeControlCode::JournalXButton;
            if (actionId == actions::JournalYButton) return NativeControlCode::JournalYButton;

            if (actionId == actions::DebugOverlayNextFocus) return NativeControlCode::DebugOverlayNextFocus;
            if (actionId == actions::DebugOverlayPreviousFocus) return NativeControlCode::DebugOverlayPreviousFocus;
            if (actionId == actions::DebugOverlayUp) return NativeControlCode::DebugOverlayUp;
            if (actionId == actions::DebugOverlayDown) return NativeControlCode::DebugOverlayDown;
            if (actionId == actions::DebugOverlayLeft) return NativeControlCode::DebugOverlayLeft;
            if (actionId == actions::DebugOverlayRight) return NativeControlCode::DebugOverlayRight;
            if (actionId == actions::DebugOverlayToggleMinimize) return NativeControlCode::DebugOverlayToggleMinimize;
            if (actionId == actions::DebugOverlayToggleMove) return NativeControlCode::DebugOverlayToggleMove;
            if (actionId == actions::DebugOverlayB) return NativeControlCode::DebugOverlayB;
            if (actionId == actions::DebugOverlayY) return NativeControlCode::DebugOverlayY;
            if (actionId == actions::DebugOverlayX) return NativeControlCode::DebugOverlayX;

            if (actionId == actions::TFCWorldZUp) return NativeControlCode::TFCWorldZUp;
            if (actionId == actions::TFCWorldZDown) return NativeControlCode::TFCWorldZDown;
            if (actionId == actions::TFCLockToZPlane) return NativeControlCode::TFCLockToZPlane;

            if (actionId == actions::LockpickingDebugMode) return NativeControlCode::LockpickingDebugMode;
            if (actionId == actions::LockpickingCancel) return NativeControlCode::LockpickingCancel;

            if (actionId == actions::CreationsAccept) return NativeControlCode::CreationsAccept;
            if (actionId == actions::CreationsCancel) return NativeControlCode::CreationsCancel;
            if (actionId == actions::CreationsUp) return NativeControlCode::CreationsUp;
            if (actionId == actions::CreationsDown) return NativeControlCode::CreationsDown;
            if (actionId == actions::CreationsLeft) return NativeControlCode::CreationsLeft;
            if (actionId == actions::CreationsRight) return NativeControlCode::CreationsRight;
            if (actionId == actions::CreationsOptions) return NativeControlCode::CreationsOptions;
            if (actionId == actions::CreationsLoadOrderAndDelete) return NativeControlCode::CreationsLoadOrderAndDelete;
            if (actionId == actions::CreationsLikeUnlike) return NativeControlCode::CreationsLikeUnlike;
            if (actionId == actions::CreationsSearchEdit) return NativeControlCode::CreationsSearchEdit;
            if (actionId == actions::CreationsPurchaseCredits) return NativeControlCode::CreationsPurchaseCredits;

            if (actionId == actions::FavorCancel) return NativeControlCode::FavorCancel;
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
            if (actionId == "Game.Move"sv) return NativeControlCode::MoveStick;
            if (actionId == "Game.Look"sv) return NativeControlCode::LookStick;
            if (actionId == "Menu.LeftStick"sv) return NativeControlCode::MenuStick;
            if (actionId == actions::FavoritesLeftStick) return NativeControlCode::FavoritesLeftStick;
            if (actionId == actions::ItemLeftEquip) return NativeControlCode::ItemLeftEquipTrigger;
            if (actionId == actions::ItemRightEquip) return NativeControlCode::ItemRightEquipTrigger;
            if (actionId == actions::ItemRotate) return NativeControlCode::ItemRotateStick;
            if (actionId == actions::MapLook) return NativeControlCode::MapLookStick;
            if (actionId == actions::MapZoomOut) return NativeControlCode::MapZoomOutTrigger;
            if (actionId == actions::MapZoomIn) return NativeControlCode::MapZoomInTrigger;
            if (actionId == actions::MapCursor) return NativeControlCode::MapCursorStick;
            if (actionId == actions::StatsRotate) return NativeControlCode::StatsRotateStick;
            if (actionId == actions::CursorMove) return NativeControlCode::CursorMoveStick;
            if (actionId == actions::JournalTabLeft) return NativeControlCode::JournalTabLeftTrigger;
            if (actionId == actions::JournalTabRight) return NativeControlCode::JournalTabRightTrigger;
            if (actionId == actions::DebugOverlayLeftTrigger) return NativeControlCode::DebugOverlayLeftTrigger;
            if (actionId == actions::DebugOverlayRightTrigger) return NativeControlCode::DebugOverlayRightTrigger;
            if (actionId == actions::TFCCameraZDown) return NativeControlCode::TFCCameraZDownTrigger;
            if (actionId == actions::TFCCameraZUp) return NativeControlCode::TFCCameraZUpTrigger;
            if (actionId == actions::DebugMapLook) return NativeControlCode::DebugMapLookStick;
            if (actionId == actions::DebugMapZoomOut) return NativeControlCode::DebugMapZoomOutTrigger;
            if (actionId == actions::DebugMapZoomIn) return NativeControlCode::DebugMapZoomInTrigger;
            if (actionId == actions::DebugMapMove) return NativeControlCode::DebugMapMoveStick;
            if (actionId == actions::LockpickingRotatePick) return NativeControlCode::LockpickingRotatePickStick;
            if (actionId == actions::LockpickingRotateLock) return NativeControlCode::LockpickingRotateLockStick;
            if (actionId == actions::CreationsLeftStick) return NativeControlCode::CreationsLeftStick;
            if (actionId == actions::CreationsCategorySideBar) return NativeControlCode::CreationsCategorySideBarTrigger;
            if (actionId == actions::CreationsFilter) return NativeControlCode::CreationsFilterTrigger;
            if (actionId == "Game.LeftTrigger"sv) return NativeControlCode::LeftTriggerAxis;
            if (actionId == "Game.RightTrigger"sv) return NativeControlCode::RightTriggerAxis;

            return NativeControlCode::None;
        }

        constexpr bool IsTriggerAxisCode(NativeControlCode code)
        {
            switch (code) {
            case NativeControlCode::ItemLeftEquipTrigger:
            case NativeControlCode::ItemRightEquipTrigger:
            case NativeControlCode::MapZoomOutTrigger:
            case NativeControlCode::MapZoomInTrigger:
            case NativeControlCode::JournalTabLeftTrigger:
            case NativeControlCode::JournalTabRightTrigger:
            case NativeControlCode::DebugOverlayLeftTrigger:
            case NativeControlCode::DebugOverlayRightTrigger:
            case NativeControlCode::TFCCameraZDownTrigger:
            case NativeControlCode::TFCCameraZUpTrigger:
            case NativeControlCode::DebugMapZoomOutTrigger:
            case NativeControlCode::DebugMapZoomInTrigger:
            case NativeControlCode::CreationsCategorySideBarTrigger:
            case NativeControlCode::CreationsFilterTrigger:
            case NativeControlCode::LeftTriggerAxis:
            case NativeControlCode::RightTriggerAxis:
                return true;
            default:
                return false;
            }
        }

        constexpr ActionOutputContract ResolveDigitalContract(std::string_view actionId)
        {
            if (actionId == actions::Sprint ||
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
                .backend = PlannedBackend::KeyboardHelper,
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
                .backend = PlannedBackend::NativeButtonCommit,
                .kind = PlannedActionKind::NativeButton,
                .contract = contract,
                .lifecyclePolicy = lifecyclePolicy,
                .nativeCode = nativeButton,
                .ownsLifecycle = ShouldOwnLifecycle(lifecyclePolicy)
            };
        }

        if (const auto axis = TryMapNativeAxis(actionId); axis != NativeControlCode::None) {
            const auto kind = IsTriggerAxisCode(axis) ?
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
            .backend = PlannedBackend::None,
            .kind = PlannedActionKind::PluginAction,
            .contract = ActionOutputContract::None,
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
