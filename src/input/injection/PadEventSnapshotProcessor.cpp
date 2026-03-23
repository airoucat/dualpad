#include "pch.h"
#include "input/injection/PadEventSnapshotProcessor.h"

#include "input/backend/ActionBackendPolicy.h"
#include "input/backend/NativeButtonCommitBackend.h"
#include "input/backend/FrameActionPlanDebugLogger.h"
#include "input/backend/KeyboardHelperBackend.h"
#include "input/AuthoritativePollState.h"
#include "input/BindingManager.h"
#include "input/InputContext.h"
#include "input/Trigger.h"
#include "input/RuntimeConfig.h"
#include "input/injection/SyntheticStateDebugLogger.h"

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        struct PlannedAnalogState
        {
            float moveX{ 0.0f };
            float moveY{ 0.0f };
            float lookX{ 0.0f };
            float lookY{ 0.0f };
            float leftTrigger{ 0.0f };
            float rightTrigger{ 0.0f };
            bool hasAnalog{ false };
        };

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

        void PublishUnmanagedDigitalState(
            const SyntheticPadFrame& frame,
            std::uint32_t handledButtons)
        {
            auto& authoritativeState = AuthoritativePollState::GetSingleton();

            if (handledButtons != 0) {
                // Hold/Tap bindings may resolve after the original press already
                // entered the unmanaged raw mask. Clear them explicitly so the
                // mapped action does not keep leaking through to Skyrim.
                authoritativeState.SetUnmanagedButton(handledButtons, false);
            }

            const auto filteredPulse = frame.pulseMask & ~handledButtons;
            const auto filteredPressed = frame.pressedMask & ~handledButtons;
            const auto filteredReleased = frame.releasedMask & ~handledButtons;

            authoritativeState.PublishUnmanagedDigitalEdges(
                filteredPressed,
                filteredReleased,
                filteredPulse);

            if (filteredPulse != 0) {
                authoritativeState.PulseUnmanagedButton(filteredPulse);
                logger::info(
                    "[DualPad][RawDigital] Pulsed unmanaged raw buttons 0x{:08X} for transient press-release",
                    filteredPulse);
            }

            if (filteredPressed != 0) {
                authoritativeState.SetUnmanagedButton(filteredPressed, true);
            }

            if (filteredReleased != 0) {
                authoritativeState.SetUnmanagedButton(filteredReleased, false);
            }
        }

        std::optional<std::string> ResolveAxisAction(PadAxisId axis, InputContext context)
        {
            Trigger trigger{};
            trigger.type = TriggerType::Axis;
            trigger.code = static_cast<std::uint32_t>(axis);
            return BindingManager::GetSingleton().GetActionForTrigger(trigger, context);
        }

        void ApplyAxisBinding(
            PlannedAnalogState& analog,
            PadAxisId axis,
            float value,
            InputContext context)
        {
            const auto actionId = ResolveAxisAction(axis, context);
            if (!actionId) {
                return;
            }

            const auto decision = backend::ActionBackendPolicy::Decide(*actionId);
            if (decision.backend != backend::PlannedBackend::NativeState) {
                return;
            }

            switch (decision.nativeCode) {
            case backend::NativeControlCode::MoveStick:
            case backend::NativeControlCode::MenuStick:
            case backend::NativeControlCode::FavoritesLeftStick:
            case backend::NativeControlCode::MapCursorStick:
            case backend::NativeControlCode::StatsRotateStick:
            case backend::NativeControlCode::DebugMapMoveStick:
            case backend::NativeControlCode::LockpickingRotatePickStick:
            case backend::NativeControlCode::CreationsLeftStick:
                if (axis == PadAxisId::LeftStickX) {
                    analog.moveX = value;
                    analog.hasAnalog = true;
                } else if (axis == PadAxisId::LeftStickY) {
                    analog.moveY = value;
                    analog.hasAnalog = true;
                }
                break;

            case backend::NativeControlCode::LookStick:
            case backend::NativeControlCode::ItemRotateStick:
            case backend::NativeControlCode::MapLookStick:
            case backend::NativeControlCode::CursorMoveStick:
            case backend::NativeControlCode::DebugMapLookStick:
            case backend::NativeControlCode::LockpickingRotateLockStick:
                if (axis == PadAxisId::RightStickX) {
                    analog.lookX = value;
                    analog.hasAnalog = true;
                } else if (axis == PadAxisId::RightStickY) {
                    analog.lookY = value;
                    analog.hasAnalog = true;
                }
                break;

            case backend::NativeControlCode::LeftTriggerAxis:
            case backend::NativeControlCode::ItemLeftEquipTrigger:
            case backend::NativeControlCode::MapZoomOutTrigger:
            case backend::NativeControlCode::JournalTabLeftTrigger:
            case backend::NativeControlCode::DebugOverlayLeftTrigger:
            case backend::NativeControlCode::TFCCameraZDownTrigger:
            case backend::NativeControlCode::DebugMapZoomOutTrigger:
            case backend::NativeControlCode::CreationsCategorySideBarTrigger:
                if (axis == PadAxisId::LeftTrigger) {
                    analog.leftTrigger = value;
                    analog.hasAnalog = true;
                }
                break;

            case backend::NativeControlCode::RightTriggerAxis:
            case backend::NativeControlCode::ItemRightEquipTrigger:
            case backend::NativeControlCode::MapZoomInTrigger:
            case backend::NativeControlCode::JournalTabRightTrigger:
            case backend::NativeControlCode::DebugOverlayRightTrigger:
            case backend::NativeControlCode::TFCCameraZUpTrigger:
            case backend::NativeControlCode::DebugMapZoomInTrigger:
            case backend::NativeControlCode::CreationsFilterTrigger:
                if (axis == PadAxisId::RightTrigger) {
                    analog.rightTrigger = value;
                    analog.hasAnalog = true;
                }
                break;

            case backend::NativeControlCode::None:
            case backend::NativeControlCode::Jump:
            case backend::NativeControlCode::Activate:
            case backend::NativeControlCode::ReadyWeapon:
            case backend::NativeControlCode::TweenMenu:
            case backend::NativeControlCode::Sprint:
            case backend::NativeControlCode::Sneak:
            case backend::NativeControlCode::Shout:
            case backend::NativeControlCode::FavoritesCombo:
            case backend::NativeControlCode::Hotkey1:
            case backend::NativeControlCode::Hotkey2:
            case backend::NativeControlCode::TogglePOV:
            case backend::NativeControlCode::Wait:
            case backend::NativeControlCode::Journal:
            case backend::NativeControlCode::MenuConfirm:
            case backend::NativeControlCode::MenuCancel:
            case backend::NativeControlCode::MenuScrollUp:
            case backend::NativeControlCode::MenuScrollDown:
            case backend::NativeControlCode::MenuLeft:
            case backend::NativeControlCode::MenuRight:
            case backend::NativeControlCode::MenuDownloadAll:
            case backend::NativeControlCode::MenuPageUp:
            case backend::NativeControlCode::MenuPageDown:
            case backend::NativeControlCode::MenuSortByName:
            case backend::NativeControlCode::MenuSortByValue:
            case backend::NativeControlCode::DialoguePreviousOption:
            case backend::NativeControlCode::DialogueNextOption:
            case backend::NativeControlCode::FavoritesPreviousItem:
            case backend::NativeControlCode::FavoritesNextItem:
            case backend::NativeControlCode::FavoritesAccept:
            case backend::NativeControlCode::FavoritesCancel:
            case backend::NativeControlCode::FavoritesUp:
            case backend::NativeControlCode::FavoritesDown:
            case backend::NativeControlCode::ConsoleExecute:
            case backend::NativeControlCode::ConsoleHistoryUp:
            case backend::NativeControlCode::ConsoleHistoryDown:
            case backend::NativeControlCode::ConsolePickPrevious:
            case backend::NativeControlCode::ConsolePickNext:
            case backend::NativeControlCode::ConsoleNextFocus:
            case backend::NativeControlCode::ConsolePreviousFocus:
            case backend::NativeControlCode::ItemZoom:
            case backend::NativeControlCode::ItemXButton:
            case backend::NativeControlCode::ItemYButton:
            case backend::NativeControlCode::InventoryChargeItem:
            case backend::NativeControlCode::BookClose:
            case backend::NativeControlCode::BookPreviousPage:
            case backend::NativeControlCode::BookNextPage:
            case backend::NativeControlCode::MapCancel:
            case backend::NativeControlCode::MapClick:
            case backend::NativeControlCode::MapOpenJournal:
            case backend::NativeControlCode::MapPlayerPosition:
            case backend::NativeControlCode::MapLocalMap:
            case backend::NativeControlCode::CursorClick:
            case backend::NativeControlCode::JournalXButton:
            case backend::NativeControlCode::JournalYButton:
            case backend::NativeControlCode::DebugOverlayNextFocus:
            case backend::NativeControlCode::DebugOverlayPreviousFocus:
            case backend::NativeControlCode::DebugOverlayUp:
            case backend::NativeControlCode::DebugOverlayDown:
            case backend::NativeControlCode::DebugOverlayLeft:
            case backend::NativeControlCode::DebugOverlayRight:
            case backend::NativeControlCode::DebugOverlayToggleMinimize:
            case backend::NativeControlCode::DebugOverlayToggleMove:
            case backend::NativeControlCode::DebugOverlayB:
            case backend::NativeControlCode::DebugOverlayY:
            case backend::NativeControlCode::DebugOverlayX:
            case backend::NativeControlCode::TFCWorldZUp:
            case backend::NativeControlCode::TFCWorldZDown:
            case backend::NativeControlCode::TFCLockToZPlane:
            case backend::NativeControlCode::LockpickingDebugMode:
            case backend::NativeControlCode::LockpickingCancel:
            case backend::NativeControlCode::CreationsAccept:
            case backend::NativeControlCode::CreationsCancel:
            case backend::NativeControlCode::CreationsUp:
            case backend::NativeControlCode::CreationsDown:
            case backend::NativeControlCode::CreationsLeft:
            case backend::NativeControlCode::CreationsRight:
            case backend::NativeControlCode::CreationsOptions:
            case backend::NativeControlCode::CreationsLoadOrderAndDelete:
            case backend::NativeControlCode::CreationsLikeUnlike:
            case backend::NativeControlCode::CreationsSearchEdit:
            case backend::NativeControlCode::CreationsPurchaseCredits:
            case backend::NativeControlCode::FavorCancel:
            default:
                break;
            }
        }

        PlannedAnalogState CollectBoundAnalogState(const SyntheticPadFrame& frame, InputContext context)
        {
            PlannedAnalogState analog{};
            ApplyAxisBinding(analog, PadAxisId::LeftStickX, frame.leftStickX.value, context);
            ApplyAxisBinding(analog, PadAxisId::LeftStickY, frame.leftStickY.value, context);
            ApplyAxisBinding(analog, PadAxisId::RightStickX, frame.rightStickX.value, context);
            ApplyAxisBinding(analog, PadAxisId::RightStickY, frame.rightStickY.value, context);
            ApplyAxisBinding(analog, PadAxisId::LeftTrigger, frame.leftTrigger.value, context);
            ApplyAxisBinding(analog, PadAxisId::RightTrigger, frame.rightTrigger.value, context);
            return analog;
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
        _lastProcessedSequence = 0;
        _stateReducer.Reset();
        AuthoritativePollState::GetSingleton().Reset();
        _lifecycleCoordinator.Reset();
        _sourceBlockCoordinator.Reset();
        backend::NativeButtonCommitBackend::GetSingleton().Reset();
        backend::KeyboardHelperBackend::GetSingleton().Reset();
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
    }

    void PadEventSnapshotProcessor::BeginFramePlanning(InputContext context)
    {
        _framePlan.Clear();
        backend::NativeButtonCommitBackend::GetSingleton().BeginFrame(
            context,
            ContextManager::GetSingleton().GetCurrentEpoch());
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
        AuthoritativePollState::GetSingleton().PublishFrameMetadata(
            syntheticFrame.sourceTimestampUs,
            syntheticFrame.overflowed,
            syntheticFrame.coalesced);
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
        FinishFramePlanning(syntheticFrame, context);
        // Poll-owned digital actions now commit through NativeButtonCommitBackend.
        // Reduced raw edges still publish unmanaged digital facts here so the
        // authoritative poll state remains the single output contract.
        PublishUnmanagedDigitalState(syntheticFrame, handledButtons);

        _lastProcessedSequence = snapshot.sequence;
    }
}

