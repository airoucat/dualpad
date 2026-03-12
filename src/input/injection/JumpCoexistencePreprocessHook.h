#pragma once

namespace dualpad::input
{
    class JumpCoexistencePreprocessHook
    {
    public:
        static JumpCoexistencePreprocessHook& GetSingleton();

        void Install();
        [[nodiscard]] bool IsInstalled() const;

    private:
        JumpCoexistencePreprocessHook() = default;

        bool _attemptedInstall{ false };
        bool _installed{ false };
    };
}
