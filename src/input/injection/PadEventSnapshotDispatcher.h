#pragma once

#include "input/injection/PadEventSnapshot.h"
#include "input/injection/RouteHealthContract.h"

#include <array>
#include <atomic>
#include <mutex>

namespace dualpad::input_v2::presentation
{
    struct SourceEvidenceFrame;
}

namespace dualpad::input
{
    class PadEventSnapshotDispatcher
    {
    public:
        using ReplayDrainSink = void(*)(const PadEventSnapshot& snapshot, void* context);

        static PadEventSnapshotDispatcher& GetSingleton();

        void SubmitSnapshot(
            const PadEventSnapshot& snapshot,
            const input_v2::presentation::SourceEvidenceFrame* sourceEvidenceFrame = nullptr);
        void SubmitReset();
        static constexpr std::size_t DefaultDrainBudget() { return kDefaultDrainBudgetEvents; }
        std::size_t DrainOnMainThread(
            std::size_t maxEvents = kDefaultDrainBudgetEvents,
            const DrainTelemetryContext* telemetryContext = nullptr);
        std::size_t DrainForReplay(
            std::size_t maxEvents,
            const DrainTelemetryContext* telemetryContext,
            ReplayDrainSink sink,
            void* context);
        void ResetForReplay();
        void SetFramePumpEnabled(bool enabled);
        bool IsFramePumpEnabled() const;

    private:
        static constexpr std::size_t kPendingSnapshotCapacity = 256;
        static constexpr std::size_t kDefaultDrainBudgetEvents = 16;
        static constexpr std::size_t kTaskDrainBudgetEvents = 64;
        static constexpr std::size_t kUpstreamTaskFallbackHighWatermarkEvents = 128;
        static constexpr std::uint64_t kUpstreamTaskFallbackPollStaleMs = 250;

        PadEventSnapshotDispatcher() = default;
        bool TryScheduleDrainTask();
        bool ScheduleDrainTask();
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
        std::atomic_bool _replayManualDrainActive{ false };
    };
}
