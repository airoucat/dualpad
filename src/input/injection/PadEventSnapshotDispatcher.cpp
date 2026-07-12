#include "pch.h"
#include "input/injection/PadEventSnapshotDispatcher.h"

#include "input_v2/compat/LegacyInputContextCompat.h"
#include "input/AuthoritativePollState.h"
#include "input/RuntimeConfig.h"
#include "input/XInputButtonSerialization.h"
#include "input/backend/NativeButtonCommitBackend.h"
#include "input/injection/PadEventSnapshotProcessor.h"
#include "input/injection/UpstreamGamepadHook.h"
#include "input_v2/context/ContextRefreshTick.h"
#include "input_v2/config/AtomicConfigReloader.h"
#include "input_v2/gameplay/PollOutputFrame.h"
#include "input_v2/gameplay/RuntimeInputPublication.h"
#include "input_v2/ingress/FrameAssembler.h"
#include "input_v2/ingress/IngressHub.h"
#include "input_v2/presentation/SkyrimCompatibilitySurface.h"
#include "input_v2/runtime/RuntimeOwnerGuard.h"
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
            return lhs.context == rhs.context &&
                lhs.contextEpoch == rhs.contextEpoch &&
                lhs.contextRevision == rhs.contextRevision;
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

        void CommitAndPublishPollOutput(std::uint64_t runtimeGeneration)
        {
            auto& nativeBackend = backend::NativeButtonCommitBackend::GetSingleton();
            const auto committed = nativeBackend.CommitPollState(runtimeGeneration);

            const auto authoritative = AuthoritativePollState::GetSingleton().ReadSnapshot();
            const auto context = input_v2::context::ContextResolver::GetSingleton().GetPublishedSnapshot();
            const auto presentation =
                input_v2::presentation::SkyrimCompatibilitySurface::GetSingleton().GetCommittedState();
            const auto bundle =
                input_v2::config::AtomicConfigReloader::GetSingleton().GetActiveBundleSnapshot();
            const bool routeActive = nativeBackend.IsRouteActive();
            const auto runtimeInput =
                input_v2::gameplay::RuntimeInputPublication::GetSingleton().GetCommitted();
            const auto* menuControls = RE::MenuControls::GetSingleton();
            const bool remapMode = menuControls && menuControls->GetRuntimeData().remapMode;
            const auto gamepadConnection =
                input_v2::ingress::IngressHub::GetSingleton().GetGamepadConnectionSnapshot();
            const bool connected = gamepadConnection.connectivity ==
                input_v2::ingress::GamepadConnectivity::Connected;
            auto* inputManager = RE::BSInputDeviceManager::GetSingleton();
            auto* gamepadHandler = inputManager ? inputManager->GetGamepadHandler() : nullptr;
            const bool delegateReady = gamepadHandler &&
                gamepadHandler->GetRuntimeData().currentPCGamePadDelegate != nullptr;

            input_v2::gameplay::PollOutputFrame output{
                .runtimeGeneration = runtimeGeneration,
                .manifestEpoch = bundle ? bundle->manifestEpoch : 0,
                .contextRevision = context.contextRevision,
                .presentationEpoch = presentation.epoch,
                .actionEpoch = runtimeGeneration,
                .context = authoritative.context,
                .contextEpoch = authoritative.contextEpoch,
                .menuStackRevision = context.menuStackRevision,
                .sourceTimestampUs = authoritative.sourceTimestampUs,
                .inputStateEpoch = runtimeInput.inputStateEpoch,
                .gamepadSessionId = runtimeInput.gamepadSessionId,
                .controlMapRevision = runtimeInput.controlMapRevision,
                .orderedCutoffSeq = runtimeInput.orderedCutoffSeq,
                .eventBatchToken = runtimeInput.eventBatchToken,
                .buttons = routeActive ? ToXInputButtons(authoritative.downMask) : std::uint16_t{ 0 },
                .pressedMask = routeActive ? authoritative.pressedMask : 0,
                .releasedMask = routeActive ? authoritative.releasedMask : 0,
                .lx = routeActive && authoritative.hasAnalog ?
                    input_v2::gameplay::EncodePollStickAxis(authoritative.moveX) : 0,
                .ly = routeActive && authoritative.hasAnalog ?
                    input_v2::gameplay::EncodePollStickAxis(authoritative.moveY) : 0,
                .rx = routeActive && authoritative.hasAnalog ?
                    input_v2::gameplay::EncodePollStickAxis(authoritative.lookX) : 0,
                .ry = routeActive && authoritative.hasAnalog ?
                    input_v2::gameplay::EncodePollStickAxis(authoritative.lookY) : 0,
                .lt = routeActive && authoritative.hasAnalog ?
                    input_v2::gameplay::EncodePollTrigger(authoritative.leftTrigger) : std::uint8_t{ 0 },
                .rt = routeActive && authoritative.hasAnalog ?
                    input_v2::gameplay::EncodePollTrigger(authoritative.rightTrigger) : std::uint8_t{ 0 },
                .pulseToken = committed.pulse.tokenId,
                .pulseDownGeneration = committed.pulse.downGeneration,
                .pulseUpGeneration = committed.pulse.upGeneration,
                .routeHealth = routeActive ?
                    input_v2::gameplay::PollOutputRouteHealth::Ready :
                    input_v2::gameplay::PollOutputRouteHealth::PublicationUnavailable,
                .neutral = !routeActive,
                .remapMode = remapMode,
                .connected = connected,
                .delegateReady = delegateReady
            };
            (void)input_v2::gameplay::PollOutputPublication::GetSingleton().PublishOwnerFrame(
                std::move(output));
        }
    }

    PadEventSnapshotDispatcher& PadEventSnapshotDispatcher::GetSingleton()
    {
        static PadEventSnapshotDispatcher instance;
        return instance;
    }

    void PadEventSnapshotDispatcher::SubmitSnapshot(
        const PadEventSnapshot& snapshot,
        const input_v2::presentation::SourceEvidenceFrame* sourceEvidenceFrame)
    {
        auto& hub = input_v2::ingress::IngressHub::GetSingleton();
        const auto pendingCountBeforeQueue = hub.PendingCount();
        const bool retainLegacySnapshot = _replayManualDrainActive.load(std::memory_order_acquire);
        (void)hub.PushPadSnapshot(snapshot, retainLegacySnapshot, sourceEvidenceFrame);
        const auto pendingCountAfterQueue = hub.PendingCount();

        input_v2::telemetry::InputTraceRecorder::GetSingleton().RecordDispatcherSubmit(
            snapshot,
            pendingCountBeforeQueue,
            pendingCountAfterQueue);

        (void)TryScheduleDrainTask();
    }

    void PadEventSnapshotDispatcher::SubmitReset()
    {
        PadEventSnapshot snapshot{};
        snapshot.type = PadEventSnapshotType::Reset;
        SubmitSnapshot(snapshot);
    }

    void PadEventSnapshotDispatcher::NotifyIngressPublished()
    {
        (void)TryScheduleDrainTask();
    }

    std::size_t PadEventSnapshotDispatcher::DrainOnOwnerTick(
        std::size_t maxEvents,
        const DrainTelemetryContext* telemetryContext,
        std::uint64_t frameToken)
    {
        if (maxEvents == 0) {
            return 0;
        }

        auto& contextRefresh = input_v2::context::ContextRefreshTick::GetSingleton();
        if (frameToken == 0) {
            frameToken = contextRefresh.BeginFrame();
        }
        auto ownerTick = input_v2::runtime::RuntimeOwnerGuard::GetSingleton().TryEnter(frameToken);
        if (!ownerTick.Accepted()) {
            return 0;
        }
        contextRefresh.RefreshOnOwnerTick(frameToken);

        auto& hub = input_v2::ingress::IngressHub::GetSingleton();
        const auto pendingBefore = hub.PendingCount();
        auto capture = hub.Capture(maxEvents);
        const auto drainedEventCount = capture.events.size();
        auto frames = RuntimeFrameAssembler().Assemble(capture);
        if (const auto resetReasons = RuntimeFrameAssembler().ConsumeGlobalResetRequest();
            resetReasons != 0) {
            (void)hub.PublishGlobalReset(
                resetReasons,
                input_v2::ingress::InputResetScope::GlobalInputState);
        }
        for (const auto& frame : frames) {
            PadEventSnapshotProcessor::GetSingleton().ProcessIngressFrame(frame);
        }
        AuthoritativePollState::GetSingleton().AdvanceOwnerTime();
        CommitAndPublishPollOutput(ownerTick.Generation());

        const auto pendingAfterDrain = hub.PendingCount();

        if (telemetryContext) {
            LogDrainTelemetry(*telemetryContext, maxEvents, drainedEventCount, pendingBefore, pendingAfterDrain);
        }

        return drainedEventCount;
    }

    std::size_t PadEventSnapshotDispatcher::DrainForReplay(
        std::size_t maxEvents,
        const DrainTelemetryContext* telemetryContext,
        ReplayDrainSink sink,
        void* context)
    {
        if (maxEvents == 0 || sink == nullptr) {
            return 0;
        }

        auto& contextRefresh = input_v2::context::ContextRefreshTick::GetSingleton();
        auto ownerTick = input_v2::runtime::RuntimeOwnerGuard::GetSingleton().TryEnter(
            contextRefresh.BeginFrame());
        if (!ownerTick.Accepted()) {
            return 0;
        }

        auto& hub = input_v2::ingress::IngressHub::GetSingleton();
        const auto pendingBefore = hub.PendingCount();
        auto capture = hub.Capture(maxEvents);
        const auto drainedEventCount = capture.events.size();
        const auto frames = RuntimeFrameAssembler().Assemble(capture);
        if (const auto resetReasons = RuntimeFrameAssembler().ConsumeGlobalResetRequest();
            resetReasons != 0) {
            (void)hub.PublishGlobalReset(
                resetReasons,
                input_v2::ingress::InputResetScope::GlobalInputState);
        }
        (void)sink;
        (void)context;
        for (const auto& frame : frames) {
            PadEventSnapshotProcessor::GetSingleton().ProcessIngressFrame(frame);
        }
        AuthoritativePollState::GetSingleton().AdvanceOwnerTime();
        CommitAndPublishPollOutput(ownerTick.Generation());
        const auto pendingAfterDrain = hub.PendingCount();

        if (telemetryContext) {
            LogDrainTelemetry(*telemetryContext, maxEvents, drainedEventCount, pendingBefore, pendingAfterDrain);
        }

        return drainedEventCount;
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
        input_v2::runtime::RuntimeOwnerGuard::GetSingleton().ResetForTests();
        input_v2::gameplay::PollOutputPublication::GetSingleton().ResetForTests();
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

    bool PadEventSnapshotDispatcher::TryScheduleDrainTask()
    {
        const auto pendingEvents = input_v2::ingress::IngressHub::GetSingleton().PendingCount();
        const auto hasUncapturedLatest =
            input_v2::ingress::IngressHub::GetSingleton().HasUncapturedLatest();
        const auto framePumpEnabled = _framePumpEnabled.load(std::memory_order_acquire);
        const auto replayManualDrainActive = _replayManualDrainActive.load(std::memory_order_acquire);
        const auto reason = framePumpEnabled ? DrainReason::TaskFallbackHighWater : DrainReason::FramePumpDisabled;
        const auto telemetry = BuildDrainTelemetryContext(reason, kUpstreamTaskFallbackPollStaleMs);
        if (!ShouldScheduleTaskFallback(
                framePumpEnabled,
                replayManualDrainActive,
                pendingEvents,
                hasUncapturedLatest,
                kUpstreamTaskFallbackHighWatermarkEvents,
                telemetry.routeState)) {
            return false;
        }

        if (_drainTaskQueued.exchange(true, std::memory_order_acq_rel)) {
            return false;
        }

        if (!ScheduleDrainTask()) {
            return false;
        }
        logger::warn(
            "[DualPad][IngressHub] Scheduled bounded fallback drain task pendingEvents={} hasUncapturedLatest={} highWatermarkEvents={} stalePollWindowMs={} reason={} routeState={} lastPollAgeMs={} hookInstalled={}",
            pendingEvents,
            hasUncapturedLatest,
            kUpstreamTaskFallbackHighWatermarkEvents,
            kUpstreamTaskFallbackPollStaleMs,
            ToString(reason),
            ToString(telemetry.routeState),
            FormatLastPollAgeMs(telemetry),
            telemetry.hookInstalled);
        return true;
    }

    bool PadEventSnapshotDispatcher::ScheduleDrainTask()
    {
        auto* taskInterface = SKSE::GetTaskInterface();
        if (!taskInterface) {
            logger::warn("[DualPad][Snapshot] Failed to get TaskInterface for main-thread drain");
            _drainTaskQueued.store(false, std::memory_order_release);
            return false;
        }

        taskInterface->AddTask([]() {
            auto& dispatcher = PadEventSnapshotDispatcher::GetSingleton();
            const auto telemetry = BuildDrainTelemetryContext(
                dispatcher.IsFramePumpEnabled() ? DrainReason::TaskFallbackHighWater : DrainReason::FramePumpDisabled,
                kUpstreamTaskFallbackPollStaleMs);
            dispatcher.DrainOnOwnerTick(kTaskDrainBudgetEvents, &telemetry);
            dispatcher._drainTaskQueued.store(false, std::memory_order_release);
            (void)dispatcher.TryScheduleDrainTask();
            });
        return true;
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
