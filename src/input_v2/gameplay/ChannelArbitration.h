#pragma once

#include <cstdint>

namespace dualpad::input_v2::gameplay
{
    enum class ChannelOwner : std::uint8_t
    {
        Gamepad = 0,
        KeyboardMouse = 1,
        None = 2
    };

    static_assert(static_cast<std::uint8_t>(ChannelOwner::Gamepad) == 0);
    static_assert(static_cast<std::uint8_t>(ChannelOwner::KeyboardMouse) == 1);
    static_assert(static_cast<std::uint8_t>(ChannelOwner::None) == 2);

    enum class GameplayReasonCode : std::uint8_t
    {
        None = 0,
        NonGameplayContext,
        CarryPreviousOwner,
        MouseLookActive,
        MeaningfulRightStick,
        KeyboardMoveActive,
        MeaningfulLeftStick,
        KeyboardMouseCombatActive,
        MeaningfulTrigger,
        KeyboardMouseTransientDigitalActive,
        GamepadTransientDigitalActive,
        SoftResync,
        HardReset
    };

    enum class ChannelArbitrationResetMode : std::uint8_t
    {
        None = 0,
        GamepadSource,
        Global
    };

    struct ChannelArbitrationState
    {
        ChannelOwner owner{ ChannelOwner::None };
        bool gamepadCandidate{ false };
        std::uint64_t lastKeyboardMouseActivityUs{ 0 };
    };

    struct ChannelArbitrationInput
    {
        ChannelArbitrationState previous{};
        bool gameplayContext{ true };
        bool keyboardMouseActive{ false };
        bool keyboardMouseActivation{ false };
        float gamepadMagnitude{ 0.0f };
        float gamepadEnterThreshold{ 0.25f };
        float gamepadSustainThreshold{ 0.15f };
        std::uint64_t nowUs{ 0 };
        std::uint64_t lastKeyboardMouseActivityUs{ 0 };
        std::uint64_t keyboardMouseQuietWindowUs{ 0 };
        GameplayReasonCode keyboardMouseReason{ GameplayReasonCode::None };
        GameplayReasonCode gamepadReason{ GameplayReasonCode::None };
        ChannelArbitrationResetMode resetMode{ ChannelArbitrationResetMode::None };
    };

    struct ChannelArbitrationDecision
    {
        ChannelOwner owner{ ChannelOwner::None };
        bool gateGamepad{ true };
        GameplayReasonCode reason{ GameplayReasonCode::None };
        ChannelArbitrationState next{};
    };

    struct ChannelArbitrationStateSet
    {
        ChannelArbitrationState look{};
        ChannelArbitrationState move{};
        ChannelArbitrationState combat{};
        ChannelArbitrationState digital{};
    };

    ChannelArbitrationDecision ResolveChannelArbitration(const ChannelArbitrationInput& input) noexcept;
}
