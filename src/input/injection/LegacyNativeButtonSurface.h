#pragma once

#include "input/injection/NativeInputInjector.h"

#include <cstddef>

namespace dualpad::input
{
    class LegacyNativeButtonSurface
    {
    public:
        static LegacyNativeButtonSurface& GetSingleton();

        void Reset();
        std::size_t GetPendingInjectedButtonCount() const;
        void DiscardPendingInjectedButtonEvents();
        void PrependInjectedInputEvents(RE::InputEvent*& head);
        std::size_t PrependInjectedInputQueueEvents(RE::InputEvent*& head, RE::InputEvent*& tail);
        void ReleaseInjectedInputEvents();
        std::size_t FlushInjectedInputQueue();

    private:
        LegacyNativeButtonSurface() = default;

        NativeInputInjector _injector{};
    };
}
