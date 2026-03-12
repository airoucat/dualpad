#include "pch.h"
#include "input/injection/PadEventSnapshotProcessor.h"

#include "input/InputContext.h"
#include "input/PadProfile.h"
#include "input/RuntimeConfig.h"
#include "input/backend/FrameActionPlanDebugLogger.h"
#include "input/backend/KeyboardNativeBackend.h"
#include "input/injection/SyntheticStateDebugLogger.h"
#include "input/injection/UpstreamGamepadHook.h"

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        std::size_t BitIndex(std::uint32_t code)
        {
            return static_cast<std::size_t>(std::countr_zero(code));
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

        float ComputeHeldSeconds(
            const SyntheticPadFrame& frame,
            const SyntheticButtonState& button,
            std::uint64_t logicalPressedAtUs)
        {
            if (button.down) {
                if (button.pressed) {
                    return 0.0f;
                }

                return button.heldSeconds;
            }

            const auto releaseTimestampUs =
                button.releasedAtUs != 0 ? button.releasedAtUs : frame.sourceTimestampUs;
            if (logicalPressedAtUs != 0 &&
                releaseTimestampUs != 0 &&
                releaseTimestampUs >= logicalPressedAtUs) {
                return static_cast<float>(releaseTimestampUs - logicalPressedAtUs) / 1000000.0f;
            }

            return button.heldSeconds;
        }

        bool UsePollCommittedNativeState()
        {
            return UpstreamGamepadHook::GetSingleton().IsRouteActive();
        }
    }

    PadEventSnapshotProcessor::PadEventSnapshotProcessor() :
        _actionDispatcher(_compatibilityInjector, _nativeInjector)
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
        _blockedSourceButtons = 0;
        _activeButtonActions = {};
        _stateReducer.Reset();
        _compatibilityInjector.Reset();
        _nativeInjector.Reset();
        backend::KeyboardNativeBackend::GetSingleton().Reset();
        ResetPlanningState();
    }

    const backend::VirtualGamepadState& PadEventSnapshotProcessor::CommitNativeStateForPoll(std::uint64_t pollTimestampUs)
    {
        return _nativeStateBackend.CommitPollState(pollTimestampUs);
    }

    void PadEventSnapshotProcessor::PrependInjectedInputEvents(RE::InputEvent*& head)
    {
        _nativeInjector.PrependStagedButtonEvents(head);
    }

    std::size_t PadEventSnapshotProcessor::PrependInjectedInputEventsUsingQueueCache(RE::InputEvent*& head)
    {
        return _nativeInjector.PrependStagedButtonEventsUsingQueueCache(head);
    }

    std::size_t PadEventSnapshotProcessor::PrependInjectedInputQueueEvents(RE::InputEvent*& head, RE::InputEvent*& tail)
    {
        return _nativeInjector.PrependStagedButtonEventsToInputQueue(head, tail);
    }

    std::size_t PadEventSnapshotProcessor::AppendInjectedInputEventsUsingEngineCache(RE::InputEvent*& head)
    {
        return _nativeInjector.AppendStagedButtonEventsUsingEngineCache(head);
    }

    std::size_t PadEventSnapshotProcessor::GetPendingInjectedButtonCount() const
    {
        return _nativeInjector.GetStagedButtonEventCount();
    }

    void PadEventSnapshotProcessor::DiscardPendingInjectedButtonEvents()
    {
        _nativeInjector.DiscardStagedButtonEvents();
    }

    void PadEventSnapshotProcessor::ReleaseInjectedInputEvents()
    {
        _nativeInjector.ReleaseInjectedButtonEvents();
    }

    std::size_t PadEventSnapshotProcessor::FlushInjectedInputQueue()
    {
        return _nativeInjector.FlushStagedButtonEventsToInputQueue();
    }

    std::uint32_t PadEventSnapshotProcessor::DispatchPadEvents(
        const PadEventBuffer& events,
        InputContext context)
    {
        const bool useCommittedNativeState = UsePollCommittedNativeState();
        std::uint32_t handledButtons = _blockedSourceButtons;

        for (std::size_t i = 0; i < events.count; ++i) {
            const auto& event = events[i];
            _actionDispatcher.DispatchDirectPadEvent(event);

            if (event.type == PadEventType::ButtonRelease &&
                IsSyntheticPadBitCode(event.code) &&
                (_blockedSourceButtons & event.code) != 0) {
                const auto bitIndex = BitIndex(event.code);
                if (!_activeButtonActions[bitIndex].active) {
                    handledButtons |= event.code;
                    _blockedSourceButtons &= ~event.code;
                }
            }

            const auto resolved = _bindingResolver.Resolve(event, context);
            if (!resolved) {
                continue;
            }

            auto routedBinding = *resolved;
            if (IsSyntheticPadBitCode(event.code)) {
                auto& activeAction = _activeButtonActions[BitIndex(event.code)];
                if (activeAction.active &&
                    (event.type == PadEventType::ButtonRelease || event.type == PadEventType::Hold)) {
                    routedBinding.actionId = activeAction.actionId;
                }
            }

            const auto decision = backend::ActionBackendPolicy::Decide(routedBinding.actionId);
            const bool isTrackedNativeButton =
                decision.backend == backend::PlannedBackend::NativeState &&
                decision.kind == backend::PlannedActionKind::NativeButton &&
                IsSyntheticPadBitCode(event.code);

            if (isTrackedNativeButton &&
                event.type == PadEventType::ButtonPress) {
                auto& activeAction = _activeButtonActions[BitIndex(event.code)];
                activeAction.active = true;
                activeAction.actionId = routedBinding.actionId;
                activeAction.logicalPressedAtUs = event.timestampUs;
                activeAction.lifecycle = decision.lifecycle;
                activeAction.sawReleaseThisSnapshot = false;
                _blockedSourceButtons |= event.code;
                handledButtons |= event.code;
            }

            if (isTrackedNativeButton &&
                event.type == PadEventType::ButtonRelease) {
                auto& activeAction = _activeButtonActions[BitIndex(event.code)];
                if (activeAction.active) {
                    activeAction.sawReleaseThisSnapshot = true;
                }
                handledButtons |= event.code;
            }

            if (useCommittedNativeState &&
                decision.backend == backend::PlannedBackend::NativeState) {
                _planner.PlanResolvedEvent(routedBinding, event, context, _currentPlan);

                if (isTrackedNativeButton || ShouldBlockPhysicalButton(event)) {
                    handledButtons |= event.code;
                }

                continue;
            }

            if (isTrackedNativeButton) {
                continue;
            }

            const auto dispatchResult = _actionDispatcher.Dispatch(
                routedBinding.actionId,
                context);
            if (!dispatchResult.handled) {
                continue;
            }

            logger::info(
                "[DualPad][Mapping] Event {} mapped to action {} via {}",
                ToString(event.type),
                routedBinding.actionId,
                ToString(dispatchResult.target));

            if (event.type == PadEventType::ButtonPress &&
                IsSyntheticPadBitCode(event.code)) {
                _blockedSourceButtons |= event.code;
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

    void PadEventSnapshotProcessor::UpdateActiveButtonActions(
        const SyntheticPadFrame& frame,
        InputContext context)
    {
        const bool useCommittedNativeState = UsePollCommittedNativeState();

        for (std::size_t bitIndex = 0; bitIndex < _activeButtonActions.size(); ++bitIndex) {
            auto& activeAction = _activeButtonActions[bitIndex];
            if (!activeAction.active) {
                continue;
            }

            const auto sourceCode = (1u << bitIndex);
            const auto& button = frame.buttons[bitIndex];
            const auto heldSeconds = ComputeHeldSeconds(frame, button, activeAction.logicalPressedAtUs);

            if (button.down) {
                if (useCommittedNativeState) {
                    if (!button.pressed) {
                        _planner.PlanButtonState(
                            activeAction.actionId,
                            true,
                            heldSeconds,
                            sourceCode,
                            context,
                            _currentPlan);
                    }
                } else {
                    (void)_actionDispatcher.DispatchButtonState(
                        activeAction.actionId,
                        true,
                        heldSeconds,
                        context);
                }

                activeAction.sawReleaseThisSnapshot = false;
                continue;
            }

            if (useCommittedNativeState) {
                if (!activeAction.sawReleaseThisSnapshot) {
                    _planner.PlanButtonState(
                        activeAction.actionId,
                        false,
                        heldSeconds,
                        sourceCode,
                        context,
                        _currentPlan);
                }
            } else {
                (void)_actionDispatcher.DispatchButtonState(
                    activeAction.actionId,
                    false,
                    heldSeconds,
                    context);
            }

            _blockedSourceButtons &= ~sourceCode;
            activeAction = {};
        }
    }

    void PadEventSnapshotProcessor::ResetPlanningState()
    {
        _currentPlan.Clear();
        _nativeStateBackend.Reset();
    }

    void PadEventSnapshotProcessor::BeginPlanning(InputContext context)
    {
        _currentPlan.Clear();
        _nativeStateBackend.BeginFrame(context);
        for (auto& activeAction : _activeButtonActions) {
            activeAction.sawReleaseThisSnapshot = false;
        }
    }

    void PadEventSnapshotProcessor::FinishPlanning()
    {
        _nativeStateBackend.ApplyPlan(_currentPlan);
        backend::LogFrameActionPlan(_currentPlan);
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
                "[DualPad][Snapshot] Sequence gap detected: expected {} got {}. Resetting injected state owners.",
                _lastProcessedSequence + 1,
                snapshot.firstSequence);
            ResetState();
        }

        const auto context = ContextManager::GetSingleton().GetCurrentContext();
        const auto& syntheticFrame = _stateReducer.Reduce(snapshot, context);
        LogSyntheticPadFrame(syntheticFrame);
        BeginPlanning(context);

        const auto handledButtons = DispatchPadEvents(snapshot.events, context);
        UpdateActiveButtonActions(syntheticFrame, context);
        _nativeStateBackend.SetRawAnalogState(
            syntheticFrame.leftStickX.value,
            syntheticFrame.leftStickY.value,
            syntheticFrame.rightStickX.value,
            syntheticFrame.rightStickY.value,
            syntheticFrame.leftTrigger.value,
            syntheticFrame.rightTrigger.value);
        FinishPlanning();

        if (_nativeInjector.ShouldUseAsMainPath() &&
            _nativeInjector.IsAvailable() &&
            !UsePollCommittedNativeState()) {
            // Legacy experimental fallback only. Plan A owns the primary
            // digital path once the producer-side Poll commit route is active.
            _nativeInjector.SubmitFrame(syntheticFrame, handledButtons);
        } else if (!UsePollCommittedNativeState()) {
            _compatibilityInjector.SubmitFrame(syntheticFrame, handledButtons);
        }

        _lastProcessedSequence = snapshot.sequence;
    }
}
