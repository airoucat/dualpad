#include "pch.h"
#include "input/injection/PadEventSnapshotProcessor.h"

#include "input/backend/ActionBackendPolicy.h"
#include "input/backend/NativeButtonCommitBackend.h"
#include "input/backend/FrameActionPlanDebugLogger.h"
#include "input/backend/KeyboardHelperBackend.h"
#include "input/AuthoritativePollState.h"
#include "input/BindingManager.h"
#include "input/InputContext.h"
#include "input/InputModalityTracker.h"
#include "input/RuntimeConfig.h"
#include "input/injection/AxisProjection.h"
#include "input/injection/SyntheticStateDebugLogger.h"
#include "input/injection/UnmanagedDigitalPublisher.h"

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        bool ShouldLogMappingActivity()
        {
            return RuntimeConfig::GetSingleton().LogMappingEvents();
        }

        bool ShouldLogDispatchPlan()
        {
            return RuntimeConfig::GetSingleton().LogActionPlan();
        }

        bool ShouldLogHandledButtons()
        {
            const auto& config = RuntimeConfig::GetSingleton();
            return config.LogMappingEvents() || config.LogNativeInjection();
        }

        bool IsLifecycleAction(const backend::ActionRoutingDecision& decision)
        {
            return decision.ownsLifecycle;
        }

        bool ShouldBlockPhysicalButton(const PadEvent& event)
        {
            switch (event.type) {
            case PadEventType::ButtonPress:
            case PadEventType::Hold:
            case PadEventType::Tap:
                return IsSyntheticPadBitCode(event.code);
            default:
                return false;
            }
        }

        std::uint32_t CollectResolvedChordMask(
            const PadEventBuffer& events,
            InputContext context,
            const BindingResolver& bindingResolver)
        {
            std::uint32_t resolvedChordMask = 0;
            for (std::size_t i = 0; i < events.count; ++i) {
                const auto& event = events[i];
                if ((event.type != PadEventType::Combo && event.type != PadEventType::Layer) ||
                    !IsSyntheticPadBitCode(event.code)) {
                    continue;
                }

                if (bindingResolver.Resolve(event, context)) {
                    resolvedChordMask |= event.code;
                }
            }

            return resolvedChordMask;
        }

        std::uint32_t CollectObservedButtonPressMask(const PadEventBuffer& events)
        {
            std::uint32_t observedPressMask = 0;
            for (std::size_t i = 0; i < events.count; ++i) {
                const auto& event = events[i];
                if (event.type == PadEventType::ButtonPress &&
                    IsSyntheticPadBitCode(event.code)) {
                    observedPressMask |= event.code;
                }
            }

            return observedPressMask;
        }

        void BlockSourceMask(SourceBlockCoordinator& coordinator, std::uint32_t mask)
        {
            for (int bitIndex = 0; bitIndex < 32; ++bitIndex) {
                const auto bit = (1u << bitIndex);
                if ((mask & bit) != 0) {
                    coordinator.Block(bit);
                }
            }
        }

        void SynthesizeMissingButtonEdges(
            const SyntheticPadFrame& frame,
            InputContext context,
            const PadState& state,
            PadEventBuffer& events)
        {
            const auto observedPressMask = CollectObservedButtonPressMask(events);
            const auto comboParticipantMask = BindingManager::GetSingleton().GetComboParticipantMask(context);

            const auto missingPressMask =
                frame.pressedMask &
                ~observedPressMask &
                ~comboParticipantMask;

            for (int bitIndex = 0; bitIndex < 32; ++bitIndex) {
                const auto bit = (1u << bitIndex);
                if ((missingPressMask & bit) == 0) {
                    continue;
                }

                PadEvent synthesized{};
                synthesized.type = PadEventType::ButtonPress;
                synthesized.triggerType = TriggerType::Button;
                synthesized.code = bit;
                synthesized.timestampUs = frame.sourceTimestampUs != 0 ? frame.sourceTimestampUs : state.timestampUs;
                synthesized.modifierMask = state.buttons.digitalMask & ~bit;
                if (events.Push(synthesized)) {
                    logger::warn(
                        "[DualPad][Snapshot] Synthesized missing press edge for source=0x{:08X} context={} seq={} firstSeq={}",
                        bit,
                        ToString(context),
                        frame.sequence,
                        frame.firstSequence);
                } else {
                    logger::warn(
                        "[DualPad][Snapshot] Failed to synthesize missing press edge for source=0x{:08X}; frame event buffer full",
                        bit);
                }
            }
        }

    }

    PadEventSnapshotProcessor::PadEventSnapshotProcessor() = default;

    PadEventSnapshotProcessor& PadEventSnapshotProcessor::GetSingleton()
    {
        static PadEventSnapshotProcessor instance;
        return instance;
    }

    void PadEventSnapshotProcessor::ResetState()
    {
        ResetAllState();
    }

    void PadEventSnapshotProcessor::ResetAllState()
    {
        _lastProcessedSequence = 0;
        _stateReducer.Reset();
        AuthoritativePollState::GetSingleton().Reset();
        _lifecycleCoordinator.Reset();
        _sourceBlockCoordinator.Reset();
        backend::NativeButtonCommitBackend::GetSingleton().Reset();
        backend::KeyboardHelperBackend::GetSingleton().Reset();
        ResetFramePlanning();
    }

    void PadEventSnapshotProcessor::ResyncNativeState()
    {
        _lastProcessedSequence = 0;
        _stateReducer.Reset();
        AuthoritativePollState::GetSingleton().Reset();
        _lifecycleCoordinator.Reset();
        _sourceBlockCoordinator.Reset();
        backend::NativeButtonCommitBackend::GetSingleton().Reset();
        ResetFramePlanning();
    }

    std::uint32_t PadEventSnapshotProcessor::CollectPlannedActions(
        const PadEventBuffer& events,
        InputContext context,
        std::uint32_t contextEpoch)
    {
        std::uint32_t handledButtons = _sourceBlockCoordinator.CurrentMask();
        const auto resolvedChordMask = CollectResolvedChordMask(events, context, _bindingResolver);
        for (std::size_t i = 0; i < events.count; ++i) {
            const auto& event = events[i];
            if (event.type == PadEventType::ButtonRelease &&
                IsSyntheticPadBitCode(event.code) &&
                _sourceBlockCoordinator.IsBlocked(event.code)) {
                handledButtons |= event.code;
                _sourceBlockCoordinator.Release(event.code);
            }

            if (event.type == PadEventType::ButtonRelease &&
                IsSyntheticPadBitCode(event.code)) {
                const auto released = _lifecycleCoordinator.ReleaseOwningAction(
                    event.code,
                    event.timestampUs,
                    context,
                    contextEpoch,
                    _framePlan);
                if (released) {
                    handledButtons |= event.code;
                    if (ShouldLogMappingActivity()) {
                        logger::info(
                            "[DualPad][Mapping] Planned lifecycle release source=0x{:08X} context={}",
                            event.code,
                            ToString(context));
                    }
                    continue;
                }
            }

            const auto resolved = _bindingResolver.Resolve(event, context);
            if (!resolved) {
                continue;
            }

            if (event.type == PadEventType::ButtonPress &&
                IsSyntheticPadBitCode(event.code) &&
                (resolvedChordMask & event.code) != 0) {
                _sourceBlockCoordinator.Block(event.code);
                handledButtons |= event.code;
                if (ShouldLogMappingActivity()) {
                    logger::info(
                        "[DualPad][Mapping] Suppressing ButtonPress source=0x{:08X} because same-frame Layer/Combo resolved",
                        event.code);
                }
                continue;
            }

            const auto routingDecision = backend::ActionBackendPolicy::Decide(resolved->actionId);

            if (event.type == PadEventType::ButtonPress &&
                IsSyntheticPadBitCode(event.code) &&
                IsLifecycleAction(routingDecision)) {
                if (_lifecycleCoordinator.RegisterOwningAction(event.code, resolved->actionId, routingDecision)) {
                    _sourceBlockCoordinator.Block(event.code);
                    handledButtons |= event.code;
                    continue;
                }
            }

            if (!_planner.PlanResolvedEvent(*resolved, event, context, contextEpoch, _framePlan)) {
                continue;
            }

            if (ShouldLogMappingActivity()) {
                logger::info("[DualPad][Mapping] Planned event {} source=0x{:08X} context={} action={}",
                    ToString(event.type),
                    event.code,
                    ToString(context),
                    resolved->actionId);
            }

            if (event.type == PadEventType::ButtonPress &&
                IsSyntheticPadBitCode(event.code)) {
                _sourceBlockCoordinator.Block(event.code);
                handledButtons |= event.code;
            }

            if (ShouldBlockPhysicalButton(event)) {
                handledButtons |= event.code;
            }
        }

        if (handledButtons != 0 && ShouldLogHandledButtons()) {
            logger::info("[DualPad] Blocked buttons from Skyrim: {:08X}", handledButtons);
        }

        return handledButtons;
    }

    void PadEventSnapshotProcessor::CollectLifecycleActions(
        const SyntheticPadFrame& frame,
        InputContext context,
        std::uint32_t contextEpoch)
    {
        _sourceBlockCoordinator.ReleaseMask(
            _lifecycleCoordinator.PlanFrame(frame, context, contextEpoch, _framePlan));
    }

    void PadEventSnapshotProcessor::DispatchPlannedActions()
    {
        for (const auto& action : _framePlan) {
            const auto dispatchResult = _actionDispatcher.DispatchPlannedAction(action);
            if (!dispatchResult.handled) {
                continue;
            }

            if (ShouldLogDispatchPlan()) {
                logger::info(
                    "[DualPad][DispatchPlan] action={} backend={} phase={} source=0x{:08X} output=0x{:08X} context={} route={}",
                    action.actionId,
                    backend::ToString(action.backend),
                    backend::ToString(action.phase),
                    action.sourceCode,
                    action.outputCode,
                    ToString(action.context),
                    ToString(dispatchResult.target));
            }
        }
    }

    void PadEventSnapshotProcessor::ResetFramePlanning()
    {
        _framePlan.Clear();
    }

    void PadEventSnapshotProcessor::BeginFramePlanning(InputContext context, std::uint32_t contextEpoch)
    {
        _framePlan.Clear();
        backend::NativeButtonCommitBackend::GetSingleton().BeginFrame(
            context,
            contextEpoch);
    }

    void PadEventSnapshotProcessor::FinishFramePlanning(const SyntheticPadFrame& frame, InputContext context)
    {
        DispatchPlannedActions();
        const auto analog = CollectBoundAnalogState(frame, context);
        AuthoritativePollState::GetSingleton().PublishAnalogState(
            analog.moveX,
            analog.moveY,
            analog.lookX,
            analog.lookY,
            analog.leftTrigger,
            analog.rightTrigger);
        backend::LogFrameActionPlan(_framePlan);
    }

    void PadEventSnapshotProcessor::Process(const PadEventSnapshot& snapshot)
    {
        if (snapshot.type == PadEventSnapshotType::Reset) {
            logger::info("[DualPad][Snapshot] Resetting injected state");
            ResetAllState();
            return;
        }

        if (snapshot.overflowed || snapshot.events.overflowed) {
            logger::warn(
                "[DualPad][Snapshot] Processing overflowed event snapshot seq={} firstSeq={} droppedEvents={} coalesced={}",
                snapshot.sequence,
                snapshot.firstSequence,
                snapshot.events.droppedCount,
                snapshot.coalesced);
        }

        if (_lastProcessedSequence != 0 &&
            snapshot.firstSequence != (_lastProcessedSequence + 1)) {
            logger::warn(
                "[DualPad][Snapshot] Sequence gap detected: expected {} got {}. Resynchronizing native input state.",
                _lastProcessedSequence + 1,
                snapshot.firstSequence);
            ResyncNativeState();
        }

        const auto context = snapshot.context;
        const auto contextEpoch = snapshot.contextEpoch;
        const auto& syntheticFrame = _stateReducer.Reduce(snapshot, context);
        InputModalityTracker::GetSingleton().ReportUpstreamPadFrame(syntheticFrame);
        AuthoritativePollState::GetSingleton().PublishFrameMetadata(
            syntheticFrame.sourceTimestampUs,
            syntheticFrame.overflowed,
            syntheticFrame.coalesced);
        LogSyntheticPadFrame(syntheticFrame);
        BeginFramePlanning(context, contextEpoch);

        auto effectiveEvents = snapshot.events;
        SynthesizeMissingButtonEdges(syntheticFrame, context, snapshot.state, effectiveEvents);

        auto handledButtons = CollectPlannedActions(effectiveEvents, context, contextEpoch);
        if (syntheticFrame.overflowed || syntheticFrame.coalesced) {
            const auto observedPressMask = CollectObservedButtonPressMask(effectiveEvents);
            const auto recoveredPressedMask =
                syntheticFrame.pressedMask & ~observedPressMask & ~handledButtons;
            if (recoveredPressedMask != 0) {
                BlockSourceMask(_sourceBlockCoordinator, recoveredPressedMask);
                handledButtons |= recoveredPressedMask;
                logger::warn(
                    "[DualPad][Snapshot] Applied {} source-block compensation for pressed bits 0x{:08X}",
                    syntheticFrame.overflowed ? "overflow/coalesced" : "coalesced",
                    recoveredPressedMask);
            }
        }
        CollectLifecycleActions(syntheticFrame, context, contextEpoch);
        FinishFramePlanning(syntheticFrame, context);
        // Poll-owned digital actions now commit through NativeButtonCommitBackend.
        // Reduced raw edges still publish unmanaged digital facts here so the
        // authoritative poll state remains the single output contract.
        PublishUnmanagedDigitalState(syntheticFrame, handledButtons);

        _lastProcessedSequence = snapshot.sequence;
    }
}

