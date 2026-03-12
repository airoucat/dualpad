#pragma once

namespace dualpad::input
{
    class GamepadFactoryXInputBypassHook
    {
    public:
        static GamepadFactoryXInputBypassHook& GetSingleton();

        void Install();
        [[nodiscard]] bool IsInstalled() const;

    private:
        GamepadFactoryXInputBypassHook() = default;

        bool _attemptedInstall{ false };
        bool _installed{ false };
        bool _loggedUnsupportedRuntime{ false };
    };
}
