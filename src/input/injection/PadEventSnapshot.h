#pragma once

#include "input/legacy/InputCompatBridge.h"
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
        PadState state{};
        CompatFrame compatFrame{};
        PadEventBuffer events{};
        bool overflowed{ false };
        bool coalesced{ false };
    };
}
