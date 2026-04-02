#include "pch.h"
#include "input/injection/PadEventSnapshotDispatcher.h"

#include "input/InputContext.h"
#include "input/RuntimeConfig.h"
#include "input/injection/PadEventSnapshotProcessor.h"
#include "input/injection/UpstreamGamepadHook.h"

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        bool ShouldPrioritizeInCoalesce(const PadEvent& event)
        {
            return event.type != PadEventType::AxisChange;
        }

        bool MatchesSnapshotContext(const PadEventSnapshot& lhs, const PadEventSnapshot& rhs)
        {
            return lhs.context == rhs.context && lhs.contextEpoch == rhs.contextEpoch;
        }

        void AppendCoalescedEvents(
            PadEventBuffer& destination,
            const PadEventSnapshot& source,
            bool prioritizedPass)
        {
            for (std::size_t eventIndex = 0; eventIndex < source.events.count; ++eventIndex) {
                const auto& event = source.events[eventIndex];
                if (ShouldPrioritizeInCoalesce(event) != prioritizedPass) {
                    continue;
                }

                destination.Push(event);
            }
        }

        bool ShouldForceTaskFallback(
            bool framePumpEnabled,
            std::size_t pendingCount,
            std::size_t highWatermark,
            std::uint64_t stalePollWindowMs)
        {
            if (!framePumpEnabled) {
                return true;
            }

            if (pendingCount < highWatermark) {
                return false;
            }

            auto& upstreamHook = UpstreamGamepadHook::GetSingleton();
            if (!upstreamHook.IsRouteActive()) {
                return false;
            }

            return !upstreamHook.HasRecentPollCallActivity(stalePollWindowMs);
        }
    }

    PadEventSnapshotDispatcher& PadEventSnapshotDispatcher::GetSingleton()
    {
        static PadEventSnapshotDispatcher instance;
        return instance;
    }

    void PadEventSnapshotDispatcher::SubmitSnapshot(const PadEventSnapshot& snapshot)
    {
        bool shouldScheduleTask = false;
        bool scheduledByHighWaterFallback = false;
        bool forcedCrossContextProbe = false;
        std::size_t pendingCountAfterQueue = 0;
        bool framePumpEnabled = false;
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
                queuedSnapshot.crossContextMismatch = false;
                queuedSnapshot.overflowed = snapshot.overflowed || snapshot.events.overflowed;

                const auto tail = (_pendingHead + _pendingCount) % _pending.size();
                _pending[tail] = queuedSnapshot;
                ++_pendingCount;
            }

            if (RuntimeConfig::GetSingleton().EnableForceCrossContextRecoveryProbe() &&
                _pendingCount > 1 &&
                !HasResetInPendingLocked() &&
                HasCrossContextPendingLocked()) {
                CoalescePendingLocked();
                forcedCrossContextProbe = true;
            }

            pendingCountAfterQueue = _pendingCount;
            framePumpEnabled = _framePumpEnabled.load(std::memory_order_acquire);
            const bool forceTaskFallback = ShouldForceTaskFallback(
                framePumpEnabled,
                pendingCountAfterQueue,
                kUpstreamTaskFallbackHighWatermark,
                kUpstreamTaskFallbackPollStaleMs);
            scheduledByHighWaterFallback = forceTaskFallback && framePumpEnabled;
            if (forceTaskFallback &&
                !_drainTaskQueued.exchange(true, std::memory_order_acq_rel)) {
                shouldScheduleTask = true;
            }
        }

        if (shouldScheduleTask) {
            ScheduleDrainTask();
            if (scheduledByHighWaterFallback) {
                logger::warn(
                    "[DualPad][Snapshot] Scheduled high-water fallback drain task pending={} threshold={} stalePollWindowMs={}",
                    pendingCountAfterQueue,
                    kUpstreamTaskFallbackHighWatermark,
                    kUpstreamTaskFallbackPollStaleMs);
            }
        }

        if (forcedCrossContextProbe) {
            logger::warn(
                "[DualPad][Snapshot] Forced cross-context recovery probe pending={} routeFramePumpEnabled={}",
                pendingCountAfterQueue,
                framePumpEnabled);
        }
    }

    void PadEventSnapshotDispatcher::SubmitReset()
    {
        PadEventSnapshot snapshot{};
        snapshot.type = PadEventSnapshotType::Reset;
        SubmitSnapshot(snapshot);
    }

    std::size_t PadEventSnapshotDispatcher::DrainOnMainThread(std::size_t maxSnapshots)
    {
        if (maxSnapshots == 0) {
            return 0;
        }

        std::size_t processedCount = 0;
        for (; processedCount < maxSnapshots; ++processedCount) {
            PadEventSnapshot snapshot{};
            std::uint64_t droppedSnapshots = 0;
            {
                std::scoped_lock lock(_mutex);
                if (_pendingCount == 0) {
                    _drainTaskQueued.store(false, std::memory_order_release);
                    return processedCount;
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

        bool scheduleFollowUpTask = false;
        bool scheduledByHighWaterFallback = false;
        std::size_t pendingAfterDrain = 0;
        {
            std::scoped_lock lock(_mutex);
            if (_pendingCount > 1 && !HasResetInPendingLocked()) {
                CoalescePendingLocked();
            }

            pendingAfterDrain = _pendingCount;
            if (_pendingCount == 0) {
                _drainTaskQueued.store(false, std::memory_order_release);
            } else {
                const auto framePumpEnabled = _framePumpEnabled.load(std::memory_order_acquire);
                const bool forceTaskFallback = ShouldForceTaskFallback(
                    framePumpEnabled,
                    _pendingCount,
                    kUpstreamTaskFallbackHighWatermark,
                    kUpstreamTaskFallbackPollStaleMs);
                scheduledByHighWaterFallback = forceTaskFallback && framePumpEnabled;
                if (forceTaskFallback) {
                    if (!_drainTaskQueued.exchange(true, std::memory_order_acq_rel)) {
                        scheduleFollowUpTask = true;
                    }
                }
            }

            if (!scheduleFollowUpTask &&
                _pendingCount != 0 &&
                !_framePumpEnabled.load(std::memory_order_acquire)) {
                scheduleFollowUpTask = true;
            }
        }

        if (scheduleFollowUpTask) {
            ScheduleDrainTask();
            if (scheduledByHighWaterFallback) {
                logger::warn(
                    "[DualPad][Snapshot] Queued high-water follow-up drain task pending={} threshold={} stalePollWindowMs={}",
                    pendingAfterDrain,
                    kUpstreamTaskFallbackHighWatermark,
                    kUpstreamTaskFallbackPollStaleMs);
            }
        }

        return processedCount;
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
            PadEventSnapshotDispatcher::GetSingleton().DrainOnMainThread(kTaskDrainBudget);
            });
    }

    bool PadEventSnapshotDispatcher::HasResetInPendingLocked() const
    {
        for (std::size_t i = 0; i < _pendingCount; ++i) {
            const auto index = (_pendingHead + i) % _pending.size();
            if (_pending[index].type == PadEventSnapshotType::Reset) {
                return true;
            }
        }

        return false;
    }

    bool PadEventSnapshotDispatcher::HasCrossContextPendingLocked() const
    {
        if (_pendingCount <= 1) {
            return false;
        }

        bool haveBaseline = false;
        InputContext baselineContext = InputContext::Gameplay;
        std::uint32_t baselineEpoch = 0;

        for (std::size_t i = 0; i < _pendingCount; ++i) {
            const auto index = (_pendingHead + i) % _pending.size();
            const auto& snapshot = _pending[index];
            if (snapshot.type == PadEventSnapshotType::Reset) {
                continue;
            }

            if (!haveBaseline) {
                baselineContext = snapshot.context;
                baselineEpoch = snapshot.contextEpoch;
                haveBaseline = true;
                continue;
            }

            if (snapshot.context != baselineContext ||
                snapshot.contextEpoch != baselineEpoch) {
                return true;
            }
        }

        return false;
    }

    void PadEventSnapshotDispatcher::CoalescePendingLocked()
    {
        if (_pendingCount <= 1) {
            return;
        }

        const auto lastIndex = (_pendingHead + _pendingCount - 1) % _pending.size();

        auto coalescedSnapshot = _pending[lastIndex];
        coalescedSnapshot.coalesced = true;
        coalescedSnapshot.crossContextMismatch = false;
        coalescedSnapshot.events.Clear();

        bool capturedFirstSequence = false;
        bool sawContextMismatch = false;

        for (std::size_t i = 0; i < _pendingCount; ++i) {
            const auto index = (_pendingHead + i) % _pending.size();
            const auto& pendingSnapshot = _pending[index];
            if (!MatchesSnapshotContext(pendingSnapshot, coalescedSnapshot)) {
                sawContextMismatch = true;
                continue;
            }

            if (!capturedFirstSequence) {
                coalescedSnapshot.firstSequence = pendingSnapshot.firstSequence;
                capturedFirstSequence = true;
            }

            coalescedSnapshot.overflowed =
                coalescedSnapshot.overflowed ||
                pendingSnapshot.overflowed ||
                pendingSnapshot.events.overflowed ||
                pendingSnapshot.coalesced;
        }

        for (bool prioritizedPass : { true, false }) {
            for (std::size_t i = 0; i < _pendingCount; ++i) {
                const auto index = (_pendingHead + i) % _pending.size();
                const auto& pendingSnapshot = _pending[index];
                if (!MatchesSnapshotContext(pendingSnapshot, coalescedSnapshot)) {
                    continue;
                }

                AppendCoalescedEvents(coalescedSnapshot.events, pendingSnapshot, prioritizedPass);
            }
        }

        // Cross-context mismatch is a degraded-delivery signal, but it should
        // not automatically escalate into "hard overflow". Processor already
        // receives `crossContextMismatch` separately and can route it to the
        // CrossContextBoundary recovery path.
        coalescedSnapshot.overflowed =
            coalescedSnapshot.overflowed ||
            coalescedSnapshot.events.overflowed;
        coalescedSnapshot.crossContextMismatch = sawContextMismatch;

        _pendingHead = 0;
        _pending[0] = coalescedSnapshot;
        for (std::size_t i = 1; i < _pending.size(); ++i) {
            _pending[i] = {};
        }
        _pendingCount = 1;

        logger::warn(
            "[DualPad][Snapshot] Coalesced pending snapshots after bounded drain; retaining latest seq={} firstSeq={} context={} epoch={} mismatchedContexts={} mergedEvents={} overflowed={}",
            coalescedSnapshot.sequence,
            coalescedSnapshot.firstSequence,
            ToString(coalescedSnapshot.context),
            coalescedSnapshot.contextEpoch,
            sawContextMismatch,
            coalescedSnapshot.events.count,
            coalescedSnapshot.overflowed);
    }
}
