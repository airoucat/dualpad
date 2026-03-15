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
        inline constexpr std::string_view Sprint = "Game.Sprint";
        inline constexpr std::string_view Sneak = "Game.Sneak";
        inline constexpr std::string_view Shout = "Game.Shout";

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

        inline constexpr std::string_view ConsoleExecute = "Console.Execute";
        inline constexpr std::string_view ConsoleHistoryUp = "Console.HistoryUp";
        inline constexpr std::string_view ConsoleHistoryDown = "Console.HistoryDown";

        inline constexpr std::string_view DialoguePreviousOption = "Dialogue.PreviousOption";
        inline constexpr std::string_view DialogueNextOption = "Dialogue.NextOption";

        inline constexpr std::string_view FavoritesPreviousItem = "Favorites.PreviousItem";
        inline constexpr std::string_view FavoritesNextItem = "Favorites.NextItem";

        inline constexpr std::string_view BookClose = "Book.Close";
        inline constexpr std::string_view BookPreviousPage = "Book.PreviousPage";
        inline constexpr std::string_view BookNextPage = "Book.NextPage";

        // Extended actions that open UI screens or toggle presentation state.
        inline constexpr std::string_view OpenInventory = "Game.OpenInventory";
        inline constexpr std::string_view OpenMagic = "Game.OpenMagic";
        inline constexpr std::string_view OpenMap = "Game.OpenMap";
        inline constexpr std::string_view OpenJournal = "Game.OpenJournal";
        inline constexpr std::string_view OpenFavorites = "Game.OpenFavorites";
        inline constexpr std::string_view OpenSkills = "Game.OpenSkills";

        inline constexpr std::string_view TogglePOV = "Game.TogglePOV";
        inline constexpr std::string_view ToggleHUD = "Game.ToggleHUD";
        inline constexpr std::string_view Screenshot = "Game.Screenshot";

        // Utility actions that do not fit normal movement or combat input.
        inline constexpr std::string_view Wait = "Game.Wait";
        inline constexpr std::string_view QuickSave = "Game.QuickSave";
        inline constexpr std::string_view QuickLoad = "Game.QuickLoad";
    }

    struct ActionMetadata
    {
        std::string id;
        std::string displayName;
        std::string description;
        std::string glyphPath;
    };
}
