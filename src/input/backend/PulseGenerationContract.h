#pragma once

#include <cstdint>

namespace dualpad::input::backend
{
    enum class PulseBoundaryReason : std::uint8_t
    {
        None = 0,
        RecoveryReset,
        Overflow,
        DeviceDisconnected,
        RouteUnavailable
    };

    struct PulseGenerationRecord
    {
        std::uint32_t tokenId{ 0 };
        std::uint32_t epoch{ 0 };
        std::uint64_t downGeneration{ 0 };
        std::uint64_t upGeneration{ 0 };
        PulseBoundaryReason boundaryReason{ PulseBoundaryReason::None };
        bool cancelled{ false };
    };
}
