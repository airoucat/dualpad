#include "pch.h"
#include "input/injection/PadEventSnapshotProcessor.h"

#include "input/Action.h"
#include "input/backend/ActionBackendPolicy.h"
#include "input/backend/NativeButtonCommitBackend.h"
#include "input/backend/FrameActionPlanDebugLogger.h"
#include "input/backend/KeyboardHelperBackend.h"
#include "input/AuthoritativePollState.h"
#include "input/BindingManager.h"
#include "input/InputContext.h"
#include "input/RuntimeConfig.h"
#include "input/injection/AxisProjection.h"
#include "input/injection/GameplayOwnershipCoordinator.h"
#include "input/injection/SyntheticStateDebugLogger.h"
#include "input/injection/UnmanagedDigitalPublisher.h"

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        enum class ResyncRecoveryKind : std::uint8_t
        {
            SameContextGap,
            CrossContextBoundary,
            HardResync
        };

        enum class ResyncRecoveryDisposition : std::uint8_t
        {
            ReplayPress,
            RecoverOwnerOnly,
            SuppressUntilRelease
        };

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

        constexpr std::string_view ToString(ResyncRecoveryKind kind)
        {
            switch (kind) {
            case ResyncRecoveryKind::SameContextGap:
                return "SameContextGap";
            case ResyncRecoveryKind::CrossContextBoundary:
                return "CrossContextBoundary";
            case ResyncRecoveryKind::HardResync:
            default:
                return "HardResync";
            }
        }

        constexpr std::string_view ToString(ResyncRecoveryDisposition disposition)
        {
            switch (disposition) {
            case ResyncRecoveryDisposition::ReplayPress:
                return "ReplayPress";
            case ResyncRecoveryDisposition::RecoverOwnerOnly:
                return "RecoverOwnerOnly";
            case ResyncRecoveryDisposition::SuppressUntilRelease:
            default:
                return "SuppressUntilRelease";
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
        _hasLastStableFrame = false;
        _lastStableContext = InputContext::Gameplay;
        _lastStableContextEpoch = 0;
        _lastStableDownMask = 0;
        _stateReducer.Reset();
        AuthoritativePollState::GetSingleton().Reset();
        _lifecycleCoordinator.Reset();
        _sourceBlockCoordinator.Reset();
        backend::NativeButtonCommitBackend::GetSingleton().Reset();
        backend::KeyboardHelperBackend::GetSingleton().Reset();
        GameplayOwnershipCoordinator::GetSingleton().Reset();
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
        GameplayOwnershipCoordinator::GetSingleton().Reset();
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
                    if (ShouldLogMappingActivity() &&
                        resolved->actionId == actions::Sneak) {
                        logger::info(
                            "[DualPad][SneakProbe] register lifecycle owner source=0x{:08X} event={} context={} action={}",
                            event.code,
                            ToString(event.type),
                            ToString(context),
                            resolved->actionId);
                    }
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
                if (resolved->actionId == actions::Sneak) {
                    logger::info(
                        "[DualPad][SneakProbe] planned resolved event={} source=0x{:08X} context={} action={}",
                        ToString(event.type),
                        event.code,
                        ToString(context),
                        resolved->actionId);
                }
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

    std::uint32_t PadEventSnapshotProcessor::RecoverMissingPressStateAfterResync(
        const SyntheticPadFrame& frame,
        InputContext context,
        std::uint32_t contextEpoch,
        bool crossContextMismatch,
        const PadState& state,
        PadEventBuffer& events)
    {
        ResyncRecoveryKind recoveryKind = ResyncRecoveryKind::SameContextGap;
        if (frame.overflowed) {
            recoveryKind = ResyncRecoveryKind::HardResync;
        } else if (!_hasLastStableFrame ||
            crossContextMismatch ||
            context != _lastStableContext ||
            contextEpoch != _lastStableContextEpoch) {
            recoveryKind = ResyncRecoveryKind::CrossContextBoundary;
        }

        std::uint32_t handledButtons = 0;

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

            const bool preGapDown =
                _hasLastStableFrame &&
                context == _lastStableContext &&
                contextEpoch == _lastStableContextEpoch &&
                (_lastStableDownMask & bit) != 0;

            const auto resolved = _bindingResolver.Resolve(synthesized, context);
            if (resolved) {
                const auto routingDecision = backend::ActionBackendPolicy::Decide(resolved->actionId);
                switch (routingDecision.contract) {
                case backend::ActionOutputContract::Hold:
                case backend::ActionOutputContract::Repeat:
                    if (routingDecision.ownsLifecycle) {
                        if (_lifecycleCoordinator.RegisterRecoveredOwningAction(bit, resolved->actionId, routingDecision)) {
                            _sourceBlockCoordinator.Block(bit);
                            handledButtons |= bit;
                            logger::warn(
                                "[DualPad][Snapshot] Resync recovery source=0x{:08X} context={} action={} contract={} kind={} preGapDown={} decision={}",
                                bit,
                                ToString(context),
                                resolved->actionId,
                                backend::ToString(routingDecision.contract),
                                ToString(recoveryKind),
                                preGapDown,
                                ToString(ResyncRecoveryDisposition::RecoverOwnerOnly));
                            continue;
                        }
                    }
                    break;

                case backend::ActionOutputContract::Pulse:
                case backend::ActionOutputContract::Toggle:
                    if (recoveryKind == ResyncRecoveryKind::SameContextGap &&
                        !preGapDown) {
                        if (events.Push(synthesized)) {
                            logger::warn(
                                "[DualPad][Snapshot] Resync recovery source=0x{:08X} context={} action={} contract={} kind={} preGapDown={} decision={}",
                                bit,
                                ToString(context),
                                resolved->actionId,
                                backend::ToString(routingDecision.contract),
                                ToString(recoveryKind),
                                preGapDown,
                                ToString(ResyncRecoveryDisposition::ReplayPress));
                            continue;
                        }
                    }

                    _sourceBlockCoordinator.Block(bit);
                    handledButtons |= bit;
                    logger::warn(
                        "[DualPad][Snapshot] Resync recovery source=0x{:08X} context={} action={} contract={} kind={} preGapDown={} decision={}",
                        bit,
                        ToString(context),
                        resolved->actionId,
                        backend::ToString(routingDecision.contract),
                        ToString(recoveryKind),
                        preGapDown,
                        ToString(ResyncRecoveryDisposition::SuppressUntilRelease));
                    continue;

                case backend::ActionOutputContract::Axis:
                case backend::ActionOutputContract::None:
                default:
                    break;
                }
            }

            if (recoveryKind == ResyncRecoveryKind::SameContextGap &&
                !preGapDown) {
                if (events.Push(synthesized)) {
                    logger::warn(
                        "[DualPad][Snapshot] Resync recovery source=0x{:08X} context={} resolvedAction={} kind={} preGapDown={} decision={}",
                        bit,
                        ToString(context),
                        resolved ? resolved->actionId : std::string_view("None"),
                        ToString(recoveryKind),
                        preGapDown,
                        ToString(ResyncRecoveryDisposition::ReplayPress));
                    continue;
                }
            }

            _sourceBlockCoordinator.Block(bit);
            handledButtons |= bit;
            logger::warn(
                "[DualPad][Snapshot] Resync recovery source=0x{:08X} context={} resolvedAction={} kind={} preGapDown={} decision={}",
                bit,
                ToString(context),
                resolved ? resolved->actionId : std::string_view("None"),
                ToString(recoveryKind),
                preGapDown,
                ToString(ResyncRecoveryDisposition::SuppressUntilRelease));
        }

        return handledButtons;
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
        const auto& runtimeConfig = RuntimeConfig::GetSingleton();
        if (!runtimeConfig.EnableGameplayOwnership()) {
            AuthoritativePollState::GetSingleton().PublishAnalogState(
                analog.moveX,
                analog.moveY,
                analog.lookX,
                analog.lookY,
                analog.leftTrigger,
                analog.rightTrigger);
            backend::LogFrameActionPlan(_framePlan);
            return;
        }

        GameplayOwnershipCoordinator::GetSingleton().UpdateDigitalOwnership(context, _framePlan);
        const auto ownedAnalog =
            GameplayOwnershipCoordinator::GetSingleton().ApplyOwnership(
                analog,
                frame,
                context);
        AuthoritativePollState::GetSingleton().PublishAnalogState(
            ownedAnalog.analog.moveX,
            ownedAnalog.analog.moveY,
            ownedAnalog.analog.lookX,
            ownedAnalog.analog.lookY,
            ownedAnalog.analog.leftTrigger,
            ownedAnalog.analog.rightTrigger);
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

        bool didResync = false;
        if (_lastProcessedSequence != 0 &&
            snapshot.firstSequence != (_lastProcessedSequence + 1)) {
            logger::warn(
                "[DualPad][Snapshot] Sequence gap detected: expected {} got {}. Resynchronizing native input state.",
                _lastProcessedSequence + 1,
                snapshot.firstSequence);
            ResyncNativeState();
            didResync = true;
        }

        const auto context = snapshot.context;
        const auto contextEpoch = snapshot.contextEpoch;
        const auto& syntheticFrame = _stateReducer.Reduce(snapshot, context);
        AuthoritativePollState::GetSingleton().PublishFrameMetadata(
            syntheticFrame.sourceTimestampUs,
            syntheticFrame.overflowed,
            syntheticFrame.coalesced);
        LogSyntheticPadFrame(syntheticFrame);
        BeginFramePlanning(context, contextEpoch);

        auto effectiveEvents = snapshot.events;
        std::uint32_t recoveredPressHandledMask = 0;
        if (didResync) {
            recoveredPressHandledMask = RecoverMissingPressStateAfterResync(
                syntheticFrame,
                context,
                contextEpoch,
                snapshot.crossContextMismatch,
                snapshot.state,
                effectiveEvents);
        } else {
            SynthesizeMissingButtonEdges(syntheticFrame, context, snapshot.state, effectiveEvents);
        }

        auto handledButtons = recoveredPressHandledMask | CollectPlannedActions(effectiveEvents, context, contextEpoch);
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
        _hasLastStableFrame = true;
        _lastStableContext = context;
        _lastStableContextEpoch = contextEpoch;
        _lastStableDownMask = syntheticFrame.downMask;
    }
}

