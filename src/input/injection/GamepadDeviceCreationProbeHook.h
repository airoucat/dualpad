#pragma once

namespace dualpad::input
{
    class GamepadDeviceCreationProbeHook
    {
    public:
        static GamepadDeviceCreationProbeHook& GetSingleton();

        void Install();
        [[nodiscard]] bool IsInstalled() const;

    private:
        GamepadDeviceCreationProbeHook() = default;

        bool _attemptedInstall{ false };
        bool _installed{ false };
        bool _loggedUnsupportedRuntime{ false };
    };
}
