#pragma once
#include <string>
#include <string_view>

namespace dualpad::input
{
    namespace actions
    {
        // Base gameplay actions.
        inline constexpr std::string_view Jump = "Game.Jump";
        inline constexpr std::string_view Attack = "Game.Attack";
        inline constexpr std::string_view Block = "Game.Block";
        inline constexpr std::string_view Activate = "Game.Activate";
        inline constexpr std::string_view ReadyWeapon = "Game.ReadyWeapon";
        inline constexpr std::string_view TweenMenu = "Game.TweenMenu";
        inline constexpr std::string_view Sprint = "Game.Sprint";
        inline constexpr std::string_view Sneak = "Game.Sneak";
        inline constexpr std::string_view Shout = "Game.Shout";
        inline constexpr std::string_view Favorites = "Game.Favorites";
        inline constexpr std::string_view Hotkey1 = "Game.Hotkey1";
        inline constexpr std::string_view Hotkey2 = "Game.Hotkey2";
        inline constexpr std::string_view Hotkey3 = "Game.Hotkey3";
        inline constexpr std::string_view Hotkey4 = "Game.Hotkey4";
        inline constexpr std::string_view Hotkey5 = "Game.Hotkey5";
        inline constexpr std::string_view Hotkey6 = "Game.Hotkey6";
        inline constexpr std::string_view Hotkey7 = "Game.Hotkey7";
        inline constexpr std::string_view Hotkey8 = "Game.Hotkey8";

        // Menu navigation actions.
        inline constexpr std::string_view MenuConfirm = "Menu.Confirm";
        inline constexpr std::string_view MenuCancel = "Menu.Cancel";
        inline constexpr std::string_view MenuScrollUp = "Menu.ScrollUp";
        inline constexpr std::string_view MenuScrollDown = "Menu.ScrollDown";
        inline constexpr std::string_view MenuLeft = "Menu.Left";
        inline constexpr std::string_view MenuRight = "Menu.Right";
        inline constexpr std::string_view MenuPageUp = "Menu.PageUp";
        inline constexpr std::string_view MenuPageDown = "Menu.PageDown";
        inline constexpr std::string_view MenuSortByName = "Menu.SortByName";
        inline constexpr std::string_view MenuSortByValue = "Menu.SortByValue";
        inline constexpr std::string_view MenuDownloadAll = "Menu.DownloadAll";

        inline constexpr std::string_view ConsoleExecute = "Console.Execute";
        inline constexpr std::string_view ConsoleHistoryUp = "Console.HistoryUp";
        inline constexpr std::string_view ConsoleHistoryDown = "Console.HistoryDown";
        inline constexpr std::string_view ConsolePickPrevious = "Console.PickPrevious";
        inline constexpr std::string_view ConsolePickNext = "Console.PickNext";
        inline constexpr std::string_view ConsoleNextFocus = "Console.NextFocus";
        inline constexpr std::string_view ConsolePreviousFocus = "Console.PreviousFocus";

        inline constexpr std::string_view DialoguePreviousOption = "Dialogue.PreviousOption";
        inline constexpr std::string_view DialogueNextOption = "Dialogue.NextOption";

        inline constexpr std::string_view FavoritesPreviousItem = "Favorites.PreviousItem";
        inline constexpr std::string_view FavoritesNextItem = "Favorites.NextItem";
        inline constexpr std::string_view FavoritesAccept = "Favorites.Accept";
        inline constexpr std::string_view FavoritesCancel = "Favorites.Cancel";
        inline constexpr std::string_view FavoritesUp = "Favorites.Up";
        inline constexpr std::string_view FavoritesDown = "Favorites.Down";
        inline constexpr std::string_view FavoritesLeftStick = "Favorites.LeftStick";

        inline constexpr std::string_view ItemLeftEquip = "Item.LeftEquip";
        inline constexpr std::string_view ItemRightEquip = "Item.RightEquip";
        inline constexpr std::string_view ItemZoom = "Item.Zoom";
        inline constexpr std::string_view ItemRotate = "Item.Rotate";
        inline constexpr std::string_view ItemXButton = "Item.XButton";
        inline constexpr std::string_view ItemYButton = "Item.YButton";

        inline constexpr std::string_view InventoryChargeItem = "Inventory.ChargeItem";

        inline constexpr std::string_view BookClose = "Book.Close";
        inline constexpr std::string_view BookPreviousPage = "Book.PreviousPage";
        inline constexpr std::string_view BookNextPage = "Book.NextPage";

        inline constexpr std::string_view MapCancel = "Map.Cancel";
        inline constexpr std::string_view MapLook = "Map.Look";
        inline constexpr std::string_view MapZoomIn = "Map.ZoomIn";
        inline constexpr std::string_view MapZoomOut = "Map.ZoomOut";
        inline constexpr std::string_view MapClick = "Map.Click";
        inline constexpr std::string_view MapCursor = "Map.Cursor";
        inline constexpr std::string_view MapOpenJournal = "Map.OpenJournal";
        inline constexpr std::string_view MapPlayerPosition = "Map.PlayerPosition";
        inline constexpr std::string_view MapLocalMap = "Map.LocalMap";

