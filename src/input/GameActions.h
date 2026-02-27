#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace dualpad::input::actions
{
    // -------- canonical game semantic ids --------
    inline constexpr std::string_view Confirm = "Game.Confirm";
    inline constexpr std::string_view Back = "Game.Back";
    inline constexpr std::string_view CloseMenu = "Game.CloseMenu";

    inline constexpr std::string_view NavigateUp = "Game.NavigateUp";
    inline constexpr std::string_view NavigateDown = "Game.NavigateDown";
    inline constexpr std::string_view NavigateLeft = "Game.NavigateLeft";
    inline constexpr std::string_view NavigateRight = "Game.NavigateRight";
    inline constexpr std::string_view TabSwitch = "Game.TabSwitch";
    inline constexpr std::string_view PageUp = "Game.PageUp";
    inline constexpr std::string_view PageDown = "Game.PageDown";

    inline constexpr std::string_view OpenInventory = "Game.OpenInventory";
    inline constexpr std::string_view OpenMagic = "Game.OpenMagic";
    inline constexpr std::string_view OpenMap = "Game.OpenMap";
    inline constexpr std::string_view OpenLocalMap = "Game.OpenLocalMap";
    inline constexpr std::string_view OpenJournal = "Game.OpenJournal";
    inline constexpr std::string_view OpenFavorites = "Game.OpenFavorites";
    inline constexpr std::string_view OpenTweenMenu = "Game.OpenTweenMenu";
    inline constexpr std::string_view OpenPauseMenu = "Game.OpenPauseMenu";
    inline constexpr std::string_view OpenOptions = "Game.OpenOptions";

    inline constexpr std::string_view Activate = "Game.Activate";
    inline constexpr std::string_view Jump = "Game.Jump";
    inline constexpr std::string_view Shout = "Game.Shout";
    inline constexpr std::string_view ToggleSneak = "Game.ToggleSneak";
    inline constexpr std::string_view SprintHold = "Game.SprintHold";
    inline constexpr std::string_view ToggleRun = "Game.ToggleRun";
    inline constexpr std::string_view LeftEquip = "Game.LeftEquip";
    inline constexpr std::string_view RightEquip = "Game.RightEquip";
    inline constexpr std::string_view ChargeItem = "Game.ChargeItem";
    inline constexpr std::string_view Wait = "Game.Wait";

    inline constexpr std::string_view QuickSave = "Game.QuickSave";
    inline constexpr std::string_view QuickLoad = "Game.QuickLoad";
    inline constexpr std::string_view Screenshot = "Game.Screenshot";
    inline constexpr std::string_view ToggleConsole = "Game.ToggleConsole";

    // axis
    inline constexpr std::string_view MoveX = "Game.MoveX";
    inline constexpr std::string_view MoveY = "Game.MoveY";
    inline constexpr std::string_view LookX = "Game.LookX";
    inline constexpr std::string_view LookY = "Game.LookY";
    inline constexpr std::string_view TriggerL = "Game.TriggerL";
    inline constexpr std::string_view TriggerR = "Game.TriggerR";

    enum class ButtonAction : std::uint16_t
    {
        Unknown = 0,
        Confirm,
        Back,
        CloseMenu,
        NavigateUp,
        NavigateDown,
        NavigateLeft,
        NavigateRight,
        TabSwitch,
        PageUp,
        PageDown,
        OpenInventory,
        OpenMagic,
        OpenMap,
        OpenLocalMap,
        OpenJournal,
        OpenFavorites,
        OpenTweenMenu,
        OpenPauseMenu,
        OpenOptions,
        Activate,
        Jump,
        Shout,
        ToggleSneak,
        SprintHold,
        ToggleRun,
        LeftEquip,
        RightEquip,
        ChargeItem,
        Wait,
        QuickSave,
        QuickLoad,
        Screenshot,
        ToggleConsole
    };

    enum class AxisAction : std::uint8_t
    {
        Unknown = 0,
        MoveX,
        MoveY,
        LookX,
        LookY,
        TriggerL,
        TriggerR
    };

    ButtonAction ParseButtonAction(std::string_view id);
    AxisAction ParseAxisAction(std::string_view id);

    std::string_view ToActionId(ButtonAction a);
    std::string_view ToActionId(AxisAction a);

    inline std::string ToString(std::string_view v) { return std::string(v); }

    inline bool IsGameAction(std::string_view id)
    {
        return id.rfind("Game.", 0) == 0;
    }

    inline bool IsKnownAction(std::string_view id)
    {
        return ParseButtonAction(id) != ButtonAction::Unknown ||
            ParseAxisAction(id) != AxisAction::Unknown;
    }
}