#include "pch.h"
#include "input/XInputButtonSerialization.h"

#include "input/PadProfile.h"

namespace dualpad::input
{
    std::uint16_t ToXInputButtons(std::uint32_t mask)
    {
        std::uint16_t buttons = 0;
        const auto& bits = GetPadBits(GetActivePadProfile());

        if (mask & bits.cross) {
            buttons |= 0x1000;
        }
        if (mask & bits.circle) {
            buttons |= 0x2000;
        }
        if (mask & bits.square) {
            buttons |= 0x4000;
        }
        if (mask & bits.triangle) {
            buttons |= 0x8000;
        }
        if (mask & bits.l1) {
            buttons |= 0x0100;
        }
        if (mask & bits.r1) {
            buttons |= 0x0200;
        }
        if (mask & bits.l3) {
            buttons |= 0x0040;
        }
        if (mask & bits.r3) {
            buttons |= 0x0080;
        }
        if (mask & bits.options) {
            buttons |= 0x0010;
        }
        if (mask & bits.create) {
            buttons |= 0x0020;
        }
        if (mask & bits.dpadUp) {
            buttons |= 0x0001;
        }
        if (mask & bits.dpadDown) {
            buttons |= 0x0002;
        }
        if (mask & bits.dpadLeft) {
            buttons |= 0x0004;
        }
        if (mask & bits.dpadRight) {
            buttons |= 0x0008;
        }

        return buttons;
    }
}
