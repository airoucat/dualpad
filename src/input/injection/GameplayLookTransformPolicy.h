#pragma once

#include <cstdint>

namespace dualpad::input
{
    enum class GameplayLookInputSource : std::uint8_t
    {
        None = 0,
        Mouse,
        Gamepad
    };

    constexpr bool ResolveGameplayLookTransformGamepadMode(
        bool originalValue,
        GameplayLookInputSource source) noexcept
    {
        switch (source) {
        case GameplayLookInputSource::Mouse:
            return false;
        case GameplayLookInputSource::Gamepad:
            return true;
        case GameplayLookInputSource::None:
        default:
            return originalValue;
        }
    }

    class GameplayLookSourceLatch
    {
    public:
        void Note(
            std::uintptr_t owner,
            GameplayLookInputSource source) noexcept
        {
            if (owner == 0 || source == GameplayLookInputSource::None) {
                Reset();
                return;
            }
            _owner = owner;
            _source = source;
        }

        [[nodiscard]] GameplayLookInputSource ConsumeFor(
            std::uintptr_t owner) noexcept
        {
            const auto result = owner != 0 && owner == _owner ?
                _source : GameplayLookInputSource::None;
            Reset();
            return result;
        }

        void Reset() noexcept
        {
            _owner = 0;
            _source = GameplayLookInputSource::None;
        }

    private:
        std::uintptr_t _owner{ 0 };
        GameplayLookInputSource _source{ GameplayLookInputSource::None };
    };
}