        inline constexpr std::string_view StatsRotate = "Stats.Rotate";

        inline constexpr std::string_view CursorMove = "Cursor.Move";
        inline constexpr std::string_view CursorClick = "Cursor.Click";

        inline constexpr std::string_view JournalXButton = "Journal.XButton";
        inline constexpr std::string_view JournalYButton = "Journal.YButton";
        inline constexpr std::string_view JournalTabLeft = "Journal.TabLeft";
        inline constexpr std::string_view JournalTabRight = "Journal.TabRight";

        inline constexpr std::string_view DebugOverlayNextFocus = "DebugOverlay.NextFocus";
        inline constexpr std::string_view DebugOverlayPreviousFocus = "DebugOverlay.PreviousFocus";
        inline constexpr std::string_view DebugOverlayUp = "DebugOverlay.Up";
        inline constexpr std::string_view DebugOverlayDown = "DebugOverlay.Down";
        inline constexpr std::string_view DebugOverlayLeft = "DebugOverlay.Left";
        inline constexpr std::string_view DebugOverlayRight = "DebugOverlay.Right";
        inline constexpr std::string_view DebugOverlayToggleMinimize = "DebugOverlay.ToggleMinimize";
        inline constexpr std::string_view DebugOverlayToggleMove = "DebugOverlay.ToggleMove";
        inline constexpr std::string_view DebugOverlayLeftTrigger = "DebugOverlay.LeftTrigger";
        inline constexpr std::string_view DebugOverlayRightTrigger = "DebugOverlay.RightTrigger";
        inline constexpr std::string_view DebugOverlayB = "DebugOverlay.B";
        inline constexpr std::string_view DebugOverlayY = "DebugOverlay.Y";
        inline constexpr std::string_view DebugOverlayX = "DebugOverlay.X";

        inline constexpr std::string_view TFCCameraZUp = "TFC.CameraZUp";
        inline constexpr std::string_view TFCCameraZDown = "TFC.CameraZDown";
        inline constexpr std::string_view TFCWorldZUp = "TFC.WorldZUp";
        inline constexpr std::string_view TFCWorldZDown = "TFC.WorldZDown";
        inline constexpr std::string_view TFCLockToZPlane = "TFC.LockToZPlane";

        inline constexpr std::string_view DebugMapLook = "DebugMap.Look";
        inline constexpr std::string_view DebugMapZoomIn = "DebugMap.ZoomIn";
        inline constexpr std::string_view DebugMapZoomOut = "DebugMap.ZoomOut";
        inline constexpr std::string_view DebugMapMove = "DebugMap.Move";

        inline constexpr std::string_view LockpickingRotatePick = "Lockpicking.RotatePick";
        inline constexpr std::string_view LockpickingRotateLock = "Lockpicking.RotateLock";
        inline constexpr std::string_view LockpickingDebugMode = "Lockpicking.DebugMode";
        inline constexpr std::string_view LockpickingCancel = "Lockpicking.Cancel";

        inline constexpr std::string_view CreationsAccept = "Creations.Accept";
        inline constexpr std::string_view CreationsCancel = "Creations.Cancel";
        inline constexpr std::string_view CreationsUp = "Creations.Up";
        inline constexpr std::string_view CreationsDown = "Creations.Down";
        inline constexpr std::string_view CreationsLeft = "Creations.Left";
        inline constexpr std::string_view CreationsRight = "Creations.Right";
        inline constexpr std::string_view CreationsOptions = "Creations.Options";
        inline constexpr std::string_view CreationsLeftStick = "Creations.LeftStick";
        inline constexpr std::string_view CreationsLoadOrderAndDelete = "Creations.LoadOrderAndDelete";
        inline constexpr std::string_view CreationsCategorySideBar = "Creations.CategorySideBar";
        inline constexpr std::string_view CreationsLikeUnlike = "Creations.LikeUnlike";
        inline constexpr std::string_view CreationsSearchEdit = "Creations.SearchEdit";
        inline constexpr std::string_view CreationsFilter = "Creations.Filter";
        inline constexpr std::string_view CreationsPurchaseCredits = "Creations.PurchaseCredits";

        inline constexpr std::string_view FavorCancel = "Favor.Cancel";

        // Extended actions that remain in the formal gameplay/native surface.
        inline constexpr std::string_view OpenJournal = "Game.OpenJournal";
        inline constexpr std::string_view Pause = "Game.Pause";

        inline constexpr std::string_view TogglePOV = "Game.TogglePOV";
        inline constexpr std::string_view ToggleHUD = "Game.ToggleHUD";
        inline constexpr std::string_view Screenshot = "Game.Screenshot";
        inline constexpr std::string_view NativeScreenshot = "Game.NativeScreenshot";

        // Utility actions that do not fit normal movement or combat input.
        inline constexpr std::string_view Wait = "Game.Wait";
    }

    struct ActionMetadata
    {
        std::string id;
        std::string displayName;
        std::string description;
        std::string glyphPath;
    };
}
