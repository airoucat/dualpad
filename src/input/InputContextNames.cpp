#include "pch.h"

#include "input/InputContextNames.h"

namespace dualpad::input
{
    std::optional<InputContext> ParseInputContextName(std::string_view name)
    {
        if (name == "Gameplay") return InputContext::Gameplay;

        if (name == "Menu" ||
            name == "Main Menu" ||
            name == "Loading Menu" ||
            name == "Credits Menu" ||
            name == "Crafting Menu" ||
            name == "TitleSequence Menu" ||
            name == "Sleep/Wait Menu" ||
            name == "Kinect Menu" ||
            name == "SafeZoneMenu" ||
            name == "StreamingInstallMenu") {
            return InputContext::Menu;
        }

        if (name == "InventoryMenu" || name == "Inventory Menu") return InputContext::InventoryMenu;
        if (name == "MagicMenu" || name == "Magic Menu") return InputContext::MagicMenu;
        if (name == "MapMenu" || name == "Map Menu") return InputContext::MapMenu;
        if (name == "JournalMenu" || name == "Journal Menu") return InputContext::JournalMenu;
        if (name == "DialogueMenu" || name == "Dialogue Menu") return InputContext::DialogueMenu;
        if (name == "FavoritesMenu" || name == "Favorites Menu") return InputContext::FavoritesMenu;
        if (name == "TweenMenu" || name == "Tween Menu") return InputContext::TweenMenu;
        if (name == "ContainerMenu" || name == "Container Menu") return InputContext::ContainerMenu;
        if (name == "BarterMenu" || name == "Barter Menu") return InputContext::BarterMenu;
        if (name == "TrainingMenu" || name == "Training Menu") return InputContext::TrainingMenu;
        if (name == "LevelUpMenu" || name == "LevelUp Menu") return InputContext::LevelUpMenu;
        if (name == "RaceSexMenu" || name == "RaceSex Menu") return InputContext::RaceSexMenu;
        if (name == "StatsMenu" || name == "Stats Menu") return InputContext::StatsMenu;
        if (name == "SkillMenu" || name == "Skill Menu") return InputContext::SkillMenu;
        if (name == "BookMenu" || name == "Book Menu") return InputContext::BookMenu;
        if (name == "MessageBoxMenu" || name == "MessageBox Menu") return InputContext::MessageBoxMenu;
        if (name == "QuantityMenu" || name == "Quantity Menu") return InputContext::QuantityMenu;
        if (name == "GiftMenu" || name == "Gift Menu") return InputContext::GiftMenu;
        if (name == "CreationsMenu" || name == "Creations Menu") return InputContext::CreationsMenu;
        if (name == "CreationClubMenu" || name == "Creation Club Menu") return InputContext::CreationsMenu;
        if (name == "Mod Manager Menu") return InputContext::CreationsMenu;

        if (name == "Console" || name == "Console Native UI Menu") return InputContext::Console;
        if (name == "ItemMenu" || name == "Item Menu") return InputContext::ItemMenu;
        if (name == "DebugText" || name == "Debug Text Menu") return InputContext::DebugText;
        if (name == "MapMenuContext") return InputContext::MapMenuContext;
        if (name == "Stats") return InputContext::Stats;
        if (name == "Cursor" || name == "Cursor Menu" || name == "CursorMenu") return InputContext::Cursor;
        if (name == "Book") return InputContext::Book;
        if (name == "DebugOverlay") return InputContext::DebugOverlay;
        if (name == "Lockpicking" || name == "LockpickingMenu" || name == "Lockpicking Menu") return InputContext::Lockpicking;
        if (name == "TFCMode") return InputContext::TFCMode;
        if (name == "DebugMapMenu") return InputContext::DebugMapMenu;
        if (name == "Favor") return InputContext::Favor;

        if (name == "Combat") return InputContext::Combat;
        if (name == "Sneaking") return InputContext::Sneaking;
        if (name == "Riding") return InputContext::Riding;
        if (name == "Werewolf") return InputContext::Werewolf;
        if (name == "VampireLord") return InputContext::VampireLord;
        if (name == "Death") return InputContext::Death;
        if (name == "Bleedout") return InputContext::Bleedout;
        if (name == "Ragdoll") return InputContext::Ragdoll;
        if (name == "KillMove") return InputContext::KillMove;

        return std::nullopt;
    }
}
