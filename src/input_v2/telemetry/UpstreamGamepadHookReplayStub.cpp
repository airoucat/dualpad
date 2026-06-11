#include "pch.h"
#include "input/injection/UpstreamGamepadHook.h"

#include <Windows.h>

#include <optional>

namespace dualpad::input
{
    namespace
    {
        std::optional<UpstreamRouteInstallSnapshot> g_forcedSnapshot;
    }

    UpstreamGamepadHook& UpstreamGamepadHook::GetSingleton()
    {
        static UpstreamGamepadHook instance;
        return instance;
    }

    void UpstreamGamepadHook::Install()
    {
    }

    bool UpstreamGamepadHook::IsInstalled() const
    {
        return true;
    }

    bool UpstreamGamepadHook::IsRouteActive() const
    {
        return true;
    }

    UpstreamGamepadHookInstallStatus UpstreamGamepadHook::GetInstallStatus() const
    {
        return UpstreamGamepadHookInstallStatus::Installed;
    }

    std::string_view UpstreamGamepadHook::GetInstallDebugReason() const
    {
        return "test_upstream_hook_installed";
    }

    bool UpstreamGamepadHook::HasInstallFailed() const
    {
        return false;
    }

    bool UpstreamGamepadHook::WasInstallAttempted() const
    {
        return true;
    }

    void UpstreamGamepadHook::NotePollCallActivity()
    {
        _lastPollCallTickMs.store(::GetTickCount64(), std::memory_order_relaxed);
    }

    std::optional<std::uint64_t> UpstreamGamepadHook::GetLastPollCallAgeMs() const
    {
        const auto last = _lastPollCallTickMs.load(std::memory_order_relaxed);
        if (last == 0) {
            return std::nullopt;
        }
        const auto now = ::GetTickCount64();
        return now >= last ? std::optional<std::uint64_t>{ now - last } : std::nullopt;
    }

    bool UpstreamGamepadHook::HasRecentPollCallActivity(std::uint64_t maxAgeMs) const
    {
        const auto age = GetLastPollCallAgeMs();
        return age && *age <= maxAgeMs;
    }

    void UpstreamGamepadHook::SetInstallStatus(
        UpstreamGamepadHookInstallStatus,
        std::string_view)
    {
    }

    UpstreamRouteInstallSnapshot GetUpstreamRouteInstallSnapshot()
    {
        if (g_forcedSnapshot) {
            return *g_forcedSnapshot;
        }
        return UpstreamRouteInstallSnapshot{
            .configured = true,
            .installAttempted = true,
            .installed = true,
            .failed = false,
            .status = UpstreamGamepadHookInstallStatus::Installed,
            .debugReason = "test_upstream_hook_installed"
        };
    }

    namespace detail
    {
        void ForceUpstreamRouteInstallSnapshotForTests(const UpstreamRouteInstallSnapshot& snapshot)
        {
            g_forcedSnapshot = snapshot;
        }

        void ResetUpstreamRouteInstallSnapshotForTests()
        {
            g_forcedSnapshot.reset();
        }
    }
}
