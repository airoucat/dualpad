#pragma once

#include <bit>
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
        std::uint32_t downMask{ 0 };
        std::uint32_t pressedMask{ 0 };
        std::uint32_t releasedMask{ 0 };

        float moveX{ 0.0f };
        float moveY{ 0.0f };
        float lookX{ 0.0f };
        float lookY{ 0.0f };
        float leftTrigger{ 0.0f };
        float rightTrigger{ 0.0f };

        void Reset()
        {
            downMask = 0;
            pressedMask = 0;
            releasedMask = 0;
            moveX = 0.0f;
            moveY = 0.0f;
            lookX = 0.0f;
            lookY = 0.0f;
            leftTrigger = 0.0f;
            rightTrigger = 0.0f;
        }

        void BeginFrame()
        {
            pressedMask = 0;
            releasedMask = 0;
        }

        void ApplyButton(std::uint32_t code, bool down)
        {
            if (code == 0 || !std::has_single_bit(code)) {
                return;
            }

            const auto wasDown = (downMask & code) != 0;
            if (down) {
                if (!wasDown) {
                    pressedMask |= code;
                    downMask |= code;
                }
                return;
            }

            if (wasDown) {
                downMask &= ~code;
                releasedMask |= code;
            }
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

        [[nodiscard]] bool IsDown(std::uint32_t code) const
        {
            return (downMask & code) != 0;
        }
    };
}
