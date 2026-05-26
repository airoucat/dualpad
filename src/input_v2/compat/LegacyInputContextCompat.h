#pragma once

#include <cstdint>
#include <string_view>

namespace dualpad::input_v2::compat
{
    enum class LegacyInputContextCompat : std::uint16_t
    {
        Gameplay = 0,

        Menu = 100,
        InventoryMenu,
        MagicMenu,
        MapMenu,
        JournalMenu,
        DialogueMenu,
        FavoritesMenu,
        TweenMenu,
        ContainerMenu,
        BarterMenu,
        TrainingMenu,
        LevelUpMenu,
        RaceSexMenu,
        StatsMenu,
        SkillMenu,
        BookMenu,
        MessageBoxMenu,
        QuantityMenu,
        GiftMenu,
        CreationsMenu,

        Console = 200,
        ItemMenu = 300,
        DebugText = 400,
        MapMenuContext = 500,
        Stats = 600,
        Cursor = 700,
        Book = 800,
        DebugOverlay = 900,
        Lockpicking = 1000,
        TFCMode = 1100,
        DebugMapMenu = 1200,
        Favor = 1300,

        Combat = 2000,
        Sneaking,
        Riding,
        Werewolf,
        VampireLord,
        Death,
        Bleedout,
        Ragdoll,
        KillMove,

        Unknown = 9999
    };

    inline constexpr std::string_view ToLegacyInputContextString(LegacyInputContextCompat ctx)
    {
        switch (ctx) {
        case LegacyInputContextCompat::Gameplay: return "Gameplay";
        case LegacyInputContextCompat::Menu: return "Menu";
        case LegacyInputContextCompat::InventoryMenu: return "InventoryMenu";
        case LegacyInputContextCompat::MagicMenu: return "MagicMenu";
        case LegacyInputContextCompat::MapMenu: return "MapMenu";
        case LegacyInputContextCompat::JournalMenu: return "JournalMenu";
        case LegacyInputContextCompat::DialogueMenu: return "DialogueMenu";
        case LegacyInputContextCompat::FavoritesMenu: return "FavoritesMenu";
        case LegacyInputContextCompat::TweenMenu: return "TweenMenu";
        case LegacyInputContextCompat::ContainerMenu: return "ContainerMenu";
        case LegacyInputContextCompat::BarterMenu: return "BarterMenu";
        case LegacyInputContextCompat::TrainingMenu: return "TrainingMenu";
        case LegacyInputContextCompat::LevelUpMenu: return "LevelUpMenu";
        case LegacyInputContextCompat::RaceSexMenu: return "RaceSexMenu";
        case LegacyInputContextCompat::StatsMenu: return "StatsMenu";
        case LegacyInputContextCompat::SkillMenu: return "SkillMenu";
        case LegacyInputContextCompat::BookMenu: return "BookMenu";
        case LegacyInputContextCompat::MessageBoxMenu: return "MessageBoxMenu";
        case LegacyInputContextCompat::QuantityMenu: return "QuantityMenu";
        case LegacyInputContextCompat::GiftMenu: return "GiftMenu";
        case LegacyInputContextCompat::CreationsMenu: return "CreationsMenu";
        case LegacyInputContextCompat::Console: return "Console";
        case LegacyInputContextCompat::ItemMenu: return "ItemMenu";
        case LegacyInputContextCompat::DebugText: return "DebugText";
        case LegacyInputContextCompat::MapMenuContext: return "MapMenuContext";
        case LegacyInputContextCompat::Stats: return "Stats";
        case LegacyInputContextCompat::Cursor: return "Cursor";
        case LegacyInputContextCompat::Book: return "Book";
        case LegacyInputContextCompat::DebugOverlay: return "DebugOverlay";
        case LegacyInputContextCompat::Lockpicking: return "Lockpicking";
        case LegacyInputContextCompat::TFCMode: return "TFCMode";
        case LegacyInputContextCompat::DebugMapMenu: return "DebugMapMenu";
        case LegacyInputContextCompat::Favor: return "Favor";
        case LegacyInputContextCompat::Combat: return "Combat";
        case LegacyInputContextCompat::Sneaking: return "Sneaking";
        case LegacyInputContextCompat::Riding: return "Riding";
        case LegacyInputContextCompat::Werewolf: return "Werewolf";
        case LegacyInputContextCompat::VampireLord: return "VampireLord";
        case LegacyInputContextCompat::Death: return "Death";
        case LegacyInputContextCompat::Bleedout: return "Bleedout";
        case LegacyInputContextCompat::Ragdoll: return "Ragdoll";
        case LegacyInputContextCompat::KillMove: return "KillMove";
        default: return "Unknown";
        }
    }
}

namespace dualpad::input
{
    using InputContext = dualpad::input_v2::compat::LegacyInputContextCompat;

    inline constexpr std::string_view ToString(InputContext ctx)
    {
        return dualpad::input_v2::compat::ToLegacyInputContextString(ctx);
    }
}
