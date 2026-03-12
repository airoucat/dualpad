#pragma once

#include <atomic>
#include <cstdint>

namespace dualpad::input
{
    class OrbisGamepadProbeHook
    {
    public:
        static OrbisGamepadProbeHook& GetSingleton();

        void Install();
        [[nodiscard]] bool IsInstalled() const;
        void NoteProviderCallActivity();
        [[nodiscard]] bool HasRecentProviderCallActivity(std::uint64_t maxAgeMs = 250) const;

    private:
        OrbisGamepadProbeHook() = default;

        bool _attemptedInstall{ false };
        bool _installed{ false };
        bool _loggedUnsupportedRuntime{ false };
        std::atomic<std::uint64_t> _lastProviderCallTickMs{ 0 };
    };
}
