#pragma once

#include <cstdint>
#include <string_view>

namespace dualpad::input::backend
{
    enum class NativeControlCode : std::uint32_t
    {
        None = 0,
        Jump,
        Activate,
        ReadyWeapon,
        TweenMenu,
        Sprint,
        Sneak,
        Shout,
        FavoritesCombo,
        Hotkey1,
        Hotkey2,
        Hotkey3,
        Hotkey4,
        Hotkey5,
        Hotkey6,
        Hotkey7,
        Hotkey8,
        TogglePOV,

        Wait,
        Journal,
        Pause,
        NativeScreenshot,

        MenuConfirm,
        MenuCancel,
        MenuScrollUp,
        MenuScrollDown,
        MenuLeft,
        MenuRight,
        MenuDownloadAll,
        MenuPageUp,
        MenuPageDown,
        MenuSortByName,
        MenuSortByValue,

        DialoguePreviousOption,
        DialogueNextOption,

        FavoritesPreviousItem,
        FavoritesNextItem,
        FavoritesAccept,
        FavoritesCancel,
        FavoritesUp,
        FavoritesDown,

        ConsoleExecute,
        ConsoleHistoryUp,
        ConsoleHistoryDown,
        ConsolePickPrevious,
        ConsolePickNext,
        ConsoleNextFocus,
        ConsolePreviousFocus,

        ItemZoom,
        ItemXButton,
        ItemYButton,
        InventoryChargeItem,

        BookClose,
        BookPreviousPage,
        BookNextPage,

        MapCancel,
        MapClick,
        MapOpenJournal,
        MapPlayerPosition,
        MapLocalMap,

        CursorClick,
        JournalXButton,
        JournalYButton,

        DebugOverlayNextFocus,
        DebugOverlayPreviousFocus,
        DebugOverlayUp,
        DebugOverlayDown,
        DebugOverlayLeft,
        DebugOverlayRight,
        DebugOverlayToggleMinimize,
        DebugOverlayToggleMove,
        DebugOverlayB,
        DebugOverlayY,
        DebugOverlayX,
        TFCWorldZUp,
        TFCWorldZDown,
        TFCLockToZPlane,
        LockpickingDebugMode,
        LockpickingCancel,
        CreationsAccept,
        CreationsCancel,
        CreationsUp,
        CreationsDown,
        CreationsLeft,
        CreationsRight,
        CreationsOptions,
        CreationsLoadOrderAndDelete,
        CreationsLikeUnlike,
        CreationsSearchEdit,
        CreationsPurchaseCredits,
        FavorCancel,

        MoveStick,
        LookStick,
        MenuStick,
        FavoritesLeftStick,
        ItemLeftEquipTrigger,
        ItemRightEquipTrigger,
        ItemRotateStick,
        MapLookStick,
        MapZoomOutTrigger,
        MapZoomInTrigger,
        MapCursorStick,
        StatsRotateStick,
        CursorMoveStick,
        JournalTabLeftTrigger,
        JournalTabRightTrigger,
        DebugOverlayLeftTrigger,
        DebugOverlayRightTrigger,
        TFCCameraZDownTrigger,
        TFCCameraZUpTrigger,
        DebugMapLookStick,
        DebugMapZoomOutTrigger,
        DebugMapZoomInTrigger,
        DebugMapMoveStick,
        LockpickingRotatePickStick,
        LockpickingRotateLockStick,
        CreationsLeftStick,
        CreationsCategorySideBarTrigger,
        CreationsFilterTrigger,
        LeftTriggerAxis,
        RightTriggerAxis
    };

