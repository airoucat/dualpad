#pragma once

#include <array>
#include <cstdint>

namespace dualpad::input::backend
{
    struct DesiredKeyboardState
    {
        std::array<std::uint8_t, 256> desiredRefCounts{};
        std::array<std::uint8_t, 256> pendingPulseCounts{};
        std::array<std::uint8_t, 256> pendingTransactionalPulseCounts{};

        void Clear()
        {
            desiredRefCounts.fill(0);
            pendingPulseCounts.fill(0);
            pendingTransactionalPulseCounts.fill(0);
        }
    };
}
