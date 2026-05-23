#include "pch.h"
#include "input/injection/UpstreamGamepadHook.h"

#include <Windows.h>

namespace dualpad::input
{
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
}
