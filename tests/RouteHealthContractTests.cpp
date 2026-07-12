#include "pch.h"

#include "input/injection/RouteHealthContract.h"
#include "input/injection/HookPatchTransaction.h"
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
                 dualpad::input::UpstreamGamepadHookInstallStatus::PatchFailed,
                 dualpad::input::UpstreamGamepadHookInstallStatus::PatchRolledBack,
                 dualpad::input::UpstreamGamepadHookInstallStatus::UnsafePartial }) {
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

        Require(
            dualpad::input::ResolveUpstreamHookOperationalState(
                dualpad::input::UpstreamGamepadHookInstallStatus::PatchRolledBack) ==
                dualpad::input::patching::HookOperationalState::SafePassthrough,
            "rolled-back patch must expose safe passthrough reality");
        Require(
            dualpad::input::ResolveUpstreamHookFailureDisposition(
                dualpad::input::UpstreamGamepadHookInstallStatus::PatchRolledBack) ==
                dualpad::input::patching::HookFailureDisposition::RolledBack,
            "rolled-back patch must expose rollback disposition");
        Require(
            dualpad::input::ResolveUpstreamHookOperationalState(
                dualpad::input::UpstreamGamepadHookInstallStatus::UnsafePartial) ==
                dualpad::input::patching::HookOperationalState::UnsafePartial &&
                dualpad::input::ResolveUpstreamHookFailureDisposition(
                    dualpad::input::UpstreamGamepadHookInstallStatus::UnsafePartial) ==
                    dualpad::input::patching::HookFailureDisposition::FailClosed,
            "unsafe partial patch must be explicitly fail-closed");
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

    void TestPatchTransactionRollback()
    {
        using namespace dualpad::input::patching;

        std::array<Bytes, 2> preflightMemory{ Bytes{ 1 }, Bytes{ 99 } };
        std::size_t preflightWrites = 0;
        std::vector<PatchSite> preflightSites;
        for (std::size_t index = 0; index < preflightMemory.size(); ++index) {
            preflightSites.push_back(PatchSite{
                .name = "preflight_" + std::to_string(index),
                .original = Bytes{ static_cast<std::uint8_t>(index + 1) },
                .replacement = Bytes{ static_cast<std::uint8_t>(index + 11) },
                .read = [&, index]() { return preflightMemory[index]; },
                .compareWrite = [&, index](const Bytes& expected, const Bytes& desired) {
                    ++preflightWrites;
                    if (preflightMemory[index] != expected) {
                        return false;
                    }
                    preflightMemory[index] = desired;
                    return true;
                }
            });
        }
        const auto preflightFailure = ExecutePatchTransaction(preflightSites);
        Require(
            preflightFailure.outcome == PatchTransactionOutcome::FailedNoWrite &&
                preflightWrites == 0 &&
                preflightMemory[0] == Bytes{ 1 },
            "every site must pass exact preflight before the transaction writes its first site");

        const auto runFailureAt = [](std::size_t failureIndex, bool tamperRollback) {
            std::array<Bytes, 3> memory{ Bytes{ 1 }, Bytes{ 2 }, Bytes{ 3 } };
            const std::array<Bytes, 3> replacements{ Bytes{ 11 }, Bytes{ 12 }, Bytes{ 13 } };
            std::vector<PatchSite> sites;
            for (std::size_t index = 0; index < memory.size(); ++index) {
                sites.push_back(PatchSite{
                    .name = "site_" + std::to_string(index),
                    .original = memory[index],
                    .replacement = replacements[index],
                    .read = [&, index]() { return memory[index]; },
                    .compareWrite = [&, index](const Bytes& expected, const Bytes& desired) {
                        if (memory[index] != expected) {
                            return false;
                        }
                        if (desired == replacements[index] && index == failureIndex) {
                            return false;
                        }
                        if (tamperRollback && index == 0 && desired == Bytes{ 1 }) {
                            memory[index] = Bytes{ 99 };
                            return false;
                        }
                        memory[index] = desired;
                        return true;
                    }
                });
            }
            return std::pair{ ExecutePatchTransaction(sites), memory };
        };

        const auto [secondFailure, afterSecondFailure] = runFailureAt(1, false);
        Require(
            secondFailure.outcome == PatchTransactionOutcome::RolledBack,
            "second-site failure must roll back the first exact patch");
        Require(
            afterSecondFailure == std::array<Bytes, 3>{ Bytes{ 1 }, Bytes{ 2 }, Bytes{ 3 } },
            "second-site failure must restore all original bytes");

        const auto [thirdFailure, afterThirdFailure] = runFailureAt(2, false);
        Require(
            thirdFailure.outcome == PatchTransactionOutcome::RolledBack,
            "third-site failure must roll back both prior exact patches");
        Require(
            afterThirdFailure == std::array<Bytes, 3>{ Bytes{ 1 }, Bytes{ 2 }, Bytes{ 3 } },
            "third-site failure must restore branch bytes and vfunc value");

        const auto [tamperedRollback, afterTamperedRollback] = runFailureAt(1, true);
        Require(
            tamperedRollback.outcome == PatchTransactionOutcome::UnsafePartial,
            "expected-current mismatch during rollback must enter unsafe partial state");
        Require(
            afterTamperedRollback[0] == Bytes{ 99 },
            "transaction must refuse to overwrite an externally changed patch site");

        std::array<Bytes, 2> throwMemory{ Bytes{ 1 }, Bytes{ 2 } };
        const std::array<Bytes, 2> throwReplacements{ Bytes{ 11 }, Bytes{ 12 } };
        std::vector<PatchSite> throwingSites;
        for (std::size_t index = 0; index < throwMemory.size(); ++index) {
            throwingSites.push_back(PatchSite{
                .name = "throw_site_" + std::to_string(index),
                .original = throwMemory[index],
                .replacement = throwReplacements[index],
                .read = [&, index]() { return throwMemory[index]; },
                .compareWrite = [&, index](const Bytes& expected, const Bytes& desired) {
                    if (throwMemory[index] != expected) {
                        return false;
                    }
                    throwMemory[index] = desired;
                    if (index == 1 && desired == throwReplacements[index]) {
                        throw std::runtime_error("injected post-write exception");
                    }
                    return true;
                }
            });
        }
        const auto throwingResult = ExecutePatchTransaction(throwingSites);
        Require(
            throwingResult.outcome == PatchTransactionOutcome::RolledBack &&
                throwMemory == std::array<Bytes, 2>{ Bytes{ 1 }, Bytes{ 2 } },
            "post-write exception must detect patch reality and roll back every applied site");
    }

    void TestPatchEncodingContracts()
    {
        using namespace dualpad::input::patching;

        const auto call = MakeRelativePatch(0x1000, 0x1800, RelativePatchOpcode::Call, 5);
        Require(call.size() == 5 && call.front() == 0xE8, "call patch must encode one exact rel32 call");
        Require(
            DecodeRelativeTarget(0x1000, call, RelativePatchOpcode::Call) == std::optional<std::uintptr_t>{ 0x1800 },
            "saved original call displacement must decode to its exact target");

        const auto entryBranch = MakeRelativePatch(0x2000, 0x2800, RelativePatchOpcode::Jump, 8);
        Require(entryBranch.size() == 8 && entryBranch.front() == 0xE9, "entry patch must encode a rel32 jump");
        Require(
            entryBranch[5] == 0x90 && entryBranch[6] == 0x90 && entryBranch[7] == 0x90,
            "entry patch must cover only complete instructions and pad the remainder with NOPs");

        const auto absoluteJump = MakeAbsoluteJump(0x123456789ABCDEF0ull);
        Require(
            absoluteJump.size() == 14 && absoluteJump[0] == 0xFF && absoluteJump[1] == 0x25,
            "trampoline stub must encode an absolute RIP-relative jump");
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

    void TestI0AvailabilitySampler()
    {
        using dualpad::input::I0AvailabilitySampleReason;
        dualpad::input::I0AvailabilitySampler sampler(5'000);

        const auto first = sampler.Observe(100, 0xAA);
        const auto unchanged = sampler.Observe(200, 0xAA);
        const auto changed = sampler.Observe(300, 0xBB);
        const auto beforeInterval = sampler.Observe(5'299, 0xBB);
        const auto interval = sampler.Observe(5'300, 0xBB);
        const auto afterInterval = sampler.Observe(6'000, 0xBB);
        const auto regressed = sampler.Observe(5'900, 0xBB);

        Require(first.record && first.pollCount == 1 &&
                first.reason == I0AvailabilitySampleReason::First,
            "I-0 availability telemetry must record the first native Poll call");
        Require(!unchanged.record && unchanged.pollCount == 2,
            "unchanged high-rate Poll calls must not flood the log");
        Require(changed.record && changed.pollCount == 3 &&
                changed.reason == I0AvailabilitySampleReason::StateChanged,
            "state or context fingerprint changes must be recorded immediately");
        Require(!beforeInterval.record && beforeInterval.pollCount == 4,
            "held current-state must wait for the full health interval");
        Require(interval.record && interval.pollCount == 5 &&
                interval.reason == I0AvailabilitySampleReason::IntervalElapsed,
            "a 30-second held case must retain periodic evidence after the initial change");
        Require(!afterInterval.record && afterInterval.pollCount == 6,
            "an unchanged sample inside the next interval must remain quiet");
        Require(regressed.record && regressed.pollCount == 7 &&
                regressed.reason == I0AvailabilitySampleReason::ClockRegressed,
            "clock regression between unrecorded Poll calls must remain explicit");

        const dualpad::input::I0AvailabilityFingerprintInput base{
            .xinputResult = 0,
            .currentStateClass = 10,
            .gamepadSessionId = 20,
            .contextRevision = 30,
            .menuStackRevision = 40,
            .context = 50,
            .routeHealth = 1,
            .remapMode = false,
            .connected = true,
            .delegateReady = true
        };
        const auto baseFingerprint = dualpad::input::BuildI0AvailabilityFingerprint(base);
        auto changedInput = base;
        ++changedInput.currentStateClass;
        Require(baseFingerprint != dualpad::input::BuildI0AvailabilityFingerprint(changedInput),
            "I-0 fingerprint must change with XInput semantic current-state class");
        changedInput = base;
        ++changedInput.contextRevision;
        Require(baseFingerprint != dualpad::input::BuildI0AvailabilityFingerprint(changedInput),
            "I-0 fingerprint must change at an owner-resolved context boundary");
        changedInput = base;
        ++changedInput.gamepadSessionId;
        Require(baseFingerprint != dualpad::input::BuildI0AvailabilityFingerprint(changedInput),
            "I-0 fingerprint must change across gamepad sessions");
        changedInput = base;
        ++changedInput.xinputResult;
        Require(baseFingerprint != dualpad::input::BuildI0AvailabilityFingerprint(changedInput),
            "I-0 fingerprint must expose an XInput availability result change");
        changedInput = base;
        changedInput.remapMode = true;
        Require(baseFingerprint != dualpad::input::BuildI0AvailabilityFingerprint(changedInput),
            "I-0 fingerprint must record remap entry and exit as separate native availability cases");
        changedInput = base;
        changedInput.connected = false;
        Require(baseFingerprint != dualpad::input::BuildI0AvailabilityFingerprint(changedInput),
            "I-0 fingerprint must record physical connectivity transitions");
        changedInput = base;
        changedInput.delegateReady = false;
        Require(baseFingerprint != dualpad::input::BuildI0AvailabilityFingerprint(changedInput),
            "I-0 fingerprint must record Skyrim delegate readiness transitions");
        Require(std::string_view(dualpad::input::ToString(
                    I0AvailabilitySampleReason::StateChanged)) == "state_changed",
            "I-0 sample reason must be stable for live evidence parsing");

        Require(dualpad::input::BuildI0AvailabilityStateClass(0, 0, 0, 0, 0, 0, 0) == 0,
            "neutral current-state must retain a zero availability class");
        const auto stateClass = dualpad::input::BuildI0AvailabilityStateClass(
            0x1000, 1, 0, 0, -1, 1, 2);
        Require((stateClass & 0xFFFF) == 0x1000 &&
                (stateClass & (1U << 16)) != 0 &&
                (stateClass & (1U << 17)) != 0 &&
                (stateClass & (1U << 18)) != 0 &&
                (stateClass & (1U << 19)) != 0,
            "availability class must distinguish buttons, LS, RS, LT and RT without hashing analog noise");
    }

    void TestTaskFallbackTruthTable()
    {
        using dualpad::input::ShouldScheduleTaskFallback;
        using dualpad::input::UpstreamRouteState;

        Require(
            ShouldScheduleTaskFallback(false, false, 1, false, 128, UpstreamRouteState::Disabled),
            "disabled frame pump must schedule the only available fallback consumer");
        Require(
            ShouldScheduleTaskFallback(false, false, 0, true, 128, UpstreamRouteState::Disabled),
            "disabled frame pump must schedule uncaptured latest work without inventing an event");
        Require(
            !ShouldScheduleTaskFallback(false, false, 0, false, 128, UpstreamRouteState::Disabled),
            "disabled frame pump must remain idle when neither events nor latest work are pending");
        Require(
            !ShouldScheduleTaskFallback(true, false, 127, true, 128, UpstreamRouteState::ActiveStale),
            "pending below high-water must not schedule task fallback");
        Require(
            !ShouldScheduleTaskFallback(true, false, 128, false, 128, UpstreamRouteState::ActiveFresh),
            "active fresh upstream route must not gain a second consumer at high-water");
        Require(
            ShouldScheduleTaskFallback(true, false, 128, false, 128, UpstreamRouteState::ActiveStale),
            "active stale upstream route must schedule high-water recovery");
        Require(
            !ShouldScheduleTaskFallback(true, false, 128, false, 128, UpstreamRouteState::Disabled),
            "registered frame pump owns disabled or missing upstream routes");
        Require(
            !ShouldScheduleTaskFallback(true, true, 128, true, 128, UpstreamRouteState::ActiveStale),
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
    TestPatchTransactionRollback();
    TestPatchEncodingContracts();
    TestPollDiagnosticLimiter();
    TestI0AvailabilitySampler();
    TestTaskFallbackTruthTable();
    return 0;
}
