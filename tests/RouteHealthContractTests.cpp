#include "pch.h"

#include "input/injection/RouteHealthContract.h"

#include <stdexcept>
#include <string_view>

namespace
{
    void Require(bool condition, std::string_view message)
    {
        if (!condition) {
            throw std::runtime_error(std::string(message));
        }
    }

    void TestActiveFreshRouteState()
    {
        const auto routeState = dualpad::input::ResolveUpstreamRouteState(true, std::uint64_t{ 12 }, 250);
        Require(routeState == dualpad::input::UpstreamRouteState::ActiveFresh, "fresh poll age should remain active_fresh");
    }

    void TestActiveStaleRouteState()
    {
        const auto routeState = dualpad::input::ResolveUpstreamRouteState(true, std::uint64_t{ 300 }, 250);
        Require(routeState == dualpad::input::UpstreamRouteState::ActiveStale, "poll age above stale window should classify as active_stale");

        const auto noPollActivity = dualpad::input::ResolveUpstreamRouteState(true, std::nullopt, 250);
        Require(noPollActivity == dualpad::input::UpstreamRouteState::ActiveStale, "missing poll activity should classify as active_stale when route is active");
    }

    void TestDisabledRouteStateWins()
    {
        const auto routeState = dualpad::input::ResolveUpstreamRouteState(false, std::uint64_t{ 12 }, 250);
        Require(routeState == dualpad::input::UpstreamRouteState::Disabled, "disabled route should not be reclassified as active");
    }

    void TestTelemetryLabels()
    {
        Require(
            std::string_view(dualpad::input::ToString(dualpad::input::UpstreamRouteState::ActiveFresh)) == "active_fresh",
            "active_fresh label should stay stable");
        Require(
            std::string_view(dualpad::input::ToString(dualpad::input::DrainReason::UpstreamPoll)) == "upstream_poll",
            "upstream_poll label should stay stable");
        Require(
            std::string_view(dualpad::input::ToString(dualpad::input::DrainReason::FramePumpAssistStale)) == "frame_pump_assist_stale",
            "frame_pump_assist_stale label should stay stable");
        Require(
            std::string_view(dualpad::input::ToString(dualpad::input::DrainReason::TaskFallbackHighWater)) == "task_fallback_high_water",
            "task_fallback_high_water label should stay stable");
        Require(
            std::string_view(dualpad::input::ToString(dualpad::input::DrainReason::FramePumpDisabled)) == "frame_pump_disabled",
            "frame_pump_disabled label should stay stable");
    }
}

int main()
{
    TestActiveFreshRouteState();
    TestActiveStaleRouteState();
    TestDisabledRouteStateWins();
    TestTelemetryLabels();
    return 0;
}
