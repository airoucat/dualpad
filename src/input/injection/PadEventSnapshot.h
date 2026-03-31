#pragma once

#include "input/InputContext.h"
#include "input/mapping/PadEvent.h"
#include "input/state/PadState.h"

#include <cstdint>

namespace dualpad::input
{
    enum class PadEventSnapshotType : std::uint8_t
    {
        Input,
        Reset
    };

    struct PadEventSnapshot
    {
        PadEventSnapshotType type{ PadEventSnapshotType::Input };
        std::uint64_t firstSequence{ 0 };
        std::uint64_t sequence{ 0 };
        std::uint64_t sourceTimestampUs{ 0 };
        InputContext context{ InputContext::Gameplay };
        std::uint32_t contextEpoch{ 0 };
        PadState state{};
        PadEventBuffer events{};
        bool overflowed{ false };
        bool coalesced{ false };
        bool crossContextMismatch{ false };
    };
}
