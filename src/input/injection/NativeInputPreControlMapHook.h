#pragma once

namespace dualpad::input
{
    class NativeInputPreControlMapHook
    {
    public:
        static NativeInputPreControlMapHook& GetSingleton();

        void Install();
        bool IsInstalled() const;
        bool CanInject() const;
        bool IsGameplayInputGateOpen() const;

    private:
        NativeInputPreControlMapHook() = default;

        bool _installed{ false };
    };
}
