#pragma once

#include <RE/Skyrim.h>

namespace dualpad::input
{
    class NativeInputConsumerHook
    {
    public:
        static NativeInputConsumerHook& GetSingleton();

        void Install();
        bool IsInstalled() const;

    private:
        NativeInputConsumerHook() = default;

        bool _playerControlsInstalled{ false };
        bool _menuControlsInstalled{ false };
    };
}
