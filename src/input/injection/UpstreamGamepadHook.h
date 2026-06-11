#pragma once

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "input/injection/RouteHealthContract.h"
#include "input/RuntimeConfig.h"

namespace dualpad::input
{
    class UpstreamGamepadHook
    {
    public:
        static UpstreamGamepadHook& GetSingleton();

        void Install();
        bool IsInstalled() const;
        bool IsRouteActive() const;
        UpstreamGamepadHookInstallStatus GetInstallStatus() const;
        std::string_view GetInstallDebugReason() const;
        bool HasInstallFailed() const;
        bool WasInstallAttempted() const;
        void NotePollCallActivity();
        std::optional<std::uint64_t> GetLastPollCallAgeMs() const;
        bool HasRecentPollCallActivity(std::uint64_t maxAgeMs = 250) const;

    private:
        UpstreamGamepadHook() = default;

        void SetInstallStatus(UpstreamGamepadHookInstallStatus status, std::string_view debugReason);

        bool _attemptedInstall{ false };
        bool _installed{ false };
        bool _loggedUnsupportedRuntime{ false };
        UpstreamGamepadHookInstallStatus _installStatus{ UpstreamGamepadHookInstallStatus::NotAttempted };
        std::string _installDebugReason{ "not_attempted" };
        std::atomic<std::uint64_t> _lastPollCallTickMs{ 0 };
    };
}
