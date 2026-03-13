#pragma once

#include "input/injection/PadEventSnapshot.h"

#include <array>
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
        static constexpr std::size_t kPendingSnapshotCapacity = 256;

        PadEventSnapshotDispatcher() = default;
        void ScheduleDrainTask();

        std::array<PadEventSnapshot, kPendingSnapshotCapacity> _pending{};
        std::size_t _pendingHead{ 0 };
        std::size_t _pendingCount{ 0 };
        std::uint64_t _droppedSnapshots{ 0 };
        std::mutex _mutex;
        std::atomic_bool _drainTaskQueued{ false };
        std::atomic_bool _framePumpEnabled{ false };
    };
}
