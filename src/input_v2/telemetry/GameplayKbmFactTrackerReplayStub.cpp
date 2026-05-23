#include "pch.h"
#include "input/GameplayKbmFactTracker.h"

namespace dualpad::input
{
    bool GameplayKbmFacts::IsKeyboardMoveActive() const
    {
        return keyboardMoveDownMask != 0;
    }

    bool GameplayKbmFacts::IsKeyboardMouseCombatActive() const
    {
        return keyboardCombatDownMask != 0 || mouseCombatDownMask != 0;
    }

    bool GameplayKbmFacts::IsKeyboardMouseDigitalActive() const
    {
        return keyboardDigitalDownMask != 0 || mouseDigitalDownMask != 0;
    }

    bool GameplayKbmFacts::IsKeyboardMouseSprintActive() const
    {
        return false;
    }

    GameplayKbmFactTracker& GameplayKbmFactTracker::GetSingleton()
    {
        static GameplayKbmFactTracker instance;
        return instance;
    }

    void GameplayKbmFactTracker::Reset()
    {
    }

    void GameplayKbmFactTracker::ObserveButtonEvent(const RE::ButtonEvent&)
    {
    }

    void GameplayKbmFactTracker::MarkMouseLookActivity()
    {
    }

    GameplayKbmFacts GameplayKbmFactTracker::GetFacts() const
    {
        return {};
    }

    bool GameplayKbmFactTracker::IsMouseLookActive() const
    {
        return false;
    }

    bool GameplayKbmFactTracker::IsSprintMappedButton(const RE::ButtonEvent&) const
    {
        return false;
    }

    bool GameplayKbmFactTracker::IsMappedGameplayKeyboardMoveKey(std::uint32_t) const
    {
        return false;
    }

    bool GameplayKbmFactTracker::IsMappedGameplayKeyboardCombatKey(std::uint32_t) const
    {
        return false;
    }

    bool GameplayKbmFactTracker::IsMappedGameplayMouseCombatButton(std::uint32_t) const
    {
        return false;
    }

    bool GameplayKbmFactTracker::IsMappedGameplayKeyboardDigitalKey(std::uint32_t) const
    {
        return false;
    }

    bool GameplayKbmFactTracker::IsMappedGameplayMouseDigitalButton(std::uint32_t) const
    {
        return false;
    }

    bool GameplayKbmFactTracker::IsMappedGameplayKeyboardSprintKey(std::uint32_t) const
    {
        return false;
    }

    bool GameplayKbmFactTracker::IsMappedGameplayMouseSprintButton(std::uint32_t) const
    {
        return false;
    }
}
