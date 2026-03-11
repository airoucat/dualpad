#pragma once

namespace dualpad::input
{
    class NativeInputPollHook
    {
    public:
        static NativeInputPollHook& GetSingleton();

        void Install();
        bool IsInstalled() const;

    private:
        NativeInputPollHook() = default;

        bool _installed{ false };
    };
}
