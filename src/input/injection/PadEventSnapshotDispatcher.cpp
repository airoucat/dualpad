#include "pch.h"
#include "input/injection/PadEventSnapshotDispatcher.h"

#include "input/InputContext.h"
#include "input/injection/PadEventSnapshotProcessor.h"

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        void MergeSnapshots(PadEventSnapshot& pending, const PadEventSnapshot& incoming)
        {
            pending.type = PadEventSnapshotType::Input;
            pending.sequence = incoming.sequence;
            pending.sourceTimestampUs = incoming.sourceTimestampUs;
            pending.state = incoming.state;
            pending.compatFrame = incoming.compatFrame;
            pending.coalesced = true;

            pending.overflowed = pending.overflowed ||
                incoming.overflowed ||
                incoming.events.overflowed;

            for (std::size_t i = 0; i < incoming.events.count; ++i) {
                if (!pending.events.Push(incoming.events[i])) {
                    pending.overflowed = true;
                }
            }

            if (incoming.events.overflowed) {
                pending.events.overflowed = true;
                pending.events.droppedCount += incoming.events.droppedCount;
            }
        }
    }

    PadEventSnapshotDispatcher& PadEventSnapshotDispatcher::GetSingleton()
    {
        static PadEventSnapshotDispatcher instance;
        return instance;
    }

    void PadEventSnapshotDispatcher::SubmitSnapshot(const PadEventSnapshot& snapshot)
    {
        bool shouldSchedule = false;
        {
            std::scoped_lock lock(_mutex);

            if (snapshot.type == PadEventSnapshotType::Reset) {
                _pending = snapshot;
                _hasPending = true;
            }
            else if (!_hasPending || _pending.type == PadEventSnapshotType::Reset) {
                _pending = snapshot;
                _pending.firstSequence = snapshot.sequence;
                _pending.coalesced = false;
                _pending.overflowed = snapshot.overflowed || snapshot.events.overflowed;
                _hasPending = true;
            }
            else {
                // Transitional coalescing patch: keep the latest state and merge per-packet
                // events until the formal BSInputDeviceManager/Main::Update hook replaces the
                // current task-driven main-thread consumer.
                MergeSnapshots(_pending, snapshot);
            }

            if (!_drainTaskQueued.exchange(true, std::memory_order_acq_rel)) {
                shouldSchedule = true;
            }
        }

        if (shouldSchedule && !_framePumpEnabled.load(std::memory_order_acquire)) {
            ScheduleDrainTask();
        }
    }

    void PadEventSnapshotDispatcher::SubmitReset()
    {
        PadEventSnapshot snapshot{};
        snapshot.type = PadEventSnapshotType::Reset;
        SubmitSnapshot(snapshot);
    }

    void PadEventSnapshotDispatcher::DrainOnMainThread()
    {
        for (;;) {
            PadEventSnapshot snapshot{};
            {
                std::scoped_lock lock(_mutex);
                if (!_hasPending) {
                    _drainTaskQueued.store(false, std::memory_order_release);
                    return;
                }

                snapshot = _pending;
                _pending = {};
                _hasPending = false;
            }

            ContextManager::GetSingleton().UpdateGameplayContext();
            PadEventSnapshotProcessor::GetSingleton().Process(snapshot);
        }
    }

    void PadEventSnapshotDispatcher::SetFramePumpEnabled(bool enabled)
    {
        _framePumpEnabled.store(enabled, std::memory_order_release);
    }

    bool PadEventSnapshotDispatcher::IsFramePumpEnabled() const
    {
        return _framePumpEnabled.load(std::memory_order_acquire);
    }

    void PadEventSnapshotDispatcher::ScheduleDrainTask()
    {
        auto* taskInterface = SKSE::GetTaskInterface();
        if (!taskInterface) {
            logger::warn("[DualPad][Snapshot] Failed to get TaskInterface for main-thread drain");
            _drainTaskQueued.store(false, std::memory_order_release);
            return;
        }

        taskInterface->AddTask([]() {
            PadEventSnapshotDispatcher::GetSingleton().DrainOnMainThread();
            });
    }
}
