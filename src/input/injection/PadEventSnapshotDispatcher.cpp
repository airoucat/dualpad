#include "pch.h"
#include "input/injection/PadEventSnapshotDispatcher.h"

#include "input/InputContext.h"
#include "input/injection/PadEventSnapshotProcessor.h"

namespace logger = SKSE::log;

namespace dualpad::input
{
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
                _pendingHead = 0;
                _pendingCount = 0;
                _droppedSnapshots = 0;
                _pending[0] = snapshot;
                _pendingCount = 1;
            }
            else {
                if (_pendingCount >= _pending.size()) {
                    _pendingHead = (_pendingHead + 1) % _pending.size();
                    --_pendingCount;
                    ++_droppedSnapshots;
                }

                auto queuedSnapshot = snapshot;
                queuedSnapshot.firstSequence = snapshot.sequence;
                queuedSnapshot.coalesced = false;
                queuedSnapshot.overflowed = snapshot.overflowed || snapshot.events.overflowed;

                const auto tail = (_pendingHead + _pendingCount) % _pending.size();
                _pending[tail] = queuedSnapshot;
                ++_pendingCount;
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
            std::uint64_t droppedSnapshots = 0;
            {
                std::scoped_lock lock(_mutex);
                if (_pendingCount == 0) {
                    _drainTaskQueued.store(false, std::memory_order_release);
                    return;
                }

                droppedSnapshots = _droppedSnapshots;
                _droppedSnapshots = 0;
                snapshot = _pending[_pendingHead];
                _pending[_pendingHead] = {};
                _pendingHead = (_pendingHead + 1) % _pending.size();
                --_pendingCount;
            }

            if (droppedSnapshots != 0) {
                logger::warn(
                    "[DualPad][Snapshot] Dropped {} pending snapshots due to dispatcher queue overflow (capacity={})",
                    droppedSnapshots,
                    kPendingSnapshotCapacity);
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
