#include "pch.h"
#include "input_v2/menu/UiMenuObserver.h"

#include <RE/U/UI.h>

namespace dualpad::input_v2::menu
{
    UiMenuObserver& UiMenuObserver::GetSingleton()
    {
        static UiMenuObserver instance;
        return instance;
    }

    void UiMenuObserver::MarkMenuEvent(std::string_view, bool)
    {
        std::scoped_lock lock(_mutex);
        _dirty = true;
        ++_eventSequence;
    }

    bool UiMenuObserver::IsDirty() const
    {
        std::scoped_lock lock(_mutex);
        return _dirty;
    }

    void UiMenuObserver::ClearDirty()
    {
        std::scoped_lock lock(_mutex);
        _dirty = false;
    }

    ObservedMenuSnapshot UiMenuObserver::Capture()
    {
        ObservedMenuSnapshot snapshot{};
        {
            std::scoped_lock lock(_mutex);
            snapshot.eventSequence = _eventSequence;
        }

        auto* ui = RE::UI::GetSingleton();
        if (!ui) {
            snapshot.completeness = ObserverCompleteness::Unavailable;
            return snapshot;
        }

        std::uint32_t order = 0;
        for (const auto& menu : ui->menuStack) {
            if (!menu) {
                snapshot.completeness = ObserverCompleteness::Partial;
                continue;
            }

            std::string menuName;
#if defined(EXCLUSIVE_SKYRIM_VR)
            menuName = menu->GetVRRuntimeData().menuName.c_str();
#else
            // Skyrim SE's IMenu does not expose the registered name on the instance.
            // Match the stack pointer back to UI::menuMap so the observer remains stack-authoritative.
            for (const auto& [name, entry] : ui->menuMap) {
                if (entry.menu.get() == menu.get()) {
                    menuName = name.c_str();
                    break;
                }
            }
#endif
            if (menuName.empty()) {
                snapshot.completeness = ObserverCompleteness::Partial;
                continue;
            }

            snapshot.nodes.push_back(ObservedMenuNode{
                .menuPtr = reinterpret_cast<std::uintptr_t>(menu.get()),
                .menuName = std::move(menuName),
                .menuFlagsValue = menu->menuFlags.underlying(),
                .inputContextValue = menu->inputContext.underlying(),
                .depthPriority = static_cast<std::int32_t>(menu->depthPriority),
                .delegatePtr = reinterpret_cast<std::uintptr_t>(menu->fxDelegate.get()),
                .moviePtr = reinterpret_cast<std::uintptr_t>(menu->uiMovie.get()),
                .observationOrder = order++
            });
        }

        return snapshot;
    }

    void UiMenuObserver::Publish(ObservedMenuSnapshot snapshot)
    {
        std::scoped_lock lock(_mutex);
        _published = std::move(snapshot);
        _dirty = false;
    }

    ObservedMenuSnapshot UiMenuObserver::GetPublishedSnapshot() const
    {
        std::scoped_lock lock(_mutex);
        return _published;
    }
}
