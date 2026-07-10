#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "input/injection/HookPatchTransaction.h"

namespace dualpad::input
{
    enum class UpstreamGamepadHookInstallStatus : std::uint8_t
    {
        NotAttempted = 0,
        DisabledByConfig,
        UnsupportedMode,
        UnsupportedRuntime,
        SignatureMismatch,
        PatchFailed,
        PatchRolledBack,
        UnsafePartial,
        Installed,
        AlreadyInstalled
    };

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
        FramePumpDisabled,
        RuntimeOwnerInputPump
    };

    struct DrainTelemetryContext
    {
        DrainReason reason{ DrainReason::FramePumpDisabled };
        UpstreamRouteState routeState{ UpstreamRouteState::Disabled };
        std::optional<std::uint64_t> lastPollAgeMs{};
        bool hookInstalled{ false };
    };

    struct UpstreamRouteInstallSnapshot
    {
        bool configured{ false };
        bool installAttempted{ false };
        bool installed{ false };
        bool failed{ false };
        UpstreamGamepadHookInstallStatus status{ UpstreamGamepadHookInstallStatus::NotAttempted };
        patching::HookOperationalState operationalState{ patching::HookOperationalState::Disabled };
        patching::HookFailureDisposition disposition{ patching::HookFailureDisposition::None };
        std::string debugReason;
    };

    constexpr bool ShouldScheduleTaskFallback(
        bool framePumpEnabled,
        bool replayManualDrainActive,
        std::size_t pendingEvents,
        bool hasUncapturedLatest,
        std::size_t highWatermarkEvents,
        UpstreamRouteState routeState) noexcept
    {
        if (replayManualDrainActive) {
            return false;
        }
        if (!framePumpEnabled) {
            return pendingEvents != 0 || hasUncapturedLatest;
        }
        if (pendingEvents < highWatermarkEvents) {
            return false;
        }
        return routeState == UpstreamRouteState::ActiveStale;
    }

    constexpr bool HasUpstreamGamepadHookInstallFailed(UpstreamGamepadHookInstallStatus status) noexcept
    {
        switch (status) {
        case UpstreamGamepadHookInstallStatus::UnsupportedMode:
        case UpstreamGamepadHookInstallStatus::UnsupportedRuntime:
        case UpstreamGamepadHookInstallStatus::SignatureMismatch:
        case UpstreamGamepadHookInstallStatus::PatchFailed:
        case UpstreamGamepadHookInstallStatus::PatchRolledBack:
        case UpstreamGamepadHookInstallStatus::UnsafePartial:
            return true;
        case UpstreamGamepadHookInstallStatus::NotAttempted:
        case UpstreamGamepadHookInstallStatus::DisabledByConfig:
        case UpstreamGamepadHookInstallStatus::Installed:
        case UpstreamGamepadHookInstallStatus::AlreadyInstalled:
        default:
            return false;
        }
    }

    constexpr bool WasUpstreamGamepadHookInstallAttempted(UpstreamGamepadHookInstallStatus status) noexcept
    {
        switch (status) {
        case UpstreamGamepadHookInstallStatus::UnsupportedMode:
        case UpstreamGamepadHookInstallStatus::UnsupportedRuntime:
        case UpstreamGamepadHookInstallStatus::SignatureMismatch:
        case UpstreamGamepadHookInstallStatus::PatchFailed:
        case UpstreamGamepadHookInstallStatus::PatchRolledBack:
        case UpstreamGamepadHookInstallStatus::UnsafePartial:
        case UpstreamGamepadHookInstallStatus::Installed:
        case UpstreamGamepadHookInstallStatus::AlreadyInstalled:
            return true;
        case UpstreamGamepadHookInstallStatus::NotAttempted:
        case UpstreamGamepadHookInstallStatus::DisabledByConfig:
        default:
            return false;
        }
    }

    constexpr bool ShouldApplyControlMapOverlay(
        bool upstreamConfigured,
        UpstreamGamepadHookInstallStatus status) noexcept
    {
        return !upstreamConfigured || !HasUpstreamGamepadHookInstallFailed(status);
    }

    constexpr patching::HookOperationalState ResolveUpstreamHookOperationalState(
        UpstreamGamepadHookInstallStatus status) noexcept
    {
        switch (status) {
        case UpstreamGamepadHookInstallStatus::Installed:
        case UpstreamGamepadHookInstallStatus::AlreadyInstalled:
            return patching::HookOperationalState::Installed;
        case UpstreamGamepadHookInstallStatus::UnsafePartial:
            return patching::HookOperationalState::UnsafePartial;
        case UpstreamGamepadHookInstallStatus::UnsupportedMode:
        case UpstreamGamepadHookInstallStatus::UnsupportedRuntime:
        case UpstreamGamepadHookInstallStatus::SignatureMismatch:
        case UpstreamGamepadHookInstallStatus::PatchFailed:
        case UpstreamGamepadHookInstallStatus::PatchRolledBack:
            return patching::HookOperationalState::SafePassthrough;
        case UpstreamGamepadHookInstallStatus::NotAttempted:
        case UpstreamGamepadHookInstallStatus::DisabledByConfig:
        default:
            return patching::HookOperationalState::Disabled;
        }
    }

    constexpr patching::HookFailureDisposition ResolveUpstreamHookFailureDisposition(
        UpstreamGamepadHookInstallStatus status) noexcept
    {
        switch (status) {
        case UpstreamGamepadHookInstallStatus::PatchRolledBack:
            return patching::HookFailureDisposition::RolledBack;
        case UpstreamGamepadHookInstallStatus::UnsafePartial:
            return patching::HookFailureDisposition::FailClosed;
        case UpstreamGamepadHookInstallStatus::UnsupportedMode:
        case UpstreamGamepadHookInstallStatus::UnsupportedRuntime:
        case UpstreamGamepadHookInstallStatus::SignatureMismatch:
        case UpstreamGamepadHookInstallStatus::PatchFailed:
            return patching::HookFailureDisposition::NotRequired;
        case UpstreamGamepadHookInstallStatus::NotAttempted:
        case UpstreamGamepadHookInstallStatus::DisabledByConfig:
        case UpstreamGamepadHookInstallStatus::Installed:
        case UpstreamGamepadHookInstallStatus::AlreadyInstalled:
        default:
            return patching::HookFailureDisposition::None;
        }
    }

    UpstreamRouteState ResolveUpstreamRouteState(
        bool routeActive,
        std::optional<std::uint64_t> lastPollAgeMs,
        std::uint64_t staleWindowMs);

    UpstreamRouteInstallSnapshot GetUpstreamRouteInstallSnapshot();

    const char* ToString(UpstreamRouteState state);
    const char* ToString(DrainReason reason);
    const char* ToString(UpstreamGamepadHookInstallStatus status);

    namespace detail
    {
        void ForceUpstreamRouteInstallSnapshotForTests(const UpstreamRouteInstallSnapshot& snapshot);
        void ResetUpstreamRouteInstallSnapshotForTests();
    }
}
