#pragma once

#include "input/InputContext.h"
#include "input/mapping/PadEvent.h"

#include <array>
#include <cstdint>

namespace dualpad::input
{
    struct SyntheticButtonState
    {
        std::uint32_t code{ 0 };
        bool down{ false };
        bool pressed{ false };
        bool released{ false };
        bool held{ false };
        bool tapTriggered{ false };
        bool holdTriggered{ false };
        bool comboTriggered{ false };
        float heldSeconds{ 0.0f };
        std::uint64_t pressedAtUs{ 0 };
        std::uint64_t releasedAtUs{ 0 };
    };

    struct SyntheticAxisState
    {
        float value{ 0.0f };
        float previousValue{ 0.0f };
        bool changed{ false };
    };

    struct SyntheticPadFrame
    {
        std::uint64_t firstSequence{ 0 };
        std::uint64_t sequence{ 0 };
        std::uint64_t sourceTimestampUs{ 0 };
        InputContext context{ InputContext::Gameplay };

        std::uint32_t downMask{ 0 };
        std::uint32_t pressedMask{ 0 };
        std::uint32_t releasedMask{ 0 };
        std::uint32_t heldMask{ 0 };
        std::uint32_t pulseMask{ 0 };
        std::uint32_t tapMask{ 0 };
        std::uint32_t holdMask{ 0 };
        std::uint32_t comboMask{ 0 };

        std::array<SyntheticButtonState, 32> buttons{};

        SyntheticAxisState leftStickX{};
        SyntheticAxisState leftStickY{};
        SyntheticAxisState rightStickX{};
        SyntheticAxisState rightStickY{};
        SyntheticAxisState leftTrigger{};
        SyntheticAxisState rightTrigger{};

        std::size_t gestureCount{ 0 };
        std::size_t touchpadPressCount{ 0 };
        std::size_t touchpadReleaseCount{ 0 };
        std::size_t touchpadSlideCount{ 0 };

        bool overflowed{ false };
        bool coalesced{ false };
    };
}
