#pragma once

#include <atomic>
#include <cstdint>

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
        void NotePollCallActivity();
        bool HasRecentPollCallActivity(std::uint64_t maxAgeMs = 250) const;

    private:
        UpstreamGamepadHook() = default;

        bool _attemptedInstall{ false };
        bool _installed{ false };
        bool _loggedUnsupportedRuntime{ false };
        std::atomic<std::uint64_t> _lastPollCallTickMs{ 0 };
    };
}
