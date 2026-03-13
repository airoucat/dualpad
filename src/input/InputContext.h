#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace dualpad::input
{
    enum class InputContext : std::uint16_t
    {
        // Base gameplay states stay in the low range.
        Gameplay = 0,

        // Menu contexts occupy the 100+ range for cheap filtering.
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

    inline constexpr std::string_view ToString(InputContext ctx)
    {
        switch (ctx) {
        case InputContext::Gameplay: return "Gameplay";

        case InputContext::Menu: return "Menu";
        case InputContext::InventoryMenu: return "InventoryMenu";
        case InputContext::MagicMenu: return "MagicMenu";
        case InputContext::MapMenu: return "MapMenu";
        case InputContext::JournalMenu: return "JournalMenu";
        case InputContext::DialogueMenu: return "DialogueMenu";
        case InputContext::FavoritesMenu: return "FavoritesMenu";
        case InputContext::TweenMenu: return "TweenMenu";
        case InputContext::ContainerMenu: return "ContainerMenu";
        case InputContext::BarterMenu: return "BarterMenu";
        case InputContext::TrainingMenu: return "TrainingMenu";
        case InputContext::LevelUpMenu: return "LevelUpMenu";
        case InputContext::RaceSexMenu: return "RaceSexMenu";
        case InputContext::StatsMenu: return "StatsMenu";
        case InputContext::SkillMenu: return "SkillMenu";
        case InputContext::BookMenu: return "BookMenu";
        case InputContext::MessageBoxMenu: return "MessageBoxMenu";
        case InputContext::QuantityMenu: return "QuantityMenu";
        case InputContext::GiftMenu: return "GiftMenu";
        case InputContext::CreationsMenu: return "CreationsMenu";

        case InputContext::Console: return "Console";
        case InputContext::ItemMenu: return "ItemMenu";
        case InputContext::DebugText: return "DebugText";
        case InputContext::MapMenuContext: return "MapMenuContext";
        case InputContext::Stats: return "Stats";
        case InputContext::Cursor: return "Cursor";
        case InputContext::Book: return "Book";
        case InputContext::DebugOverlay: return "DebugOverlay";
        case InputContext::Lockpicking: return "Lockpicking";
        case InputContext::TFCMode: return "TFCMode";
        case InputContext::DebugMapMenu: return "DebugMapMenu";
        case InputContext::Favor: return "Favor";

        case InputContext::Combat: return "Combat";
        case InputContext::Sneaking: return "Sneaking";
        case InputContext::Riding: return "Riding";
        case InputContext::Werewolf: return "Werewolf";
        case InputContext::VampireLord: return "VampireLord";
        case InputContext::Death: return "Death";
        case InputContext::Bleedout: return "Bleedout";
        case InputContext::Ragdoll: return "Ragdoll";
        case InputContext::KillMove: return "KillMove";

        default: return "Unknown";
        }
    }

    // Tracks the active context and restores previous contexts when menus close.
    class ContextManager
    {
    public:
        static ContextManager& GetSingleton();

        InputContext GetCurrentContext() const;

        // Menu events push and restore UI contexts around their lifetime.
        void OnMenuOpen(std::string_view menuName);
        void OnMenuClose(std::string_view menuName);

        // Polls player state for gameplay-only transitions such as sneak or death.
        void UpdateFrameState();
        void UpdateGameplayContext();

        void PushContext(InputContext context);
        void PopContext();
        void SetContext(InputContext context);

    private:
        struct MenuContextEntry
        {
            std::string menuName;
            InputContext context{ InputContext::Menu };
        };

        ContextManager() = default;

        InputContext _currentContext{ InputContext::Gameplay };
        InputContext _baseContext{ InputContext::Gameplay };
        std::vector<InputContext> _contextStack;
        std::vector<MenuContextEntry> _menuStack;
        mutable std::mutex _mutex;

        InputContext MenuNameToContext(std::string_view menuName) const;
        InputContext DetectGameplayContext() const;
        void RefreshCurrentContextLocked();
        bool ShouldTrackMenu(std::string_view menuName) const;
    };
}
