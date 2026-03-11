#pragma once

#include "input/state/PadState.h"

#include <cstdint>

namespace dualpad::input
{
    struct PadSnapshot
    {
        PadState previous{};
        PadState current{};
        std::uint32_t previousMask{ 0 };
        std::uint32_t currentMask{ 0 };
        std::uint32_t pressedMask{ 0 };
        std::uint32_t releasedMask{ 0 };
    };

    inline PadSnapshot MakePadSnapshot(const PadState& previous, const PadState& current)
    {
        PadSnapshot snapshot{};
        snapshot.previous = previous;
        snapshot.current = current;
        snapshot.previousMask = previous.buttons.digitalMask;
        snapshot.currentMask = current.buttons.digitalMask;
        snapshot.pressedMask = snapshot.currentMask & ~snapshot.previousMask;
        snapshot.releasedMask = snapshot.previousMask & ~snapshot.currentMask;
        return snapshot;
    }
}
