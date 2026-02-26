#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace dualpad::input::actions
{
    // -------- canonical action ids --------
    inline constexpr std::string_view OpenInventory = "Game.OpenInventory";
    inline constexpr std::string_view OpenMagic = "Game.OpenMagic";
    inline constexpr std::string_view OpenMap = "Game.OpenMap";
    inline constexpr std::string_view OpenJournal = "Game.OpenJournal";

    inline constexpr std::string_view MoveX = "Game.MoveX";
    inline constexpr std::string_view MoveY = "Game.MoveY";
    inline constexpr std::string_view LookX = "Game.LookX";
    inline constexpr std::string_view LookY = "Game.LookY";
    inline constexpr std::string_view TriggerL = "Game.TriggerL";
    inline constexpr std::string_view TriggerR = "Game.TriggerR";

    enum class ButtonAction : std::uint8_t
    {
        Unknown = 0,
        OpenInventory,
        OpenMagic,
        OpenMap,
        OpenJournal
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

    // id <-> enum
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