    inline constexpr std::string_view ToString(NativeControlCode code)
    {
        switch (code) {
        case NativeControlCode::Jump:
            return "Jump";
        case NativeControlCode::Activate:
            return "Activate";
        case NativeControlCode::ReadyWeapon:
            return "ReadyWeapon";
        case NativeControlCode::TweenMenu:
            return "TweenMenu";
        case NativeControlCode::Sprint:
            return "Sprint";
        case NativeControlCode::Sneak:
            return "Sneak";
        case NativeControlCode::Shout:
            return "Shout";
        case NativeControlCode::FavoritesCombo:
            return "FavoritesCombo";
        case NativeControlCode::Hotkey1:
            return "Hotkey1";
        case NativeControlCode::Hotkey2:
            return "Hotkey2";
        case NativeControlCode::Hotkey3:
            return "Hotkey3";
        case NativeControlCode::Hotkey4:
            return "Hotkey4";
        case NativeControlCode::Hotkey5:
            return "Hotkey5";
        case NativeControlCode::Hotkey6:
            return "Hotkey6";
        case NativeControlCode::Hotkey7:
            return "Hotkey7";
        case NativeControlCode::Hotkey8:
            return "Hotkey8";
        case NativeControlCode::TogglePOV:
            return "TogglePOV";
        case NativeControlCode::Pause:
            return "Pause";
        case NativeControlCode::NativeScreenshot:
            return "NativeScreenshot";
        case NativeControlCode::MenuConfirm:
            return "MenuConfirm";
        case NativeControlCode::MenuCancel:
            return "MenuCancel";
        case NativeControlCode::MenuScrollUp:
            return "MenuScrollUp";
        case NativeControlCode::MenuScrollDown:
            return "MenuScrollDown";
        case NativeControlCode::MenuLeft:
            return "MenuLeft";
        case NativeControlCode::MenuRight:
            return "MenuRight";
        case NativeControlCode::MenuDownloadAll:
            return "MenuDownloadAll";
        case NativeControlCode::DialoguePreviousOption:
            return "DialoguePreviousOption";
        case NativeControlCode::DialogueNextOption:
            return "DialogueNextOption";
        case NativeControlCode::FavoritesPreviousItem:
            return "FavoritesPreviousItem";
        case NativeControlCode::FavoritesNextItem:
            return "FavoritesNextItem";
        case NativeControlCode::FavoritesAccept:
            return "FavoritesAccept";
        case NativeControlCode::FavoritesCancel:
            return "FavoritesCancel";
        case NativeControlCode::FavoritesUp:
            return "FavoritesUp";
        case NativeControlCode::FavoritesDown:
            return "FavoritesDown";
        case NativeControlCode::ConsoleExecute:
            return "ConsoleExecute";
        case NativeControlCode::ConsoleHistoryUp:
            return "ConsoleHistoryUp";
        case NativeControlCode::ConsoleHistoryDown:
            return "ConsoleHistoryDown";
        case NativeControlCode::ConsolePickPrevious:
            return "ConsolePickPrevious";
        case NativeControlCode::ConsolePickNext:
            return "ConsolePickNext";
        case NativeControlCode::ConsoleNextFocus:
            return "ConsoleNextFocus";
        case NativeControlCode::ConsolePreviousFocus:
            return "ConsolePreviousFocus";
        case NativeControlCode::MenuPageUp:
            return "MenuPageUp";
        case NativeControlCode::MenuPageDown:
            return "MenuPageDown";
        case NativeControlCode::MenuSortByName:
            return "MenuSortByName";
        case NativeControlCode::MenuSortByValue:
            return "MenuSortByValue";
        case NativeControlCode::ItemZoom:
            return "ItemZoom";
        case NativeControlCode::ItemXButton:
            return "ItemXButton";
        case NativeControlCode::ItemYButton:
            return "ItemYButton";
        case NativeControlCode::InventoryChargeItem:
            return "InventoryChargeItem";
        case NativeControlCode::BookClose:
            return "BookClose";
        case NativeControlCode::BookPreviousPage:
            return "BookPreviousPage";
        case NativeControlCode::BookNextPage:
            return "BookNextPage";
        case NativeControlCode::MapCancel:
            return "MapCancel";
        case NativeControlCode::MapClick:
            return "MapClick";
        case NativeControlCode::MapOpenJournal:
            return "MapOpenJournal";
        case NativeControlCode::MapPlayerPosition:
            return "MapPlayerPosition";
        case NativeControlCode::MapLocalMap:
            return "MapLocalMap";
        case NativeControlCode::CursorClick:
            return "CursorClick";
        case NativeControlCode::JournalXButton:
            return "JournalXButton";
        case NativeControlCode::JournalYButton:
            return "JournalYButton";
        case NativeControlCode::DebugOverlayNextFocus:
            return "DebugOverlayNextFocus";
        case NativeControlCode::DebugOverlayPreviousFocus:
            return "DebugOverlayPreviousFocus";
        case NativeControlCode::DebugOverlayUp:
            return "DebugOverlayUp";
        case NativeControlCode::DebugOverlayDown:
            return "DebugOverlayDown";
        case NativeControlCode::DebugOverlayLeft:
            return "DebugOverlayLeft";
        case NativeControlCode::DebugOverlayRight:
            return "DebugOverlayRight";
        case NativeControlCode::DebugOverlayToggleMinimize:
            return "DebugOverlayToggleMinimize";
        case NativeControlCode::DebugOverlayToggleMove:
            return "DebugOverlayToggleMove";
        case NativeControlCode::DebugOverlayB:
            return "DebugOverlayB";
        case NativeControlCode::DebugOverlayY:
            return "DebugOverlayY";
        case NativeControlCode::DebugOverlayX:
            return "DebugOverlayX";
        case NativeControlCode::TFCWorldZUp:
            return "TFCWorldZUp";
        case NativeControlCode::TFCWorldZDown:
            return "TFCWorldZDown";
        case NativeControlCode::TFCLockToZPlane:
            return "TFCLockToZPlane";
        case NativeControlCode::LockpickingDebugMode:
            return "LockpickingDebugMode";
        case NativeControlCode::LockpickingCancel:
            return "LockpickingCancel";
        case NativeControlCode::CreationsAccept:
            return "CreationsAccept";
        case NativeControlCode::CreationsCancel:
            return "CreationsCancel";
        case NativeControlCode::CreationsUp:
            return "CreationsUp";
        case NativeControlCode::CreationsDown:
            return "CreationsDown";
        case NativeControlCode::CreationsLeft:
            return "CreationsLeft";
        case NativeControlCode::CreationsRight:
            return "CreationsRight";
        case NativeControlCode::CreationsOptions:
            return "CreationsOptions";
        case NativeControlCode::CreationsLoadOrderAndDelete:
            return "CreationsLoadOrderAndDelete";
        case NativeControlCode::CreationsLikeUnlike:
            return "CreationsLikeUnlike";
        case NativeControlCode::CreationsSearchEdit:
            return "CreationsSearchEdit";
        case NativeControlCode::CreationsPurchaseCredits:
            return "CreationsPurchaseCredits";
        case NativeControlCode::FavorCancel:
            return "FavorCancel";
        case NativeControlCode::Wait:
            return "Wait";
        case NativeControlCode::Journal:
            return "Journal";
        case NativeControlCode::MoveStick:
            return "MoveStick";
        case NativeControlCode::LookStick:
            return "LookStick";
        case NativeControlCode::MenuStick:
            return "MenuStick";
        case NativeControlCode::FavoritesLeftStick:
            return "FavoritesLeftStick";
        case NativeControlCode::ItemLeftEquipTrigger:
            return "ItemLeftEquipTrigger";
        case NativeControlCode::ItemRightEquipTrigger:
            return "ItemRightEquipTrigger";
        case NativeControlCode::ItemRotateStick:
            return "ItemRotateStick";
        case NativeControlCode::MapLookStick:
            return "MapLookStick";
        case NativeControlCode::MapZoomOutTrigger:
            return "MapZoomOutTrigger";
        case NativeControlCode::MapZoomInTrigger:
            return "MapZoomInTrigger";
        case NativeControlCode::MapCursorStick:
            return "MapCursorStick";
        case NativeControlCode::StatsRotateStick:
            return "StatsRotateStick";
        case NativeControlCode::CursorMoveStick:
            return "CursorMoveStick";
        case NativeControlCode::JournalTabLeftTrigger:
            return "JournalTabLeftTrigger";
        case NativeControlCode::JournalTabRightTrigger:
            return "JournalTabRightTrigger";
        case NativeControlCode::DebugOverlayLeftTrigger:
            return "DebugOverlayLeftTrigger";
        case NativeControlCode::DebugOverlayRightTrigger:
            return "DebugOverlayRightTrigger";
        case NativeControlCode::TFCCameraZDownTrigger:
            return "TFCCameraZDownTrigger";
        case NativeControlCode::TFCCameraZUpTrigger:
            return "TFCCameraZUpTrigger";
        case NativeControlCode::DebugMapLookStick:
            return "DebugMapLookStick";
        case NativeControlCode::DebugMapZoomOutTrigger:
            return "DebugMapZoomOutTrigger";
        case NativeControlCode::DebugMapZoomInTrigger:
            return "DebugMapZoomInTrigger";
        case NativeControlCode::DebugMapMoveStick:
            return "DebugMapMoveStick";
        case NativeControlCode::LockpickingRotatePickStick:
            return "LockpickingRotatePickStick";
        case NativeControlCode::LockpickingRotateLockStick:
            return "LockpickingRotateLockStick";
        case NativeControlCode::CreationsLeftStick:
            return "CreationsLeftStick";
        case NativeControlCode::CreationsCategorySideBarTrigger:
            return "CreationsCategorySideBarTrigger";
        case NativeControlCode::CreationsFilterTrigger:
            return "CreationsFilterTrigger";
        case NativeControlCode::LeftTriggerAxis:
            return "LeftTriggerAxis";
        case NativeControlCode::RightTriggerAxis:
            return "RightTriggerAxis";
        case NativeControlCode::None:
        default:
            return "None";
        }
    }
}
