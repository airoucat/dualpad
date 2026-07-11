#include "pch.h"
#include "input_v2/menu/UiMenuObserver.h"

#include <RE/U/UI.h>

namespace logger = SKSE::log;

namespace dualpad::input_v2::menu
{
    UiMenuObserver& UiMenuObserver::GetSingleton()
    {
        static UiMenuObserver instance;
        return instance;
    }

    void UiMenuObserver::MarkMenuEvent(std::string_view menuName, bool opening)
    {
        std::scoped_lock lock(_mutex);
        _dirty = true;
        ++_eventSequence;
        _lastEventMenuName = menuName;
        _lastEventOpening = opening;
        _published.completeness = ObserverCompleteness::Partial;
        _published.eventSequence = _eventSequence;
        _published.lastEventMenuName = _lastEventMenuName;
        _published.lastEventOpening = _lastEventOpening;
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

    bool UiMenuObserver::QueueCaptureOnUiThread()
    {
        {
            std::scoped_lock lock(_mutex);
            if (_captureQueued) {
                return true;
            }
            _captureQueued = true;
        }

        auto* taskInterface = SKSE::GetTaskInterface();
        if (!taskInterface) {
            std::scoped_lock lock(_mutex);
            _captureQueued = false;
            logger::warn("[DualPad][UiSnapshot] event=queue_failed reason=no_skse_task_interface");
            return false;
        }

        taskInterface->AddUITask([] {
            UiMenuObserver::GetSingleton().RunQueuedCaptureOnUiThread();
        });
        return true;
    }

    ObservedMenuSnapshot UiMenuObserver::CaptureOnUiThread()
    {
        ObservedMenuSnapshot snapshot{};
        {
            std::scoped_lock lock(_mutex);
            snapshot.eventSequence = _eventSequence;
            snapshot.lastEventMenuName = _lastEventMenuName;
            snapshot.lastEventOpening = _lastEventOpening;
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

    void UiMenuObserver::RunQueuedCaptureOnUiThread()
    {
        auto snapshot = CaptureOnUiThread();
        const auto eventSequence = snapshot.eventSequence;
        const auto completeness = snapshot.completeness;
        const auto nodeCount = snapshot.nodes.size();
        const auto published = PublishCapturedSnapshot(std::move(snapshot));

        bool retry = false;
        {
            std::scoped_lock lock(_mutex);
            _captureQueued = false;
            retry = _dirty;
        }

        logger::info(
            "[DualPad][UiSnapshot] event=captured eventSeq={} completeness={} nodes={} published={} retry={}",
            eventSequence,
            static_cast<std::uint32_t>(completeness),
            nodeCount,
            published,
            retry);
        if (retry) {
            (void)QueueCaptureOnUiThread();
        }
    }

    void UiMenuObserver::Publish(ObservedMenuSnapshot snapshot)
    {
        std::scoped_lock lock(_mutex);
        _published = std::move(snapshot);
        _dirty = false;
    }

    bool UiMenuObserver::PublishCapturedSnapshot(ObservedMenuSnapshot snapshot)
    {
        std::scoped_lock lock(_mutex);
        if (snapshot.eventSequence != _eventSequence) {
            return false;
        }
        _published = std::move(snapshot);
        _dirty = false;
        return true;
    }

    ObservedMenuSnapshot UiMenuObserver::GetPublishedSnapshot() const
    {
        std::scoped_lock lock(_mutex);
        return _published;
    }

    void UiMenuObserver::ResetForTests()
    {
        std::scoped_lock lock(_mutex);
        _dirty = false;
        _eventSequence = 0;
        _lastEventMenuName.clear();
        _lastEventOpening = false;
        _captureQueued = false;
        _published = ObservedMenuSnapshot{};
    }
}
