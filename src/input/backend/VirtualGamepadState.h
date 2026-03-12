#pragma once

#include <cstdint>

namespace dualpad::input::backend
{
    enum class VirtualGamepadAxis : std::uint8_t
    {
        MoveX = 0,
        MoveY,
        LookX,
        LookY,
        LeftTrigger,
        RightTrigger
    };

    enum class VirtualGamepadStick : std::uint8_t
    {
        Move = 0,
        Look
    };

    struct VirtualGamepadState
    {
        std::uint64_t pollIndex{ 0 };
        std::uint32_t packetNumber{ 0 };
        std::uint32_t previousDownMask{ 0 };
        std::uint32_t downMask{ 0 };
        std::uint32_t pressedMask{ 0 };
        std::uint32_t releasedMask{ 0 };
        std::uint16_t previousRawButtons{ 0 };
        std::uint16_t rawButtons{ 0 };

        float moveX{ 0.0f };
        float moveY{ 0.0f };
        float lookX{ 0.0f };
        float lookY{ 0.0f };
        float leftTrigger{ 0.0f };
        float rightTrigger{ 0.0f };

        void Reset()
        {
            pollIndex = 0;
            packetNumber = 0;
            previousDownMask = 0;
            downMask = 0;
            pressedMask = 0;
            releasedMask = 0;
            previousRawButtons = 0;
            rawButtons = 0;
            moveX = 0.0f;
            moveY = 0.0f;
            lookX = 0.0f;
            lookY = 0.0f;
            leftTrigger = 0.0f;
            rightTrigger = 0.0f;
        }

        void SetCommittedDigitalState(
            std::uint64_t nextPollIndex,
            std::uint32_t nextPacketNumber,
            std::uint32_t nextDownMask,
            std::uint16_t nextRawButtons)
        {
            pollIndex = nextPollIndex;
            packetNumber = nextPacketNumber;
            previousDownMask = downMask;
            previousRawButtons = rawButtons;
            downMask = nextDownMask;
            rawButtons = nextRawButtons;
            pressedMask = downMask & ~previousDownMask;
            releasedMask = previousDownMask & ~downMask;
        }

        void SetAxis(VirtualGamepadAxis axis, float value)
        {
            switch (axis) {
            case VirtualGamepadAxis::MoveX:
                moveX = value;
                break;
            case VirtualGamepadAxis::MoveY:
                moveY = value;
                break;
            case VirtualGamepadAxis::LookX:
                lookX = value;
                break;
            case VirtualGamepadAxis::LookY:
                lookY = value;
                break;
            case VirtualGamepadAxis::LeftTrigger:
                leftTrigger = value;
                break;
            case VirtualGamepadAxis::RightTrigger:
                rightTrigger = value;
                break;
            }
        }

        void SetStick(VirtualGamepadStick stick, float x, float y)
        {
            switch (stick) {
            case VirtualGamepadStick::Move:
                moveX = x;
                moveY = y;
                break;
            case VirtualGamepadStick::Look:
                lookX = x;
                lookY = y;
                break;
            }
        }
    };
}
