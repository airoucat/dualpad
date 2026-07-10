#include "pch.h"

#include "input/injection/RouteHealthContract.h"
#include "input/injection/PollDiagnostics.h"

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

    void TestInstallStatusFailureMapping()
    {
        for (const auto status : {
                 dualpad::input::UpstreamGamepadHookInstallStatus::UnsupportedMode,
                 dualpad::input::UpstreamGamepadHookInstallStatus::UnsupportedRuntime,
                 dualpad::input::UpstreamGamepadHookInstallStatus::SignatureMismatch,
                 dualpad::input::UpstreamGamepadHookInstallStatus::PatchFailed }) {
            Require(
                dualpad::input::HasUpstreamGamepadHookInstallFailed(status),
                "failure status should be classified as failed");
            Require(
                dualpad::input::WasUpstreamGamepadHookInstallAttempted(status),
                "failure status should be classified as attempted");
        }

        for (const auto status : {
                 dualpad::input::UpstreamGamepadHookInstallStatus::NotAttempted,
                 dualpad::input::UpstreamGamepadHookInstallStatus::DisabledByConfig,
                 dualpad::input::UpstreamGamepadHookInstallStatus::Installed,
                 dualpad::input::UpstreamGamepadHookInstallStatus::AlreadyInstalled }) {
            Require(
                !dualpad::input::HasUpstreamGamepadHookInstallFailed(status),
                "non-failure status should not be classified as failed");
        }

        Require(
            !dualpad::input::WasUpstreamGamepadHookInstallAttempted(
                dualpad::input::UpstreamGamepadHookInstallStatus::DisabledByConfig),
            "disabled upstream config should not be classified as an install attempt");
    }

    void TestControlMapOverlayGate()
    {
        Require(
            !dualpad::input::ShouldApplyControlMapOverlay(
                true,
                dualpad::input::UpstreamGamepadHookInstallStatus::SignatureMismatch),
            "configured upstream route with signature mismatch should skip controlmap overlay");
        Require(
            !dualpad::input::ShouldApplyControlMapOverlay(
                true,
                dualpad::input::UpstreamGamepadHookInstallStatus::UnsupportedRuntime),
            "configured upstream route with unsupported runtime should skip controlmap overlay");
        Require(
            dualpad::input::ShouldApplyControlMapOverlay(
                false,
                dualpad::input::UpstreamGamepadHookInstallStatus::SignatureMismatch),
            "unconfigured upstream route should not block controlmap overlay");
        Require(
            dualpad::input::ShouldApplyControlMapOverlay(
                true,
                dualpad::input::UpstreamGamepadHookInstallStatus::DisabledByConfig),
            "disabled upstream config should not block controlmap overlay");
    }

    void TestInstallStatusLabels()
    {
        Require(
            std::string_view(dualpad::input::ToString(
                dualpad::input::UpstreamGamepadHookInstallStatus::SignatureMismatch)) == "signature_mismatch",
            "signature mismatch install status label should stay stable");
        Require(
            std::string_view(dualpad::input::ToString(
                dualpad::input::UpstreamGamepadHookInstallStatus::UnsupportedRuntime)) == "unsupported_runtime",
            "unsupported runtime install status label should stay stable");
        Require(
            std::string_view(dualpad::input::ToString(
                dualpad::input::UpstreamGamepadHookInstallStatus::DisabledByConfig)) == "disabled_by_config",
            "disabled-by-config install status label should stay stable");
    }

    void TestPollDiagnosticLimiter()
    {
        dualpad::input::PollDiagnosticLimiter limiter(2);

        const auto disabled = limiter.Begin(false);
        Require(!disabled.record, "disabled Poll diagnostics must not reserve a record");
        Require(disabled.sequence == 0, "disabled Poll diagnostics must not advance sequence");
        Require(limiter.InFlight() == 0, "disabled Poll diagnostics must not affect in-flight count");

        const auto first = limiter.Begin(true);
        const auto second = limiter.Begin(true);
        const auto overflow = limiter.Begin(true);
        Require(first.record && first.sequence == 1 && first.inFlight == 1, "first Poll diagnostic must be recorded");
        Require(second.record && second.sequence == 2 && second.inFlight == 2, "second Poll diagnostic must be recorded");
        Require(!overflow.record && overflow.sequence == 3 && overflow.inFlight == 3, "capacity overflow must remain observable without recording");
        Require(limiter.Dropped() == 1, "capacity overflow must increment dropped count");

        Require(limiter.End(true) == 2, "first Poll completion must decrement in-flight count");
        Require(limiter.End(true) == 1, "second Poll completion must decrement in-flight count");
        Require(limiter.End(true) == 0, "final Poll completion must clear in-flight count");
    }

    void TestTaskFallbackTruthTable()
    {
        using dualpad::input::ShouldScheduleTaskFallback;
        using dualpad::input::UpstreamRouteState;

        Require(
            ShouldScheduleTaskFallback(false, false, 1, 128, UpstreamRouteState::Disabled),
            "disabled frame pump must schedule the only available fallback consumer");
        Require(
            !ShouldScheduleTaskFallback(true, false, 127, 128, UpstreamRouteState::ActiveStale),
            "pending below high-water must not schedule task fallback");
        Require(
            !ShouldScheduleTaskFallback(true, false, 128, 128, UpstreamRouteState::ActiveFresh),
            "active fresh upstream route must not gain a second consumer at high-water");
        Require(
            ShouldScheduleTaskFallback(true, false, 128, 128, UpstreamRouteState::ActiveStale),
            "active stale upstream route must schedule high-water recovery");
        Require(
            !ShouldScheduleTaskFallback(true, false, 128, 128, UpstreamRouteState::Disabled),
            "registered frame pump owns disabled or missing upstream routes");
        Require(
            !ShouldScheduleTaskFallback(true, true, 128, 128, UpstreamRouteState::ActiveStale),
            "manual replay drain must never schedule an asynchronous consumer");
    }
}

int main()
{
    TestActiveFreshRouteState();
    TestActiveStaleRouteState();
    TestDisabledRouteStateWins();
    TestTelemetryLabels();
    TestInstallStatusFailureMapping();
    TestControlMapOverlayGate();
    TestInstallStatusLabels();
    TestPollDiagnosticLimiter();
    TestTaskFallbackTruthTable();
    return 0;
}
