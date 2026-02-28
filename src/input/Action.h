#pragma once
#include <string>
#include <string_view>

namespace dualpad::input
{
    namespace actions
    {
        // ========================================
        // 游戏内动作（原生 XInput 映射已处理）
        // ========================================
        inline constexpr std::string_view Jump = "Game.Jump";             // 跳跃
        inline constexpr std::string_view Attack = "Game.Attack";         // 攻击
        inline constexpr std::string_view Block = "Game.Block";           // 格挡
        inline constexpr std::string_view Activate = "Game.Activate";     // 互动/激活
        inline constexpr std::string_view Sprint = "Game.Sprint";         // 冲刺
        inline constexpr std::string_view Sneak = "Game.Sneak";           // 潜行
        inline constexpr std::string_view Shout = "Game.Shout";           // 龙吼/力量

        // ========================================
        // 菜单内动作
        // ========================================
        inline constexpr std::string_view MenuConfirm = "Menu.Confirm";       // 菜单确认
        inline constexpr std::string_view MenuCancel = "Menu.Cancel";         // 菜单取消
        inline constexpr std::string_view MenuPageUp = "Menu.PageUp";         // 菜单向上翻页
        inline constexpr std::string_view MenuPageDown = "Menu.PageDown";     // 菜单向下翻页

        // ========================================
        // 扩展动作（触摸板/背键/Fn组合键触发）
        // ========================================

        // --- 打开菜单 ---
        inline constexpr std::string_view OpenInventory = "Game.OpenInventory";   // 打开背包
        inline constexpr std::string_view OpenMagic = "Game.OpenMagic";           // 打开魔法菜单
        inline constexpr std::string_view OpenMap = "Game.OpenMap";               // 打开地图
        inline constexpr std::string_view OpenJournal = "Game.OpenJournal";       // 打开日志/任务
        inline constexpr std::string_view OpenFavorites = "Game.OpenFavorites";   // 打开收藏夹
        inline constexpr std::string_view OpenSkills = "Game.OpenSkills";         // 打开技能/升级菜单

        // --- 视角与界面 ---
        inline constexpr std::string_view TogglePOV = "Game.TogglePOV";           // 切换第一/第三人称视角
        inline constexpr std::string_view ToggleHUD = "Game.ToggleHUD";           // 切换HUD显示/隐藏（截图、沉浸式游玩）
        inline constexpr std::string_view Screenshot = "Game.Screenshot";         // 截图

        // --- 游戏功能 ---
        inline constexpr std::string_view Wait = "Game.Wait";                     // 打开等待/休息菜单（等待时间流逝、恢复生命）
        inline constexpr std::string_view QuickSave = "Game.QuickSave";           // 快速存档（暂未实现，mod整合包中有风险）
        inline constexpr std::string_view QuickLoad = "Game.QuickLoad";           // 快速读档（暂未实现，mod整合包中有风险）
    }

    struct ActionMetadata
    {
        std::string id;
        std::string displayName;
        std::string description;
        std::string glyphPath;
    };
}