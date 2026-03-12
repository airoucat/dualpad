#pragma once

namespace dualpad::input
{
    // Legacy experimental ControlMap-side probe. It remains installable only as
    // a reverse/debug fallback and is no longer the primary digital backend.
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
