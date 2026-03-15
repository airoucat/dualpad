#include "pch.h"
#include "input/injection/PadEventSnapshotProcessor.h"

#include "input/backend/ActionBackendPolicy.h"
#include "input/backend/ButtonEventBackend.h"
#include "input/backend/FrameActionPlanDebugLogger.h"
#include "input/backend/KeyboardNativeBackend.h"
#include "input/InputContext.h"
#include "input/RuntimeConfig.h"
#include "input/injection/LegacyNativeButtonSurface.h"
#include "input/injection/SyntheticStateDebugLogger.h"

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
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

        std::uint32_t CollectResolvedComboMask(
            const PadEventBuffer& events,
            InputContext context,
            const BindingResolver& bindingResolver)
        {
            std::uint32_t resolvedComboMask = 0;
            for (std::size_t i = 0; i < events.count; ++i) {
                const auto& event = events[i];
                if (event.type != PadEventType::Combo ||
                    !IsSyntheticPadBitCode(event.code)) {
                    continue;
                }

                if (bindingResolver.Resolve(event, context)) {
                    resolvedComboMask |= event.code;
                }
            }

            return resolvedComboMask;
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
    }

    PadEventSnapshotProcessor::PadEventSnapshotProcessor() :
        _actionDispatcher(_compatibilityInjector)
    {
    }

    PadEventSnapshotProcessor& PadEventSnapshotProcessor::GetSingleton()
    {
        static PadEventSnapshotProcessor instance;
        return instance;
    }

    void PadEventSnapshotProcessor::ResetState()
    {
        _lastProcessedSequence = 0;
        _stateReducer.Reset();
        _compatibilityInjector.Reset();
        LegacyNativeButtonSurface::GetSingleton().Reset();
        _lifecycleCoordinator.Reset();
        _sourceBlockCoordinator.Reset();
        backend::ButtonEventBackend::GetSingleton().Reset();
        backend::KeyboardNativeBackend::GetSingleton().Reset();
        ResetFramePlanning();
    }

    std::uint32_t PadEventSnapshotProcessor::CollectPlannedActions(
        const PadEventBuffer& events,
        InputContext context)
    {
        std::uint32_t handledButtons = _sourceBlockCoordinator.CurrentMask();
        const auto resolvedComboMask = CollectResolvedComboMask(events, context, _bindingResolver);
        for (std::size_t i = 0; i < events.count; ++i) {
            const auto& event = events[i];
            _actionDispatcher.DispatchDirectPadEvent(event);

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
                    ContextManager::GetSingleton().GetCurrentEpoch(),
                    _framePlan);
                if (released) {
                    handledButtons |= event.code;
                    logger::info(
                        "[DualPad][Mapping] Planned lifecycle release source=0x{:08X} context={}",
                        event.code,
                        ToString(context));
                    continue;
                }
            }

            const auto resolved = _bindingResolver.Resolve(event, context);
            if (!resolved) {
                continue;
            }

            if (event.type == PadEventType::ButtonPress &&
                IsSyntheticPadBitCode(event.code) &&
                (resolvedComboMask & event.code) != 0) {
                _sourceBlockCoordinator.Block(event.code);
                handledButtons |= event.code;
                logger::info(
                    "[DualPad][Mapping] Suppressing ButtonPress source=0x{:08X} because same-frame Combo resolved",
                    event.code);
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

            if (!_planner.PlanResolvedEvent(*resolved, event, context, _framePlan)) {
                continue;
            }

            logger::info("[DualPad][Mapping] Planned event {} source=0x{:08X} context={} action={}",
                ToString(event.type),
                event.code,
                ToString(context),
                resolved->actionId);

            if (event.type == PadEventType::ButtonPress &&
                IsSyntheticPadBitCode(event.code)) {
                _sourceBlockCoordinator.Block(event.code);
                handledButtons |= event.code;
            }

            if (ShouldBlockPhysicalButton(event)) {
                handledButtons |= event.code;
            }
        }

        if (handledButtons != 0) {
            logger::info("[DualPad] Blocked buttons from Skyrim: {:08X}", handledButtons);
        }

        return handledButtons;
    }

    void PadEventSnapshotProcessor::CollectLifecycleActions(
        const SyntheticPadFrame& frame,
        InputContext context)
    {
        _sourceBlockCoordinator.ReleaseMask(_lifecycleCoordinator.PlanFrame(frame, context, _framePlan));
    }

    void PadEventSnapshotProcessor::DispatchPlannedActions()
    {
        for (const auto& action : _framePlan) {
            const auto dispatchResult = _actionDispatcher.DispatchPlannedAction(action);
            if (!dispatchResult.handled) {
                continue;
            }

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

    void PadEventSnapshotProcessor::ResetFramePlanning()
    {
        _framePlan.Clear();
        _plannedNativeState.Reset();
    }

    void PadEventSnapshotProcessor::BeginFramePlanning(InputContext context)
    {
        _framePlan.Clear();
        _plannedNativeState.BeginFrame(context);
        backend::ButtonEventBackend::GetSingleton().BeginFrame(
            context,
            ContextManager::GetSingleton().GetCurrentEpoch());
    }

    void PadEventSnapshotProcessor::FinishFramePlanning()
    {
        DispatchPlannedActions();
        _plannedNativeState.ApplyPlan(_framePlan);
        backend::LogFrameActionPlan(_framePlan);
        backend::LogVirtualGamepadState(_plannedNativeState.GetState());
    }

    void PadEventSnapshotProcessor::Process(const PadEventSnapshot& snapshot)
    {
        if (snapshot.type == PadEventSnapshotType::Reset) {
            logger::info("[DualPad][Snapshot] Resetting injected state");
            ResetState();
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
                "[DualPad][Snapshot] Sequence gap detected: expected {} got {}. Resetting compatibility state.",
                _lastProcessedSequence + 1,
                snapshot.firstSequence);
            ResetState();
        }

        const auto context = ContextManager::GetSingleton().GetCurrentContext();
        const auto& syntheticFrame = _stateReducer.Reduce(snapshot, context);
        LogSyntheticPadFrame(syntheticFrame);
        BeginFramePlanning(context);

        auto handledButtons = CollectPlannedActions(snapshot.events, context);
        if (syntheticFrame.overflowed) {
            const auto observedPressMask = CollectObservedButtonPressMask(snapshot.events);
            const auto recoveredPressedMask =
                syntheticFrame.pressedMask & ~observedPressMask & ~handledButtons;
            if (recoveredPressedMask != 0) {
                BlockSourceMask(_sourceBlockCoordinator, recoveredPressedMask);
                handledButtons |= recoveredPressedMask;
                logger::warn(
                    "[DualPad][Snapshot] Applied overflow source-block compensation for pressed bits 0x{:08X}",
                    recoveredPressedMask);
            }
        }
        CollectLifecycleActions(syntheticFrame, context);
        FinishFramePlanning();
        // The default digital mainline now commits at Poll ownership time. Old
        // native frame injection no longer owns snapshot-time dispatch here.
        _compatibilityInjector.SubmitLegacyDigitalFallback(syntheticFrame, handledButtons);
        _compatibilityInjector.SubmitAnalogState(syntheticFrame);

        _lastProcessedSequence = snapshot.sequence;
    }
}
