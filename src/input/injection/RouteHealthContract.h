#pragma once

#include <cstdint>
#include <optional>

namespace dualpad::input
{
    enum class UpstreamRouteState
    {
        ActiveFresh = 0,
        ActiveStale,
        Disabled
    };

    enum class DrainReason
    {
        UpstreamPoll = 0,
        FramePumpAssistStale,
        TaskFallbackHighWater,
        FramePumpDisabled
    };

    struct DrainTelemetryContext
    {
        DrainReason reason{ DrainReason::FramePumpDisabled };
        UpstreamRouteState routeState{ UpstreamRouteState::Disabled };
        std::optional<std::uint64_t> lastPollAgeMs{};
        bool hookInstalled{ false };
    };

    UpstreamRouteState ResolveUpstreamRouteState(
        bool routeActive,
        std::optional<std::uint64_t> lastPollAgeMs,
        std::uint64_t staleWindowMs);

    const char* ToString(UpstreamRouteState state);
    const char* ToString(DrainReason reason);
}
