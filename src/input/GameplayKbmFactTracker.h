#pragma once

#include <RE/Skyrim.h>

#include <atomic>
#include <cstdint>

namespace dualpad::input
{
    struct GameplayKbmFacts
    {
        std::uint32_t keyboardMoveDownMask{ 0 };
        std::uint32_t keyboardCombatDownMask{ 0 };
        std::uint32_t mouseCombatDownMask{ 0 };
        std::uint32_t keyboardDigitalDownMask{ 0 };
        std::uint32_t mouseDigitalDownMask{ 0 };
        bool mouseLookActive{ false };

        [[nodiscard]] bool IsKeyboardMoveActive() const;
        [[nodiscard]] bool IsKeyboardMouseCombatActive() const;
        [[nodiscard]] bool IsKeyboardMouseDigitalActive() const;
        [[nodiscard]] bool IsKeyboardMouseSprintActive() const;
    };

    class GameplayKbmFactTracker
    {
    public:
        static GameplayKbmFactTracker& GetSingleton();

        void Reset();
        void ObserveButtonEvent(const RE::ButtonEvent& event);
        void MarkMouseLookActivity();

        [[nodiscard]] GameplayKbmFacts GetFacts() const;
        [[nodiscard]] bool IsMouseLookActive() const;
        [[nodiscard]] bool IsSprintMappedButton(const RE::ButtonEvent& event) const;

    private:
        GameplayKbmFactTracker() = default;

        bool IsMappedGameplayKeyboardMoveKey(std::uint32_t idCode) const;
        bool IsMappedGameplayKeyboardCombatKey(std::uint32_t idCode) const;
        bool IsMappedGameplayMouseCombatButton(std::uint32_t idCode) const;
        bool IsMappedGameplayKeyboardDigitalKey(std::uint32_t idCode) const;
        bool IsMappedGameplayMouseDigitalButton(std::uint32_t idCode) const;
        bool IsMappedGameplayKeyboardSprintKey(std::uint32_t idCode) const;
        bool IsMappedGameplayMouseSprintButton(std::uint32_t idCode) const;

        std::atomic<std::uint32_t> _keyboardMoveDownMask{ 0 };
        std::atomic<std::uint32_t> _keyboardCombatDownMask{ 0 };
        std::atomic<std::uint32_t> _mouseCombatDownMask{ 0 };
        std::atomic<std::uint32_t> _keyboardDigitalDownMask{ 0 };
        std::atomic<std::uint32_t> _mouseDigitalDownMask{ 0 };
        std::atomic<std::uint64_t> _lastMouseLookAtMs{ 0 };
    };
}
