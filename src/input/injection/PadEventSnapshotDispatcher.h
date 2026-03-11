#pragma once

#include "input/injection/PadEventSnapshot.h"

#include <atomic>
#include <mutex>

namespace dualpad::input
{
    class PadEventSnapshotDispatcher
    {
    public:
        static PadEventSnapshotDispatcher& GetSingleton();

        void SubmitSnapshot(const PadEventSnapshot& snapshot);
        void SubmitReset();
        void DrainOnMainThread();
        void SetFramePumpEnabled(bool enabled);
        bool IsFramePumpEnabled() const;

    private:
        PadEventSnapshotDispatcher() = default;
        void ScheduleDrainTask();

        PadEventSnapshot _pending{};
        bool _hasPending{ false };
        std::mutex _mutex;
        std::atomic_bool _drainTaskQueued{ false };
        std::atomic_bool _framePumpEnabled{ false };
    };
}
