#include "pch.h"
#include "input/GameplayKbmFactTracker.h"

#include "input/Action.h"

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        constexpr std::uint64_t kGameplayMouseLookActiveMs = 200;

        enum GameplayMoveSemanticBit : std::uint32_t
        {
            kMoveForwardBit = 1u << 0,
            kMoveBackBit = 1u << 1,
            kMoveStrafeLeftBit = 1u << 2,
            kMoveStrafeRightBit = 1u << 3
        };

        enum GameplayCombatSemanticBit : std::uint32_t
        {
            kCombatLeftBit = 1u << 0,
            kCombatRightBit = 1u << 1
        };

        enum GameplayDigitalSemanticBit : std::uint32_t
        {
            kDigitalJumpBit = 1u << 0,
            kDigitalActivateBit = 1u << 1,
            kDigitalSprintBit = 1u << 2
        };

        constexpr std::uint32_t kDigitalOwnerRelevantMask =
            kDigitalJumpBit |
            kDigitalActivateBit;

        std::uint64_t GetMonotonicMs()
        {
            return ::GetTickCount64();
        }

        std::uint32_t GetGameplayMappedKey(std::string_view eventId, RE::INPUT_DEVICE device)
        {
            const auto* controlMap = RE::ControlMap::GetSingleton();
            if (!controlMap) {
                return RE::ControlMap::kInvalid;
            }

            return controlMap->GetMappedKey(
                eventId,
                device,
                RE::ControlMap::InputContextID::kGameplay);
        }
    }

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
        return (keyboardDigitalDownMask & kDigitalOwnerRelevantMask) != 0 ||
            (mouseDigitalDownMask & kDigitalOwnerRelevantMask) != 0;
    }

    bool GameplayKbmFacts::IsKeyboardMouseSprintActive() const
    {
        return (keyboardDigitalDownMask & kDigitalSprintBit) != 0 ||
            (mouseDigitalDownMask & kDigitalSprintBit) != 0;
    }

    GameplayKbmFactTracker& GameplayKbmFactTracker::GetSingleton()
    {
        static GameplayKbmFactTracker instance;
        return instance;
    }

    void GameplayKbmFactTracker::Reset()
    {
        _keyboardMoveDownMask.store(0, std::memory_order_relaxed);
        _keyboardCombatDownMask.store(0, std::memory_order_relaxed);
        _mouseCombatDownMask.store(0, std::memory_order_relaxed);
        _keyboardDigitalDownMask.store(0, std::memory_order_relaxed);
        _mouseDigitalDownMask.store(0, std::memory_order_relaxed);
        _lastMouseLookAtMs.store(0, std::memory_order_relaxed);
    }

    void GameplayKbmFactTracker::ObserveButtonEvent(const RE::ButtonEvent& event)
    {
        const auto idCode = event.GetIDCode();
        const bool down = event.IsPressed();

        if (event.GetDevice() == RE::INPUT_DEVICE::kKeyboard) {
            auto moveMask = _keyboardMoveDownMask.load(std::memory_order_relaxed);
            if (IsMappedGameplayKeyboardMoveKey(idCode)) {
                const auto* userEvents = RE::UserEvents::GetSingleton();
                if (userEvents) {
                    if (idCode == GetGameplayMappedKey(userEvents->forward, RE::INPUT_DEVICE::kKeyboard)) {
                        moveMask = down ? (moveMask | kMoveForwardBit) : (moveMask & ~kMoveForwardBit);
                    }
                    if (idCode == GetGameplayMappedKey(userEvents->back, RE::INPUT_DEVICE::kKeyboard)) {
                        moveMask = down ? (moveMask | kMoveBackBit) : (moveMask & ~kMoveBackBit);
                    }
                    if (idCode == GetGameplayMappedKey(userEvents->strafeLeft, RE::INPUT_DEVICE::kKeyboard)) {
                        moveMask = down ? (moveMask | kMoveStrafeLeftBit) : (moveMask & ~kMoveStrafeLeftBit);
                    }
                    if (idCode == GetGameplayMappedKey(userEvents->strafeRight, RE::INPUT_DEVICE::kKeyboard)) {
                        moveMask = down ? (moveMask | kMoveStrafeRightBit) : (moveMask & ~kMoveStrafeRightBit);
                    }
                    _keyboardMoveDownMask.store(moveMask, std::memory_order_relaxed);
                }
            }

            auto combatMask = _keyboardCombatDownMask.load(std::memory_order_relaxed);
            if (IsMappedGameplayKeyboardCombatKey(idCode)) {
                const auto* userEvents = RE::UserEvents::GetSingleton();
                if (userEvents) {
                    if (idCode == GetGameplayMappedKey(userEvents->leftAttack, RE::INPUT_DEVICE::kKeyboard)) {
                        combatMask = down ? (combatMask | kCombatLeftBit) : (combatMask & ~kCombatLeftBit);
                    }
                    if (idCode == GetGameplayMappedKey(userEvents->rightAttack, RE::INPUT_DEVICE::kKeyboard)) {
                        combatMask = down ? (combatMask | kCombatRightBit) : (combatMask & ~kCombatRightBit);
                    }
                    _keyboardCombatDownMask.store(combatMask, std::memory_order_relaxed);
                }
            }

            auto digitalMask = _keyboardDigitalDownMask.load(std::memory_order_relaxed);
            if (IsMappedGameplayKeyboardDigitalKey(idCode)) {
                const auto* userEvents = RE::UserEvents::GetSingleton();
                if (userEvents) {
                    if (idCode == GetGameplayMappedKey(userEvents->jump, RE::INPUT_DEVICE::kKeyboard)) {
                        digitalMask = down ? (digitalMask | kDigitalJumpBit) : (digitalMask & ~kDigitalJumpBit);
                    }
                    if (idCode == GetGameplayMappedKey(userEvents->activate, RE::INPUT_DEVICE::kKeyboard)) {
                        digitalMask = down ? (digitalMask | kDigitalActivateBit) : (digitalMask & ~kDigitalActivateBit);
                    }
                    if (idCode == GetGameplayMappedKey(userEvents->sprint, RE::INPUT_DEVICE::kKeyboard)) {
                        const auto previousDigitalMask = digitalMask;
                        digitalMask = down ? (digitalMask | kDigitalSprintBit) : (digitalMask & ~kDigitalSprintBit);
                        if ((previousDigitalMask ^ digitalMask) & kDigitalSprintBit) {
                            logger::info(
                                "[DualPad][SprintProbe] KB sprint fact -> {} (keyboard)",
                                down);
                        }
                    }
                    _keyboardDigitalDownMask.store(digitalMask, std::memory_order_relaxed);
                }
            }
            return;
        }

        if (event.GetDevice() == RE::INPUT_DEVICE::kMouse) {
            const auto* userEvents = RE::UserEvents::GetSingleton();
            if (!userEvents) {
                return;
            }

            if (IsMappedGameplayMouseCombatButton(idCode)) {
                auto combatMask = _mouseCombatDownMask.load(std::memory_order_relaxed);
                if (idCode == GetGameplayMappedKey(userEvents->leftAttack, RE::INPUT_DEVICE::kMouse)) {
                    combatMask = down ? (combatMask | kCombatLeftBit) : (combatMask & ~kCombatLeftBit);
                }
                if (idCode == GetGameplayMappedKey(userEvents->rightAttack, RE::INPUT_DEVICE::kMouse)) {
                    combatMask = down ? (combatMask | kCombatRightBit) : (combatMask & ~kCombatRightBit);
                }
                _mouseCombatDownMask.store(combatMask, std::memory_order_relaxed);
            }

            if (IsMappedGameplayMouseDigitalButton(idCode)) {
                auto digitalMask = _mouseDigitalDownMask.load(std::memory_order_relaxed);
                if (idCode == GetGameplayMappedKey(userEvents->jump, RE::INPUT_DEVICE::kMouse)) {
                    digitalMask = down ? (digitalMask | kDigitalJumpBit) : (digitalMask & ~kDigitalJumpBit);
                }
                if (idCode == GetGameplayMappedKey(userEvents->activate, RE::INPUT_DEVICE::kMouse)) {
                    digitalMask = down ? (digitalMask | kDigitalActivateBit) : (digitalMask & ~kDigitalActivateBit);
                }
                if (idCode == GetGameplayMappedKey(userEvents->sprint, RE::INPUT_DEVICE::kMouse)) {
                    const auto previousDigitalMask = digitalMask;
                    digitalMask = down ? (digitalMask | kDigitalSprintBit) : (digitalMask & ~kDigitalSprintBit);
                    if ((previousDigitalMask ^ digitalMask) & kDigitalSprintBit) {
                        logger::info(
                            "[DualPad][SprintProbe] KB sprint fact -> {} (mouse)",
                            down);
                    }
                }
                _mouseDigitalDownMask.store(digitalMask, std::memory_order_relaxed);
            }
        }
    }

    void GameplayKbmFactTracker::MarkMouseLookActivity()
    {
        _lastMouseLookAtMs.store(GetMonotonicMs(), std::memory_order_relaxed);
    }

    GameplayKbmFacts GameplayKbmFactTracker::GetFacts() const
    {
        GameplayKbmFacts facts{};
        facts.keyboardMoveDownMask = _keyboardMoveDownMask.load(std::memory_order_relaxed);
        facts.keyboardCombatDownMask = _keyboardCombatDownMask.load(std::memory_order_relaxed);
        facts.mouseCombatDownMask = _mouseCombatDownMask.load(std::memory_order_relaxed);
        facts.keyboardDigitalDownMask = _keyboardDigitalDownMask.load(std::memory_order_relaxed);
        facts.mouseDigitalDownMask = _mouseDigitalDownMask.load(std::memory_order_relaxed);
        facts.mouseLookActive = IsMouseLookActive();
        return facts;
    }

    bool GameplayKbmFactTracker::IsMouseLookActive() const
    {
        const auto lastAtMs = _lastMouseLookAtMs.load(std::memory_order_relaxed);
        if (lastAtMs == 0) {
            return false;
        }

        const auto nowMs = GetMonotonicMs();
        return nowMs >= lastAtMs && (nowMs - lastAtMs) <= kGameplayMouseLookActiveMs;
    }

    bool GameplayKbmFactTracker::IsSprintMappedButton(const RE::ButtonEvent& event) const
    {
        switch (event.GetDevice()) {
        case RE::INPUT_DEVICE::kKeyboard:
            return IsMappedGameplayKeyboardSprintKey(event.GetIDCode());
        case RE::INPUT_DEVICE::kMouse:
            return IsMappedGameplayMouseSprintButton(event.GetIDCode());
        default:
            return false;
        }
    }

    bool GameplayKbmFactTracker::IsMappedGameplayKeyboardMoveKey(std::uint32_t idCode) const
    {
        const auto* userEvents = RE::UserEvents::GetSingleton();
        if (!userEvents) {
            return false;
        }

        return idCode == GetGameplayMappedKey(userEvents->forward, RE::INPUT_DEVICE::kKeyboard) ||
            idCode == GetGameplayMappedKey(userEvents->back, RE::INPUT_DEVICE::kKeyboard) ||
            idCode == GetGameplayMappedKey(userEvents->strafeLeft, RE::INPUT_DEVICE::kKeyboard) ||
            idCode == GetGameplayMappedKey(userEvents->strafeRight, RE::INPUT_DEVICE::kKeyboard);
    }

    bool GameplayKbmFactTracker::IsMappedGameplayKeyboardCombatKey(std::uint32_t idCode) const
    {
        const auto* userEvents = RE::UserEvents::GetSingleton();
        if (!userEvents) {
            return false;
        }

        return idCode == GetGameplayMappedKey(userEvents->leftAttack, RE::INPUT_DEVICE::kKeyboard) ||
            idCode == GetGameplayMappedKey(userEvents->rightAttack, RE::INPUT_DEVICE::kKeyboard);
    }

    bool GameplayKbmFactTracker::IsMappedGameplayMouseCombatButton(std::uint32_t idCode) const
    {
        const auto* userEvents = RE::UserEvents::GetSingleton();
        if (!userEvents) {
            return false;
        }

        return idCode == GetGameplayMappedKey(userEvents->leftAttack, RE::INPUT_DEVICE::kMouse) ||
            idCode == GetGameplayMappedKey(userEvents->rightAttack, RE::INPUT_DEVICE::kMouse);
    }

    bool GameplayKbmFactTracker::IsMappedGameplayKeyboardDigitalKey(std::uint32_t idCode) const
    {
        const auto* userEvents = RE::UserEvents::GetSingleton();
        if (!userEvents) {
            return false;
        }

        return idCode == GetGameplayMappedKey(userEvents->jump, RE::INPUT_DEVICE::kKeyboard) ||
            idCode == GetGameplayMappedKey(userEvents->activate, RE::INPUT_DEVICE::kKeyboard) ||
            idCode == GetGameplayMappedKey(userEvents->sprint, RE::INPUT_DEVICE::kKeyboard);
    }

    bool GameplayKbmFactTracker::IsMappedGameplayMouseDigitalButton(std::uint32_t idCode) const
    {
        const auto* userEvents = RE::UserEvents::GetSingleton();
        if (!userEvents) {
            return false;
        }

        return idCode == GetGameplayMappedKey(userEvents->jump, RE::INPUT_DEVICE::kMouse) ||
            idCode == GetGameplayMappedKey(userEvents->activate, RE::INPUT_DEVICE::kMouse) ||
            idCode == GetGameplayMappedKey(userEvents->sprint, RE::INPUT_DEVICE::kMouse);
    }

    bool GameplayKbmFactTracker::IsMappedGameplayKeyboardSprintKey(std::uint32_t idCode) const
    {
        const auto* userEvents = RE::UserEvents::GetSingleton();
        if (!userEvents) {
            return false;
        }

        return idCode == GetGameplayMappedKey(userEvents->sprint, RE::INPUT_DEVICE::kKeyboard);
    }

    bool GameplayKbmFactTracker::IsMappedGameplayMouseSprintButton(std::uint32_t idCode) const
    {
        const auto* userEvents = RE::UserEvents::GetSingleton();
        if (!userEvents) {
            return false;
        }

        return idCode == GetGameplayMappedKey(userEvents->sprint, RE::INPUT_DEVICE::kMouse);
    }
}
