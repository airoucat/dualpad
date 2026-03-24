#include "pch.h"
#include "input/backend/NativeActionDescriptor.h"

#include "input/Action.h"

using namespace std::literals;

namespace dualpad::input::backend
{
    namespace
    {
        constexpr std::uint16_t operator|(VirtualPadButtonRoleMask lhs, VirtualPadButtonRoleMask rhs)
        {
            return static_cast<std::uint16_t>(lhs) | static_cast<std::uint16_t>(rhs);
        }

        constexpr NativeActionDescriptor kNativeActionDescriptors[] = {
            { actions::Jump, NativeControlCode::Jump, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleTriangle },
            { actions::Activate, NativeControlCode::Activate, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::MinDownWindowPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleCross },
            { actions::ReadyWeapon, NativeControlCode::ReadyWeapon, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleSquare },
            { actions::TweenMenu, NativeControlCode::TweenMenu, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleCircle },
            { actions::Sprint, NativeControlCode::Sprint, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Hold, ActionLifecyclePolicy::HoldOwner, true, NativeAxisTarget::None, VirtualPadButtonRoleL1 },
            { actions::Sneak, NativeControlCode::Sneak, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Toggle, ActionLifecyclePolicy::ToggleOwner, true, NativeAxisTarget::None, VirtualPadButtonRoleL3 },
            { actions::Shout, NativeControlCode::Shout, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Hold, ActionLifecyclePolicy::HoldOwner, true, NativeAxisTarget::None, VirtualPadButtonRoleR1 },
            { actions::Favorites, NativeControlCode::FavoritesCombo, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleDpadUp },
            { actions::Hotkey1, NativeControlCode::Hotkey1, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleDpadLeft },
            { actions::Hotkey2, NativeControlCode::Hotkey2, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleDpadRight },
            { actions::Hotkey3, NativeControlCode::Hotkey3, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleL1 | VirtualPadButtonRoleCreate },
            { actions::Hotkey4, NativeControlCode::Hotkey4, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleL1 | VirtualPadButtonRoleDpadUp },
            { actions::Hotkey5, NativeControlCode::Hotkey5, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleL1 | VirtualPadButtonRoleDpadLeft },
            { actions::Hotkey6, NativeControlCode::Hotkey6, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleL1 | VirtualPadButtonRoleDpadDown },
            { actions::Hotkey7, NativeControlCode::Hotkey7, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleL1 | VirtualPadButtonRoleDpadRight },
            { actions::Hotkey8, NativeControlCode::Hotkey8, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleL1 | VirtualPadButtonRoleR1 },
            { actions::TogglePOV, NativeControlCode::TogglePOV, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleR3 },
            { actions::Wait, NativeControlCode::Wait, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleCreate },
            { actions::OpenJournal, NativeControlCode::Journal, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleOptions },
            { actions::Pause, NativeControlCode::Pause, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleCircle | VirtualPadButtonRoleTriangle },
            { actions::NativeScreenshot, NativeControlCode::NativeScreenshot, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleCircle | VirtualPadButtonRoleR1 },

            { actions::MenuConfirm, NativeControlCode::MenuConfirm, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleCross },
            { actions::MenuCancel, NativeControlCode::MenuCancel, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleCircle },
            { actions::MenuScrollUp, NativeControlCode::MenuScrollUp, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Repeat, ActionLifecyclePolicy::RepeatOwner, true, NativeAxisTarget::None, VirtualPadButtonRoleDpadUp },
            { actions::MenuScrollDown, NativeControlCode::MenuScrollDown, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Repeat, ActionLifecyclePolicy::RepeatOwner, true, NativeAxisTarget::None, VirtualPadButtonRoleDpadDown },
            { actions::MenuLeft, NativeControlCode::MenuLeft, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Repeat, ActionLifecyclePolicy::RepeatOwner, true, NativeAxisTarget::None, VirtualPadButtonRoleDpadLeft },
            { actions::MenuRight, NativeControlCode::MenuRight, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Repeat, ActionLifecyclePolicy::RepeatOwner, true, NativeAxisTarget::None, VirtualPadButtonRoleDpadRight },
            { actions::MenuDownloadAll, NativeControlCode::MenuDownloadAll, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleTriangle },
            { actions::MenuPageUp, NativeControlCode::MenuPageUp, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleL1 },
            { actions::MenuPageDown, NativeControlCode::MenuPageDown, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleR1 },
            { actions::MenuSortByName, NativeControlCode::MenuSortByName, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleL1 },
            { actions::MenuSortByValue, NativeControlCode::MenuSortByValue, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleR1 },

            { actions::DialoguePreviousOption, NativeControlCode::DialoguePreviousOption, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Repeat, ActionLifecyclePolicy::RepeatOwner, true, NativeAxisTarget::None, VirtualPadButtonRoleDpadUp },
            { actions::DialogueNextOption, NativeControlCode::DialogueNextOption, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Repeat, ActionLifecyclePolicy::RepeatOwner, true, NativeAxisTarget::None, VirtualPadButtonRoleDpadDown },

            { actions::FavoritesPreviousItem, NativeControlCode::FavoritesPreviousItem, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Repeat, ActionLifecyclePolicy::RepeatOwner, true, NativeAxisTarget::None, VirtualPadButtonRoleDpadUp },
            { actions::FavoritesNextItem, NativeControlCode::FavoritesNextItem, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Repeat, ActionLifecyclePolicy::RepeatOwner, true, NativeAxisTarget::None, VirtualPadButtonRoleDpadDown },
            { actions::FavoritesAccept, NativeControlCode::FavoritesAccept, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleCross },
            { actions::FavoritesCancel, NativeControlCode::FavoritesCancel, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleCircle },
            { actions::FavoritesUp, NativeControlCode::FavoritesUp, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Repeat, ActionLifecyclePolicy::RepeatOwner, true, NativeAxisTarget::None, VirtualPadButtonRoleDpadUp },
            { actions::FavoritesDown, NativeControlCode::FavoritesDown, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Repeat, ActionLifecyclePolicy::RepeatOwner, true, NativeAxisTarget::None, VirtualPadButtonRoleDpadDown },

            { actions::ConsoleExecute, NativeControlCode::ConsoleExecute, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleCross },
            { actions::ConsoleHistoryUp, NativeControlCode::ConsoleHistoryUp, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Repeat, ActionLifecyclePolicy::RepeatOwner, true, NativeAxisTarget::None, VirtualPadButtonRoleDpadUp },
            { actions::ConsoleHistoryDown, NativeControlCode::ConsoleHistoryDown, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Repeat, ActionLifecyclePolicy::RepeatOwner, true, NativeAxisTarget::None, VirtualPadButtonRoleDpadDown },
            { actions::ConsolePickPrevious, NativeControlCode::ConsolePickPrevious, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Repeat, ActionLifecyclePolicy::RepeatOwner, true, NativeAxisTarget::None, VirtualPadButtonRoleDpadDown },
            { actions::ConsolePickNext, NativeControlCode::ConsolePickNext, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Repeat, ActionLifecyclePolicy::RepeatOwner, true, NativeAxisTarget::None, VirtualPadButtonRoleDpadUp },
            { actions::ConsoleNextFocus, NativeControlCode::ConsoleNextFocus, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleR1 },
            { actions::ConsolePreviousFocus, NativeControlCode::ConsolePreviousFocus, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleL1 },

            { actions::ItemZoom, NativeControlCode::ItemZoom, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleR3 },
            { actions::ItemXButton, NativeControlCode::ItemXButton, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleSquare },
            { actions::ItemYButton, NativeControlCode::ItemYButton, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleTriangle },
            { actions::InventoryChargeItem, NativeControlCode::InventoryChargeItem, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleR1 },

            { actions::BookClose, NativeControlCode::BookClose, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleCircle },
            { actions::BookPreviousPage, NativeControlCode::BookPreviousPage, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleDpadLeft },
            { actions::BookNextPage, NativeControlCode::BookNextPage, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleDpadRight },

            { actions::MapCancel, NativeControlCode::MapCancel, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleCircle },
            { actions::MapClick, NativeControlCode::MapClick, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleCross },
            { actions::MapOpenJournal, NativeControlCode::MapOpenJournal, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleDpadLeft },
            { actions::MapPlayerPosition, NativeControlCode::MapPlayerPosition, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleTriangle },
            { actions::MapLocalMap, NativeControlCode::MapLocalMap, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleSquare },

            { actions::CursorClick, NativeControlCode::CursorClick, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleCross },
            { actions::JournalXButton, NativeControlCode::JournalXButton, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleDpadLeft },
            { actions::JournalYButton, NativeControlCode::JournalYButton, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleTriangle },

            { actions::DebugOverlayNextFocus, NativeControlCode::DebugOverlayNextFocus, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleR1 },
            { actions::DebugOverlayPreviousFocus, NativeControlCode::DebugOverlayPreviousFocus, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleL1 },
            { actions::DebugOverlayUp, NativeControlCode::DebugOverlayUp, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Repeat, ActionLifecyclePolicy::RepeatOwner, true, NativeAxisTarget::None, VirtualPadButtonRoleDpadUp },
            { actions::DebugOverlayDown, NativeControlCode::DebugOverlayDown, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Repeat, ActionLifecyclePolicy::RepeatOwner, true, NativeAxisTarget::None, VirtualPadButtonRoleDpadDown },
            { actions::DebugOverlayLeft, NativeControlCode::DebugOverlayLeft, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Repeat, ActionLifecyclePolicy::RepeatOwner, true, NativeAxisTarget::None, VirtualPadButtonRoleDpadLeft },
            { actions::DebugOverlayRight, NativeControlCode::DebugOverlayRight, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Repeat, ActionLifecyclePolicy::RepeatOwner, true, NativeAxisTarget::None, VirtualPadButtonRoleDpadRight },
            { actions::DebugOverlayToggleMinimize, NativeControlCode::DebugOverlayToggleMinimize, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleCreate },
            { actions::DebugOverlayToggleMove, NativeControlCode::DebugOverlayToggleMove, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleR3 },
            { actions::DebugOverlayB, NativeControlCode::DebugOverlayB, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleCircle },
            { actions::DebugOverlayY, NativeControlCode::DebugOverlayY, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleTriangle },
            { actions::DebugOverlayX, NativeControlCode::DebugOverlayX, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleSquare },
            { actions::TFCWorldZUp, NativeControlCode::TFCWorldZUp, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleR1 },
            { actions::TFCWorldZDown, NativeControlCode::TFCWorldZDown, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleL1 },
            { actions::TFCLockToZPlane, NativeControlCode::TFCLockToZPlane, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleSquare },
            { actions::LockpickingDebugMode, NativeControlCode::LockpickingDebugMode, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleSquare },
            { actions::LockpickingCancel, NativeControlCode::LockpickingCancel, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleCircle },
            { actions::CreationsAccept, NativeControlCode::CreationsAccept, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleCross },
            { actions::CreationsCancel, NativeControlCode::CreationsCancel, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleCircle },
            { actions::CreationsUp, NativeControlCode::CreationsUp, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Repeat, ActionLifecyclePolicy::RepeatOwner, true, NativeAxisTarget::None, VirtualPadButtonRoleDpadUp },
            { actions::CreationsDown, NativeControlCode::CreationsDown, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Repeat, ActionLifecyclePolicy::RepeatOwner, true, NativeAxisTarget::None, VirtualPadButtonRoleDpadDown },
            { actions::CreationsLeft, NativeControlCode::CreationsLeft, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Repeat, ActionLifecyclePolicy::RepeatOwner, true, NativeAxisTarget::None, VirtualPadButtonRoleDpadLeft },
            { actions::CreationsRight, NativeControlCode::CreationsRight, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Repeat, ActionLifecyclePolicy::RepeatOwner, true, NativeAxisTarget::None, VirtualPadButtonRoleDpadRight },
            { actions::CreationsOptions, NativeControlCode::CreationsOptions, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleOptions },
            { actions::CreationsLoadOrderAndDelete, NativeControlCode::CreationsLoadOrderAndDelete, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleTriangle },
            { actions::CreationsLikeUnlike, NativeControlCode::CreationsLikeUnlike, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleR1 },
            { actions::CreationsSearchEdit, NativeControlCode::CreationsSearchEdit, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleL1 },
            { actions::CreationsPurchaseCredits, NativeControlCode::CreationsPurchaseCredits, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleSquare },
            { actions::FavorCancel, NativeControlCode::FavorCancel, PlannedBackend::NativeButtonCommit, PlannedActionKind::NativeButton, ActionOutputContract::Pulse, ActionLifecyclePolicy::DeferredPulse, false, NativeAxisTarget::None, VirtualPadButtonRoleCircle },

            { "Game.Move"sv, NativeControlCode::MoveStick, PlannedBackend::NativeState, PlannedActionKind::NativeAxis2D, ActionOutputContract::Axis, ActionLifecyclePolicy::AxisValue, true, NativeAxisTarget::MoveStick, VirtualPadButtonRoleNone },
            { "Game.Look"sv, NativeControlCode::LookStick, PlannedBackend::NativeState, PlannedActionKind::NativeAxis2D, ActionOutputContract::Axis, ActionLifecyclePolicy::AxisValue, true, NativeAxisTarget::LookStick, VirtualPadButtonRoleNone },
            { "Menu.LeftStick"sv, NativeControlCode::MenuStick, PlannedBackend::NativeState, PlannedActionKind::NativeAxis2D, ActionOutputContract::Axis, ActionLifecyclePolicy::AxisValue, true, NativeAxisTarget::MoveStick, VirtualPadButtonRoleNone },
            { actions::FavoritesLeftStick, NativeControlCode::FavoritesLeftStick, PlannedBackend::NativeState, PlannedActionKind::NativeAxis2D, ActionOutputContract::Axis, ActionLifecyclePolicy::AxisValue, true, NativeAxisTarget::MoveStick, VirtualPadButtonRoleNone },
            { actions::ItemLeftEquip, NativeControlCode::ItemLeftEquipTrigger, PlannedBackend::NativeState, PlannedActionKind::NativeAxis1D, ActionOutputContract::Axis, ActionLifecyclePolicy::AxisValue, true, NativeAxisTarget::LeftTrigger, VirtualPadButtonRoleNone },
            { actions::ItemRightEquip, NativeControlCode::ItemRightEquipTrigger, PlannedBackend::NativeState, PlannedActionKind::NativeAxis1D, ActionOutputContract::Axis, ActionLifecyclePolicy::AxisValue, true, NativeAxisTarget::RightTrigger, VirtualPadButtonRoleNone },
            { actions::ItemRotate, NativeControlCode::ItemRotateStick, PlannedBackend::NativeState, PlannedActionKind::NativeAxis2D, ActionOutputContract::Axis, ActionLifecyclePolicy::AxisValue, true, NativeAxisTarget::LookStick, VirtualPadButtonRoleNone },
            { actions::MapLook, NativeControlCode::MapLookStick, PlannedBackend::NativeState, PlannedActionKind::NativeAxis2D, ActionOutputContract::Axis, ActionLifecyclePolicy::AxisValue, true, NativeAxisTarget::LookStick, VirtualPadButtonRoleNone },
            { actions::MapZoomOut, NativeControlCode::MapZoomOutTrigger, PlannedBackend::NativeState, PlannedActionKind::NativeAxis1D, ActionOutputContract::Axis, ActionLifecyclePolicy::AxisValue, true, NativeAxisTarget::LeftTrigger, VirtualPadButtonRoleNone },
            { actions::MapZoomIn, NativeControlCode::MapZoomInTrigger, PlannedBackend::NativeState, PlannedActionKind::NativeAxis1D, ActionOutputContract::Axis, ActionLifecyclePolicy::AxisValue, true, NativeAxisTarget::RightTrigger, VirtualPadButtonRoleNone },
            { actions::MapCursor, NativeControlCode::MapCursorStick, PlannedBackend::NativeState, PlannedActionKind::NativeAxis2D, ActionOutputContract::Axis, ActionLifecyclePolicy::AxisValue, true, NativeAxisTarget::MoveStick, VirtualPadButtonRoleNone },
            { actions::StatsRotate, NativeControlCode::StatsRotateStick, PlannedBackend::NativeState, PlannedActionKind::NativeAxis2D, ActionOutputContract::Axis, ActionLifecyclePolicy::AxisValue, true, NativeAxisTarget::MoveStick, VirtualPadButtonRoleNone },
            { actions::CursorMove, NativeControlCode::CursorMoveStick, PlannedBackend::NativeState, PlannedActionKind::NativeAxis2D, ActionOutputContract::Axis, ActionLifecyclePolicy::AxisValue, true, NativeAxisTarget::LookStick, VirtualPadButtonRoleNone },
            { actions::JournalTabLeft, NativeControlCode::JournalTabLeftTrigger, PlannedBackend::NativeState, PlannedActionKind::NativeAxis1D, ActionOutputContract::Axis, ActionLifecyclePolicy::AxisValue, true, NativeAxisTarget::LeftTrigger, VirtualPadButtonRoleNone },
            { actions::JournalTabRight, NativeControlCode::JournalTabRightTrigger, PlannedBackend::NativeState, PlannedActionKind::NativeAxis1D, ActionOutputContract::Axis, ActionLifecyclePolicy::AxisValue, true, NativeAxisTarget::RightTrigger, VirtualPadButtonRoleNone },
            { actions::DebugOverlayLeftTrigger, NativeControlCode::DebugOverlayLeftTrigger, PlannedBackend::NativeState, PlannedActionKind::NativeAxis1D, ActionOutputContract::Axis, ActionLifecyclePolicy::AxisValue, true, NativeAxisTarget::LeftTrigger, VirtualPadButtonRoleNone },
            { actions::DebugOverlayRightTrigger, NativeControlCode::DebugOverlayRightTrigger, PlannedBackend::NativeState, PlannedActionKind::NativeAxis1D, ActionOutputContract::Axis, ActionLifecyclePolicy::AxisValue, true, NativeAxisTarget::RightTrigger, VirtualPadButtonRoleNone },
            { actions::TFCCameraZDown, NativeControlCode::TFCCameraZDownTrigger, PlannedBackend::NativeState, PlannedActionKind::NativeAxis1D, ActionOutputContract::Axis, ActionLifecyclePolicy::AxisValue, true, NativeAxisTarget::LeftTrigger, VirtualPadButtonRoleNone },
            { actions::TFCCameraZUp, NativeControlCode::TFCCameraZUpTrigger, PlannedBackend::NativeState, PlannedActionKind::NativeAxis1D, ActionOutputContract::Axis, ActionLifecyclePolicy::AxisValue, true, NativeAxisTarget::RightTrigger, VirtualPadButtonRoleNone },
            { actions::DebugMapLook, NativeControlCode::DebugMapLookStick, PlannedBackend::NativeState, PlannedActionKind::NativeAxis2D, ActionOutputContract::Axis, ActionLifecyclePolicy::AxisValue, true, NativeAxisTarget::LookStick, VirtualPadButtonRoleNone },
            { actions::DebugMapZoomOut, NativeControlCode::DebugMapZoomOutTrigger, PlannedBackend::NativeState, PlannedActionKind::NativeAxis1D, ActionOutputContract::Axis, ActionLifecyclePolicy::AxisValue, true, NativeAxisTarget::LeftTrigger, VirtualPadButtonRoleNone },
            { actions::DebugMapZoomIn, NativeControlCode::DebugMapZoomInTrigger, PlannedBackend::NativeState, PlannedActionKind::NativeAxis1D, ActionOutputContract::Axis, ActionLifecyclePolicy::AxisValue, true, NativeAxisTarget::RightTrigger, VirtualPadButtonRoleNone },
            { actions::DebugMapMove, NativeControlCode::DebugMapMoveStick, PlannedBackend::NativeState, PlannedActionKind::NativeAxis2D, ActionOutputContract::Axis, ActionLifecyclePolicy::AxisValue, true, NativeAxisTarget::MoveStick, VirtualPadButtonRoleNone },
            { actions::LockpickingRotatePick, NativeControlCode::LockpickingRotatePickStick, PlannedBackend::NativeState, PlannedActionKind::NativeAxis2D, ActionOutputContract::Axis, ActionLifecyclePolicy::AxisValue, true, NativeAxisTarget::MoveStick, VirtualPadButtonRoleNone },
            { actions::LockpickingRotateLock, NativeControlCode::LockpickingRotateLockStick, PlannedBackend::NativeState, PlannedActionKind::NativeAxis2D, ActionOutputContract::Axis, ActionLifecyclePolicy::AxisValue, true, NativeAxisTarget::LookStick, VirtualPadButtonRoleNone },
            { actions::CreationsLeftStick, NativeControlCode::CreationsLeftStick, PlannedBackend::NativeState, PlannedActionKind::NativeAxis2D, ActionOutputContract::Axis, ActionLifecyclePolicy::AxisValue, true, NativeAxisTarget::MoveStick, VirtualPadButtonRoleNone },
            { actions::CreationsCategorySideBar, NativeControlCode::CreationsCategorySideBarTrigger, PlannedBackend::NativeState, PlannedActionKind::NativeAxis1D, ActionOutputContract::Axis, ActionLifecyclePolicy::AxisValue, true, NativeAxisTarget::LeftTrigger, VirtualPadButtonRoleNone },
            { actions::CreationsFilter, NativeControlCode::CreationsFilterTrigger, PlannedBackend::NativeState, PlannedActionKind::NativeAxis1D, ActionOutputContract::Axis, ActionLifecyclePolicy::AxisValue, true, NativeAxisTarget::RightTrigger, VirtualPadButtonRoleNone },
            { "Game.LeftTrigger"sv, NativeControlCode::LeftTriggerAxis, PlannedBackend::NativeState, PlannedActionKind::NativeAxis1D, ActionOutputContract::Axis, ActionLifecyclePolicy::AxisValue, true, NativeAxisTarget::LeftTrigger, VirtualPadButtonRoleNone },
            { "Game.RightTrigger"sv, NativeControlCode::RightTriggerAxis, PlannedBackend::NativeState, PlannedActionKind::NativeAxis1D, ActionOutputContract::Axis, ActionLifecyclePolicy::AxisValue, true, NativeAxisTarget::RightTrigger, VirtualPadButtonRoleNone },
        };
    }

