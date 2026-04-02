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
        std::size_t DrainOnMainThread(std::size_t maxSnapshots = kDefaultDrainBudget);
        void SetFramePumpEnabled(bool enabled);
        bool IsFramePumpEnabled() const;

    private:
        static constexpr std::size_t kPendingSnapshotCapacity = 256;
        static constexpr std::size_t kDefaultDrainBudget = 16;
        static constexpr std::size_t kTaskDrainBudget = 64;
        static constexpr std::size_t kUpstreamTaskFallbackHighWatermark = 128;
        static constexpr std::uint64_t kUpstreamTaskFallbackPollStaleMs = 250;

        PadEventSnapshotDispatcher() = default;
        void ScheduleDrainTask();
        bool HasResetInPendingLocked() const;
        bool HasCrossContextPendingLocked() const;
        void CoalescePendingLocked();

        std::array<PadEventSnapshot, kPendingSnapshotCapacity> _pending{};
        std::size_t _pendingHead{ 0 };
        std::size_t _pendingCount{ 0 };
        std::uint64_t _droppedSnapshots{ 0 };
        std::mutex _mutex;
        std::atomic_bool _drainTaskQueued{ false };
        std::atomic_bool _framePumpEnabled{ false };
    };
}
