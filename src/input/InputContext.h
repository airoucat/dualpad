#pragma once
#include <cstdint>
#include <string_view>
#include <vector>

namespace dualpad::input
{
    enum class InputContext : std::uint16_t
    {
        // === 主要游戏玩法 (Main Gameplay) ===
        Gameplay = 0,

        // === 菜单模式 (Menu Mode) ===
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

        // === 控制台 (Console) ===
        Console = 200,

        // === 物品菜单 (Item Menus) ===
        ItemMenu = 300,

        // === 调试文本 (Debug Text) ===
        DebugText = 400,

        // === 地图菜单 (Map Menu) ===
        MapMenuContext = 500,

        // === 统计 (Stats) ===
        Stats = 600,

        // === 光标 (Cursor) ===
        Cursor = 700,

        // === 书籍 (Book) ===
        Book = 800,

        // === 调试覆盖 (Debug Overlay) ===
        DebugOverlay = 900,

        // === 开锁 (Lockpicking) ===
        Lockpicking = 1000,

        // === TFC 模式 (TFC Mode) ===
        TFCMode = 1100,

        // === 调试地图模式 (Debug Map Menu) ===
        DebugMapMenu = 1200,

        // === 好感度 (Favor) ===
        Favor = 1300,

        // === 特殊游戏状态 ===
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

    class ContextManager
    {
    public:
        static ContextManager& GetSingleton();

        InputContext GetCurrentContext() const;

        void OnMenuOpen(std::string_view menuName);
        void OnMenuClose(std::string_view menuName);

        void UpdateGameplayContext();

        void PushContext(InputContext context);
        void PopContext();
        void SetContext(InputContext context);

    private:
        ContextManager() = default;

        InputContext _currentContext{ InputContext::Gameplay };
        std::vector<InputContext> _contextStack;

        InputContext MenuNameToContext(std::string_view menuName) const;
        InputContext DetectGameplayContext() const;
    };
}