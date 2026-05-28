#include "pch.h"
#include "input/injection/RouteHealthContract.h"

namespace dualpad::input
{
    UpstreamRouteState ResolveUpstreamRouteState(
        bool routeActive,
        std::optional<std::uint64_t> lastPollAgeMs,
        std::uint64_t staleWindowMs)
    {
        if (!routeActive) {
            return UpstreamRouteState::Disabled;
        }

        if (lastPollAgeMs && *lastPollAgeMs <= staleWindowMs) {
            return UpstreamRouteState::ActiveFresh;
        }

        return UpstreamRouteState::ActiveStale;
    }

    const char* ToString(UpstreamRouteState state)
    {
        switch (state) {
        case UpstreamRouteState::ActiveFresh:
            return "active_fresh";
        case UpstreamRouteState::ActiveStale:
            return "active_stale";
        case UpstreamRouteState::Disabled:
        default:
            return "disabled";
        }
    }

    const char* ToString(DrainReason reason)
    {
        switch (reason) {
        case DrainReason::UpstreamPoll:
            return "upstream_poll";
        case DrainReason::FramePumpAssistStale:
            return "frame_pump_assist_stale";
        case DrainReason::TaskFallbackHighWater:
            return "task_fallback_high_water";
        case DrainReason::FramePumpDisabled:
        default:
            return "frame_pump_disabled";
        }
    }
}
