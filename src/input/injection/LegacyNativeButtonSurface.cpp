#include "pch.h"
#include "input/injection/LegacyNativeButtonSurface.h"

namespace dualpad::input
{
    LegacyNativeButtonSurface& LegacyNativeButtonSurface::GetSingleton()
    {
        static LegacyNativeButtonSurface instance;
        return instance;
    }

    void LegacyNativeButtonSurface::Reset()
    {
        _injector.Reset();
    }

    std::size_t LegacyNativeButtonSurface::GetPendingInjectedButtonCount() const
    {
        return _injector.GetStagedButtonEventCount();
    }

    void LegacyNativeButtonSurface::DiscardPendingInjectedButtonEvents()
    {
        _injector.DiscardStagedButtonEvents();
    }

    void LegacyNativeButtonSurface::PrependInjectedInputEvents(RE::InputEvent*& head)
    {
        _injector.PrependStagedButtonEvents(head);
    }

    std::size_t LegacyNativeButtonSurface::PrependInjectedInputQueueEvents(RE::InputEvent*& head, RE::InputEvent*& tail)
    {
        return _injector.PrependStagedButtonEventsToInputQueue(head, tail);
    }

    void LegacyNativeButtonSurface::ReleaseInjectedInputEvents()
    {
        _injector.ReleaseInjectedButtonEvents();
    }

    std::size_t LegacyNativeButtonSurface::FlushInjectedInputQueue()
    {
        return _injector.FlushStagedButtonEventsToInputQueue();
    }
}
