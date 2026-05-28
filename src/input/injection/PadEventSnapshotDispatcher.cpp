#include "pch.h"
#include "input/injection/PadEventSnapshotDispatcher.h"

#include "input_v2/compat/LegacyInputContextCompat.h"
#include "input/RuntimeConfig.h"
#include "input/injection/PadEventSnapshotProcessor.h"
#include "input/injection/UpstreamGamepadHook.h"
#include "input_v2/context/ContextRefreshTick.h"
#include "input_v2/ingress/FrameAssembler.h"
#include "input_v2/ingress/IngressHub.h"
#include "input_v2/telemetry/InputTraceRecorder.h"

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

        DrainTelemetryContext BuildDrainTelemetryContext(DrainReason reason, std::uint64_t stalePollWindowMs)
        {
            auto& upstreamHook = UpstreamGamepadHook::GetSingleton();
            const auto lastPollAgeMs = upstreamHook.GetLastPollCallAgeMs();
            return DrainTelemetryContext{
                .reason = reason,
                .routeState = ResolveUpstreamRouteState(
                    upstreamHook.IsRouteActive(),
                    lastPollAgeMs,
                    stalePollWindowMs),
                .lastPollAgeMs = lastPollAgeMs,
                .hookInstalled = upstreamHook.IsInstalled()
            };
        }

        std::string FormatLastPollAgeMs(const DrainTelemetryContext& telemetryContext)
        {
            return telemetryContext.lastPollAgeMs ? std::to_string(*telemetryContext.lastPollAgeMs) : "none";
        }

        void LogDrainTelemetry(
            const DrainTelemetryContext& telemetryContext,
            std::size_t budget,
            std::size_t drained,
            std::size_t pendingBefore,
            std::size_t pendingAfter)
        {
            input_v2::telemetry::InputTraceRecorder::GetSingleton().RecordDispatcherDrain(
                telemetryContext,
                budget,
                drained,
                pendingBefore,
                pendingAfter);

            if (!RuntimeConfig::GetSingleton().LogRouteHealth()) {
                return;
            }

            if (telemetryContext.reason == DrainReason::UpstreamPoll &&
                drained == 0 &&
                pendingBefore == 0) {
                return;
            }

            logger::info(
                "[DualPad][RouteHealth] drain reason={} routeState={} lastPollAgeMs={} hookInstalled={} budget={} drained={} pendingBefore={} pendingAfter={}",
                ToString(telemetryContext.reason),
                ToString(telemetryContext.routeState),
                FormatLastPollAgeMs(telemetryContext),
                telemetryContext.hookInstalled,
                budget,
                drained,
                pendingBefore,
                pendingAfter);
        }

        input_v2::ingress::FrameAssembler& RuntimeFrameAssembler()
        {
            static input_v2::ingress::FrameAssembler assembler;
            return assembler;
        }
    }

    PadEventSnapshotDispatcher& PadEventSnapshotDispatcher::GetSingleton()
    {
        static PadEventSnapshotDispatcher instance;
        return instance;
    }

    void PadEventSnapshotDispatcher::SubmitSnapshot(const PadEventSnapshot& snapshot)
    {
        auto& hub = input_v2::ingress::IngressHub::GetSingleton();
        const auto pendingCountBeforeQueue = hub.PendingLegacySnapshotCount();
        (void)hub.PushPadSnapshot(snapshot);
        const auto pendingCountAfterQueue = hub.PendingLegacySnapshotCount();

        input_v2::telemetry::InputTraceRecorder::GetSingleton().RecordDispatcherSubmit(
            snapshot,
            pendingCountBeforeQueue,
            pendingCountAfterQueue);

        const auto framePumpEnabled = _framePumpEnabled.load(std::memory_order_acquire);
        const bool shouldScheduleTask =
            framePumpEnabled &&
            pendingCountAfterQueue >= kUpstreamTaskFallbackHighWatermark &&
            !_replayManualDrainActive.load(std::memory_order_acquire) &&
            !_drainTaskQueued.exchange(true, std::memory_order_acq_rel);

        if (shouldScheduleTask) {
            ScheduleDrainTask();
            const auto telemetry = BuildDrainTelemetryContext(
                DrainReason::TaskFallbackHighWater,
                kUpstreamTaskFallbackPollStaleMs);
            logger::warn(
                "[DualPad][IngressHub] Scheduled high-water fallback drain task pending={} threshold={} stalePollWindowMs={} routeState={} lastPollAgeMs={} hookInstalled={}",
                pendingCountAfterQueue,
                kUpstreamTaskFallbackHighWatermark,
                kUpstreamTaskFallbackPollStaleMs,
                ToString(telemetry.routeState),
                FormatLastPollAgeMs(telemetry),
                telemetry.hookInstalled);
        }
    }

    void PadEventSnapshotDispatcher::SubmitReset()
    {
        PadEventSnapshot snapshot{};
        snapshot.type = PadEventSnapshotType::Reset;
        SubmitSnapshot(snapshot);
    }

    std::size_t PadEventSnapshotDispatcher::DrainOnMainThread(
        std::size_t maxSnapshots,
        const DrainTelemetryContext* telemetryContext)
    {
        if (maxSnapshots == 0) {
            return 0;
        }

        auto& contextRefresh = input_v2::context::ContextRefreshTick::GetSingleton();
        contextRefresh.RefreshOnMainThread(contextRefresh.BeginFrame());

        auto& hub = input_v2::ingress::IngressHub::GetSingleton();
        const auto pendingBefore = hub.PendingLegacySnapshotCount();
        auto events = hub.Drain();
        auto frames = RuntimeFrameAssembler().Assemble(events);
        for (const auto& frame : frames) {
            PadEventSnapshotProcessor::GetSingleton().ProcessIngressFrame(frame);
        }

        const auto processedCount = frames.size();
        const auto pendingAfterDrain = hub.PendingCount();
        if (pendingAfterDrain == 0) {
            _drainTaskQueued.store(false, std::memory_order_release);
        }

        if (telemetryContext) {
            LogDrainTelemetry(*telemetryContext, maxSnapshots, processedCount, pendingBefore, pendingAfterDrain);
        }

        return processedCount;
    }

    std::size_t PadEventSnapshotDispatcher::DrainForReplay(
        std::size_t maxSnapshots,
        const DrainTelemetryContext* telemetryContext,
        ReplayDrainSink sink,
        void* context)
    {
        if (maxSnapshots == 0 || sink == nullptr) {
            return 0;
        }

        auto& hub = input_v2::ingress::IngressHub::GetSingleton();
        const auto pendingBefore = hub.PendingLegacySnapshotCount();
        auto events = hub.Drain();
        const auto frames = RuntimeFrameAssembler().Assemble(events);
        std::size_t processedCount = 0;
        (void)sink;
        (void)context;
        for (const auto& frame : frames) {
            if (frame.kind == input_v2::ingress::AssembledFrameKind::Stable &&
                frame.facts.legacySnapshot) {
                ++processedCount;
            }
            PadEventSnapshotProcessor::GetSingleton().ProcessIngressFrame(frame);
        }
        const auto pendingAfterDrain = hub.PendingCount();
        if (pendingAfterDrain == 0) {
            _drainTaskQueued.store(false, std::memory_order_release);
        }

        if (telemetryContext) {
            LogDrainTelemetry(*telemetryContext, maxSnapshots, processedCount, pendingBefore, pendingAfterDrain);
        }

        return processedCount;
    }

    void PadEventSnapshotDispatcher::ResetForReplay()
    {
        std::scoped_lock lock(_mutex);
        _pending = {};
        _pendingHead = 0;
        _pendingCount = 0;
        _droppedSnapshots = 0;
        input_v2::ingress::IngressHub::GetSingleton().ResetForTests();
        RuntimeFrameAssembler().Reset();
        _drainTaskQueued.store(false, std::memory_order_release);
        _framePumpEnabled.store(false, std::memory_order_release);
        _replayManualDrainActive.store(true, std::memory_order_release);
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
            auto& dispatcher = PadEventSnapshotDispatcher::GetSingleton();
            const auto telemetry = BuildDrainTelemetryContext(
                dispatcher.IsFramePumpEnabled() ? DrainReason::TaskFallbackHighWater : DrainReason::FramePumpDisabled,
                kUpstreamTaskFallbackPollStaleMs);
            dispatcher.DrainOnMainThread(kTaskDrainBudget, &telemetry);
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
