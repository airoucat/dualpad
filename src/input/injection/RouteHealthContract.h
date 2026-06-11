#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

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
        FramePumpDisabled
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
        std::string_view debugReason{};
    };

    constexpr bool HasUpstreamGamepadHookInstallFailed(UpstreamGamepadHookInstallStatus status) noexcept
    {
        switch (status) {
        case UpstreamGamepadHookInstallStatus::UnsupportedMode:
        case UpstreamGamepadHookInstallStatus::UnsupportedRuntime:
        case UpstreamGamepadHookInstallStatus::SignatureMismatch:
        case UpstreamGamepadHookInstallStatus::PatchFailed:
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
