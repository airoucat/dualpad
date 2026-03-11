#include "pch.h"
#include "input/injection/PadEventSnapshotProcessor.h"

#include "input/backend/FrameActionPlanDebugLogger.h"
#include "input/backend/KeyboardNativeBackend.h"
#include "input/injection/SyntheticStateDebugLogger.h"
#include "input/InputContext.h"
#include "input/PadProfile.h"
#include "input/RuntimeConfig.h"

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        bool IsLifecycleAction(std::string_view actionId)
        {
            return actionId == actions::Sprint ||
                actionId == actions::Activate ||
                actionId == actions::Sneak ||
                actionId == actions::MenuScrollUp ||
                actionId == actions::MenuScrollDown ||
                actionId == actions::MenuPageUp ||
                actionId == actions::MenuPageDown;
        }

        bool IsSprintObservationEnabled()
        {
            return RuntimeConfig::GetSingleton().LogSprintObservation();
        }

        std::uint64_t HoldLogBucket(float heldSeconds)
        {
            return static_cast<std::uint64_t>(heldSeconds / 0.25f);
        }

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
        _rawSprintObservedDown = false;
        _rawSprintHoldBucket = std::numeric_limits<std::uint64_t>::max();
        _stateReducer.Reset();
        _compatibilityInjector.Reset();
        _nativeInjector.Reset();
        backend::KeyboardNativeBackend::GetSingleton().Reset();
        ResetShadowPlanning();
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
        std::uint32_t handledButtons = _blockedSourceButtons;
        for (std::size_t i = 0; i < events.count; ++i) {
            const auto& event = events[i];
            _actionDispatcher.DispatchDirectPadEvent(event);

            if (event.type == PadEventType::ButtonRelease &&
                IsSyntheticPadBitCode(event.code) &&
                (_blockedSourceButtons & event.code) != 0) {
                handledButtons |= event.code;
                _blockedSourceButtons &= ~event.code;
            }

            const auto resolved = _bindingResolver.Resolve(event, context);
            if (!resolved) {
                continue;
            }

            if (event.type == PadEventType::ButtonPress &&
                IsSyntheticPadBitCode(event.code) &&
                IsLifecycleAction(resolved->actionId)) {
                auto& activeAction = _activeButtonActions[BitIndex(event.code)];
                activeAction.active = true;
                activeAction.actionId = resolved->actionId;
                activeAction.lastObservedHoldBucket = std::numeric_limits<std::uint64_t>::max();
                _blockedSourceButtons |= event.code;
                handledButtons |= event.code;

                if (IsSprintObservationEnabled() && resolved->actionId == actions::Sprint) {
                    logger::info(
                        "[DualPad][Sprint] Registered lifecycle source=0x{:08X} context={}",
                        event.code,
                        ToString(context));
                }

                continue;
            }

            _shadowPlanner.PlanResolvedEvent(*resolved, event, context, _shadowPlan);

            const auto dispatchResult = _actionDispatcher.Dispatch(
                resolved->actionId,
                context);
            if (!dispatchResult.handled) {
                continue;
            }

            logger::info("[DualPad][Mapping] Event {} mapped to action {} via {}",
                ToString(event.type),
                resolved->actionId,
                ToString(dispatchResult.target));

            if (event.type == PadEventType::ButtonPress &&
                IsSyntheticPadBitCode(event.code)) {
                _blockedSourceButtons |= event.code;
                handledButtons |= event.code;
            }

            if (IsSprintObservationEnabled() &&
                resolved->actionId == actions::Sprint) {
                logger::info(
                    "[DualPad][Sprint] Routed event={} source=0x{:08X} context={} via {}",
                    ToString(event.type),
                    event.code,
                    ToString(context),
                    ToString(dispatchResult.target));
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
        for (std::size_t bitIndex = 0; bitIndex < _activeButtonActions.size(); ++bitIndex) {
            auto& activeAction = _activeButtonActions[bitIndex];
            if (!activeAction.active) {
                continue;
            }

            const auto sourceCode = (1u << bitIndex);
            const auto& button = frame.buttons[bitIndex];

            if (button.down) {
                _shadowPlanner.PlanButtonState(
                    activeAction.actionId,
                    true,
                    button.pressed ? 0.0f : button.heldSeconds,
                    sourceCode,
                    context,
                    _shadowPlan);

                const auto dispatchResult = _actionDispatcher.DispatchButtonState(
                    activeAction.actionId,
                    true,
                    button.pressed ? 0.0f : button.heldSeconds,
                    context);
                if (!dispatchResult.handled) {
                    continue;
                }

                if (IsSprintObservationEnabled() &&
                    activeAction.actionId == actions::Sprint) {
                    if (button.pressed) {
                        ObserveSprintState("press", frame, button, dispatchResult.target, sourceCode);
                        activeAction.lastObservedHoldBucket = HoldLogBucket(button.heldSeconds);
                    }
                    else {
                        const auto bucket = HoldLogBucket(button.heldSeconds);
                        if (bucket != activeAction.lastObservedHoldBucket) {
                            ObserveSprintState("hold", frame, button, dispatchResult.target, sourceCode);
                            activeAction.lastObservedHoldBucket = bucket;
                        }
                    }
                }

                continue;
            }

            const auto heldSeconds = button.released && button.releasedAtUs > button.pressedAtUs && button.pressedAtUs != 0 ?
                static_cast<float>(button.releasedAtUs - button.pressedAtUs) / 1000000.0f :
                button.heldSeconds;
            _shadowPlanner.PlanButtonState(
                activeAction.actionId,
                false,
                heldSeconds,
                sourceCode,
                context,
                _shadowPlan);
            const auto dispatchResult = _actionDispatcher.DispatchButtonState(
                activeAction.actionId,
                false,
                heldSeconds,
                context);

            if (dispatchResult.handled &&
                IsSprintObservationEnabled() &&
                activeAction.actionId == actions::Sprint) {
                ObserveSprintState("release", frame, button, dispatchResult.target, sourceCode);
            }

            _blockedSourceButtons &= ~sourceCode;
            activeAction = {};
        }
    }

    void PadEventSnapshotProcessor::ObserveSprintState(
        std::string_view phase,
        const SyntheticPadFrame& frame,
        const SyntheticButtonState& button,
        ActionDispatchTarget target,
        std::uint32_t sourceCode) const
    {
        logger::info(
            "[DualPad][Sprint] phase={} seq={} context={} source=0x{:08X} route={} down={} pressed={} released={} held={:.3f} blocked=0x{:08X}",
            phase,
            frame.sequence,
            ToString(frame.context),
            sourceCode,
            ToString(target),
            button.down,
            button.pressed,
            button.released,
            button.heldSeconds,
            _blockedSourceButtons);
    }

    void PadEventSnapshotProcessor::ObserveRawSprintCompatibilityState(
        const SyntheticPadFrame& frame,
        std::uint32_t handledButtons)
    {
        if (!IsSprintObservationEnabled()) {
            return;
        }

        const auto sprintBit = GetPadBits(GetActivePadProfile()).sprint;
        if (sprintBit == 0 || !std::has_single_bit(sprintBit)) {
            return;
        }

        const auto bitIndex = BitIndex(sprintBit);
        const auto& button = frame.buttons[bitIndex];
        const bool blocked = (handledButtons & sprintBit) != 0;

        if (blocked) {
            if (_rawSprintObservedDown) {
                logger::info(
                    "[DualPad][SprintRaw] phase=blocked-release route=Blocked seq={} context={} source=0x{:08X} down={} pressed={} released={} held={:.3f}",
                    frame.sequence,
                    ToString(frame.context),
                    sprintBit,
                    button.down,
                    button.pressed,
                    button.released,
                    button.heldSeconds);
            }

            _rawSprintObservedDown = false;
            _rawSprintHoldBucket = std::numeric_limits<std::uint64_t>::max();
            return;
        }

        if (button.down) {
            if (!_rawSprintObservedDown || button.pressed) {
                logger::info(
                    "[DualPad][SprintRaw] phase=press route=Compatibility seq={} context={} source=0x{:08X} down={} pressed={} released={} held={:.3f}",
                    frame.sequence,
                    ToString(frame.context),
                    sprintBit,
                    button.down,
                    button.pressed,
                    button.released,
                    button.heldSeconds);
                _rawSprintHoldBucket = HoldLogBucket(button.heldSeconds);
            }
            else {
                const auto bucket = HoldLogBucket(button.heldSeconds);
                if (bucket != _rawSprintHoldBucket) {
                    logger::info(
                        "[DualPad][SprintRaw] phase=hold route=Compatibility seq={} context={} source=0x{:08X} down={} pressed={} released={} held={:.3f}",
                        frame.sequence,
                        ToString(frame.context),
                        sprintBit,
                        button.down,
                        button.pressed,
                        button.released,
                        button.heldSeconds);
                    _rawSprintHoldBucket = bucket;
                }
            }

            _rawSprintObservedDown = true;
            return;
        }

        if (_rawSprintObservedDown || button.released) {
            logger::info(
                "[DualPad][SprintRaw] phase=release route=Compatibility seq={} context={} source=0x{:08X} down={} pressed={} released={} held={:.3f}",
                frame.sequence,
                ToString(frame.context),
                sprintBit,
                button.down,
                button.pressed,
                button.released,
                button.heldSeconds);
        }

        _rawSprintObservedDown = false;
        _rawSprintHoldBucket = std::numeric_limits<std::uint64_t>::max();
    }

    void PadEventSnapshotProcessor::ResetShadowPlanning()
    {
        _shadowPlan.Clear();
        _shadowNativeState.Reset();
    }

    void PadEventSnapshotProcessor::BeginShadowPlanning(InputContext context)
    {
        _shadowPlan.Clear();
        _shadowNativeState.BeginFrame(context);
    }

    void PadEventSnapshotProcessor::FinishShadowPlanning()
    {
        _shadowNativeState.ApplyPlan(_shadowPlan);
        backend::LogFrameActionPlan(_shadowPlan);
        backend::LogVirtualGamepadState(_shadowNativeState.GetState());
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
        BeginShadowPlanning(context);

        const auto handledButtons = DispatchPadEvents(snapshot.events, context);
        ObserveRawSprintCompatibilityState(syntheticFrame, handledButtons);
        UpdateActiveButtonActions(syntheticFrame, context);
        FinishShadowPlanning();
        if (_nativeInjector.ShouldUseAsMainPath() &&
            _nativeInjector.IsAvailable()) {
            _nativeInjector.SubmitFrame(syntheticFrame, handledButtons);
        }
        else {
            // Stable compatibility path until native button injection moves to a
            // pre-ControlMap/consumer hook instead of the current input-pump sink.
            _compatibilityInjector.SubmitFrame(syntheticFrame, handledButtons);
        }

        _lastProcessedSequence = snapshot.sequence;
    }
}