    const NativeActionDescriptor* FindNativeActionDescriptor(std::string_view actionId)
    {
        for (const auto& descriptor : kNativeActionDescriptors) {
            if (descriptor.actionId == actionId) {
                return &descriptor;
            }
        }

        return nullptr;
    }

    const NativeActionDescriptor* FindNativeActionDescriptor(NativeControlCode nativeCode)
    {
        for (const auto& descriptor : kNativeActionDescriptors) {
            if (descriptor.nativeCode == nativeCode) {
                return &descriptor;
            }
        }

        return nullptr;
    }

    std::string_view ToString(NativeControlCode code)
    {
        if (code == NativeControlCode::None) {
            return "None";
        }

        if (const auto* descriptor = FindNativeActionDescriptor(code)) {
            return descriptor->actionId;
        }

        return "UnknownNativeControl";
    }

    std::uint32_t ResolveVirtualPadBitMask(std::uint16_t virtualButtonRoles, const PadBits& bits)
    {
        std::uint32_t mask = 0;
        if ((virtualButtonRoles & VirtualPadButtonRoleCross) != 0) mask |= bits.cross;
        if ((virtualButtonRoles & VirtualPadButtonRoleCircle) != 0) mask |= bits.circle;
        if ((virtualButtonRoles & VirtualPadButtonRoleSquare) != 0) mask |= bits.square;
        if ((virtualButtonRoles & VirtualPadButtonRoleTriangle) != 0) mask |= bits.triangle;
        if ((virtualButtonRoles & VirtualPadButtonRoleL1) != 0) mask |= bits.l1;
        if ((virtualButtonRoles & VirtualPadButtonRoleR1) != 0) mask |= bits.r1;
        if ((virtualButtonRoles & VirtualPadButtonRoleL3) != 0) mask |= bits.l3;
        if ((virtualButtonRoles & VirtualPadButtonRoleR3) != 0) mask |= bits.r3;
        if ((virtualButtonRoles & VirtualPadButtonRoleCreate) != 0) mask |= bits.create;
        if ((virtualButtonRoles & VirtualPadButtonRoleOptions) != 0) mask |= bits.options;
        if ((virtualButtonRoles & VirtualPadButtonRoleDpadUp) != 0) mask |= bits.dpadUp;
        if ((virtualButtonRoles & VirtualPadButtonRoleDpadDown) != 0) mask |= bits.dpadDown;
        if ((virtualButtonRoles & VirtualPadButtonRoleDpadLeft) != 0) mask |= bits.dpadLeft;
        if ((virtualButtonRoles & VirtualPadButtonRoleDpadRight) != 0) mask |= bits.dpadRight;
        return mask;
    }

    std::uint32_t ResolveVirtualPadBitMask(NativeControlCode nativeCode, const PadBits& bits)
    {
        if (const auto* descriptor = FindNativeActionDescriptor(nativeCode)) {
            return ResolveVirtualPadBitMask(descriptor->virtualButtonRoles, bits);
        }

        return 0;
    }
}
