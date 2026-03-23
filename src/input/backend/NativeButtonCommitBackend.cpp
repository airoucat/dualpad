#include "pch.h"
#include "input/backend/NativeButtonCommitBackend.h"

#include "input/Action.h"
#include "input/AuthoritativePollState.h"
#include "input/InputContext.h"
#include "input/PadProfile.h"
#include "input/RuntimeConfig.h"
#include "input/backend/ActionBackendPolicy.h"
#include "input/injection/UpstreamGamepadHook.h"

#include <chrono>

namespace logger = SKSE::log;

namespace dualpad::input::backend
{
    NativeButtonCommitBackend& NativeButtonCommitBackend::GetSingleton()
    {
        static NativeButtonCommitBackend instance;
        return instance;
    }

    void NativeButtonCommitBackend::Reset()
    {
        std::scoped_lock lock(_lock);
        _pollCommit.Reset();
        _frameContext = InputContext::Gameplay;
        _frameContextEpoch = 0;
        _pollSequence = 0;
        _lastCommittedButtonDownMask = 0;
    }

    bool NativeButtonCommitBackend::IsRouteActive() const
    {
        return UpstreamGamepadHook::GetSingleton().IsRouteActive();
    }

    bool NativeButtonCommitBackend::CanHandleAction(std::string_view actionId) const
    {
        const auto decision = ActionBackendPolicy::Decide(actionId);
        return decision.backend == PlannedBackend::NativeButtonCommit &&
            decision.kind == PlannedActionKind::NativeButton &&
            decision.nativeCode != NativeControlCode::None &&
            ToVirtualPadBit(decision.nativeCode) != 0;
    }

    bool NativeButtonCommitBackend::TriggerAction(
        std::string_view actionId,
        ActionOutputContract contract,
        InputContext context)
    {
        std::scoped_lock lock(_lock);
        return SubmitSyntheticActionLocked(
            actionId,
            contract,
            PlannedActionPhase::Pulse,
            0.0f,
            context);
    }

    bool NativeButtonCommitBackend::SubmitActionState(
        std::string_view actionId,
        ActionOutputContract contract,
        bool pressed,
        float heldSeconds,
        InputContext context)
    {
        std::scoped_lock lock(_lock);
        return SubmitSyntheticActionLocked(
            actionId,
            contract,
            pressed ? (heldSeconds > 0.0f ? PlannedActionPhase::Hold : PlannedActionPhase::Press) : PlannedActionPhase::Release,
            heldSeconds,
            context);
    }

    void NativeButtonCommitBackend::BeginFrame(
        InputContext context,
        std::uint32_t contextEpoch,
        std::uint64_t nowUs)
    {
        std::scoped_lock lock(_lock);
        _frameContext = context;
        _frameContextEpoch = contextEpoch;
        _pollCommit.BeginFrame(context, contextEpoch, nowUs != 0 ? nowUs : NowUs());
    }

    bool NativeButtonCommitBackend::ApplyPlannedAction(const PlannedAction& action)
    {
        std::scoped_lock lock(_lock);
        PollCommitRequest request{};
        if (!TranslatePlannedActionToCommitRequest(action, request)) {
            return false;
        }
        return _pollCommit.QueueRequest(request);
    }

    CommittedButtonState NativeButtonCommitBackend::CommitPollState()
    {
        if (!IsRouteActive()) {
            return {};
        }

        std::scoped_lock lock(_lock);

        const auto nowUs = NowUs();
        const auto context = ContextManager::GetSingleton().GetCurrentContext();
        const auto contextEpoch = ContextManager::GetSingleton().GetCurrentEpoch();
        _frameContext = context;
        _frameContextEpoch = contextEpoch;

        _pollCommit.BeginFrame(context, contextEpoch, nowUs);
        _pollCommit.Tick(nowUs, IsGameplayGateOpen(context));
        _pollCommit.Flush(*this, nowUs);

        CommittedButtonState result{};
        result.context = context;
        result.contextEpoch = contextEpoch;
        result.pollSequence = ++_pollSequence;

        for (const auto& slot : _pollCommit.Slots()) {
            if (slot.actionId.empty()) {
                continue;
            }

            const auto buttonBit = ToVirtualPadBit(slot.outputCode);
            if (buttonBit == 0) {
                continue;
            }

            if (SlotIsManaged(slot)) {
                result.managedMask |= buttonBit;
            }
            if (SlotIsDown(slot)) {
                result.buttonDownMask |= buttonBit;
            }

            if (ShouldLogPollCommit() && SlotIsManaged(slot)) {
                logger::info(
                    "[DualPad][NativeButtonCommit] poll={} context={} action={} code={} execState={} commitMode={} managed={} down={} epoch={} token={} nextPulse={} desiredHeld={} downCount={} upCount={}",
                    result.pollSequence,
                    ToString(slot.context),
                    slot.actionId.c_str(),
                    ToString(slot.outputCode),
                    ToString(slot.state),
                    ToString(slot.mode),
                    SlotIsManaged(slot),
                    SlotIsDown(slot),
                    slot.epoch,
                    slot.token.tokenId,
                    slot.pending.pendingNextPulse,
                    slot.desiredHeld,
                    slot.emittedDownCount,
                    slot.emittedUpCount);
            }
        }

        result.buttonPressedMask = result.buttonDownMask & ~_lastCommittedButtonDownMask;
        result.buttonReleasedMask = _lastCommittedButtonDownMask & ~result.buttonDownMask;
        _lastCommittedButtonDownMask = result.buttonDownMask;

        AuthoritativePollState::GetSingleton().PublishCommittedButtons(
            result.buttonDownMask,
            result.buttonPressedMask,
            result.buttonReleasedMask,
            result.managedMask,
            result.pollSequence,
            result.context,
            result.contextEpoch);

        return result;
    }

    EmitResult NativeButtonCommitBackend::Emit(const EmitRequest& request)
    {
        const auto decision = ActionBackendPolicy::Decide(request.actionId);
        if (decision.backend != PlannedBackend::NativeButtonCommit ||
            decision.kind != PlannedActionKind::NativeButton ||
            decision.nativeCode == NativeControlCode::None ||
            ToVirtualPadBit(decision.nativeCode) == 0) {
            return {};
        }

        if (ShouldLogPollCommit()) {
            logger::info(
                "[DualPad][NativeButtonCommit] emit action={} edge={} epoch={} token={} context={} held={:.3f}",
                request.actionId.c_str(),
                request.edge == EmitEdge::Down ? "Down" : "Up",
                request.epoch,
                request.tokenId,
                ToString(request.context),
                request.heldSeconds);
        }

        // In the poll-owned mainline this is a commit-FSM acknowledgement, not
        // direct BSInputEvent queue injection. Gameplay-visible state is
        // materialized later by CommitPollState() exporting the committed
        // virtual button current-state.
        return {
            .submitted = true,
            .queueFull = false,
            .transientBlocked = false
        };
    }

    bool NativeButtonCommitBackend::SubmitSyntheticActionLocked(
        std::string_view actionId,
        ActionOutputContract contract,
        PlannedActionPhase phase,
        float heldSeconds,
        InputContext context)
    {
        // Legacy bridge path for IActionLifecycleBackend callers. The primary
        // mainline is ApplyPlannedAction(), which consumes planner-owned
        // metadata directly. Keep the defaults here aligned with planner
        // values, but do not treat this method as the canonical metadata source.
        const auto decision = ActionBackendPolicy::Decide(actionId);
        if (decision.backend != PlannedBackend::NativeButtonCommit ||
            decision.kind != PlannedActionKind::NativeButton ||
            decision.nativeCode == NativeControlCode::None ||
            decision.contract != contract) {
            return false;
        }

        PlannedAction action{};
        action.backend = decision.backend;
        action.kind = decision.kind;
        action.phase = phase;
        action.context = context;
        action.actionId = std::string(actionId);
        action.contract = contract;
        action.lifecyclePolicy = decision.lifecyclePolicy;
        action.outputCode = static_cast<std::uint32_t>(decision.nativeCode);
        action.heldSeconds = heldSeconds;
        action.timestampUs = NowUs();
        action.contextEpoch = _frameContextEpoch != 0 ?
            _frameContextEpoch :
            ContextManager::GetSingleton().GetCurrentEpoch();

        switch (decision.lifecyclePolicy) {
        case ActionLifecyclePolicy::HoldOwner:
            action.digitalPolicy = NativeDigitalPolicyKind::HoldOwner;
            break;
        case ActionLifecyclePolicy::RepeatOwner:
            action.digitalPolicy = NativeDigitalPolicyKind::RepeatOwner;
            action.repeatDelayMs = 350;
            action.repeatIntervalMs = 75;
            break;
        case ActionLifecyclePolicy::ToggleOwner:
            action.digitalPolicy = NativeDigitalPolicyKind::ToggleDebounced;
            break;
        case ActionLifecyclePolicy::DeferredPulse:
        case ActionLifecyclePolicy::MinDownWindowPulse:
            action.digitalPolicy = NativeDigitalPolicyKind::PulseMinDown;
            action.minDownMs = 40;
            break;
        case ActionLifecyclePolicy::AxisValue:
        case ActionLifecyclePolicy::None:
        default:
            action.digitalPolicy = NativeDigitalPolicyKind::None;
            break;
        }

        action.gateAware = actionId == actions::Jump ||
            actionId == actions::Activate ||
            actionId == actions::Sprint;

        PollCommitRequest request{};
        if (!TranslatePlannedActionToCommitRequest(action, request)) {
            return false;
        }

        return _pollCommit.QueueRequest(request);
    }

    bool NativeButtonCommitBackend::TranslatePlannedActionToCommitRequest(
        const PlannedAction& action,
        PollCommitRequest& outRequest)
    {
        if (action.backend != PlannedBackend::NativeButtonCommit ||
            action.kind != PlannedActionKind::NativeButton ||
            action.digitalPolicy == NativeDigitalPolicyKind::None ||
            action.outputCode == 0) {
            return false;
        }

        outRequest.actionId = RE::BSFixedString(action.actionId.c_str());
        outRequest.context = action.context;
        outRequest.outputCode = static_cast<NativeControlCode>(action.outputCode);
        outRequest.gateAware = action.gateAware;
        outRequest.epoch = action.contextEpoch;
        outRequest.timestampUs = action.timestampUs;
        outRequest.minDownMs = action.minDownMs;
        outRequest.repeatDelayMs = action.repeatDelayMs;
        outRequest.repeatIntervalMs = action.repeatIntervalMs;

        switch (action.digitalPolicy) {
        case NativeDigitalPolicyKind::PulseMinDown:
            if (action.phase == PlannedActionPhase::Pulse ||
                action.phase == PlannedActionPhase::Press) {
                outRequest.mode = PollCommitMode::Pulse;
                outRequest.kind = PollCommitRequestKind::Pulse;
                return true;
            }
            return false;

        case NativeDigitalPolicyKind::HoldOwner:
            outRequest.mode = PollCommitMode::Hold;
            if (action.phase == PlannedActionPhase::Release) {
                outRequest.kind = PollCommitRequestKind::HoldClear;
                return true;
            }
            if (action.phase == PlannedActionPhase::Press ||
                action.phase == PlannedActionPhase::Hold) {
                outRequest.kind = PollCommitRequestKind::HoldSet;
                return true;
            }
            return false;

        case NativeDigitalPolicyKind::RepeatOwner:
            outRequest.mode = PollCommitMode::Repeat;
            if (action.phase == PlannedActionPhase::Release) {
                outRequest.kind = PollCommitRequestKind::RepeatClear;
                return true;
            }
            if (action.phase == PlannedActionPhase::Press ||
                action.phase == PlannedActionPhase::Hold) {
                outRequest.kind = PollCommitRequestKind::RepeatSet;
                return true;
            }
            return false;

        case NativeDigitalPolicyKind::ToggleDebounced:
            if (action.phase == PlannedActionPhase::Pulse ||
                action.phase == PlannedActionPhase::Press) {
                outRequest.mode = PollCommitMode::Pulse;
                outRequest.kind = PollCommitRequestKind::Pulse;
                return true;
            }
            return false;

        case NativeDigitalPolicyKind::None:
        default:
            return false;
        }
    }

    std::uint64_t NativeButtonCommitBackend::NowUs()
    {
        using namespace std::chrono;
        return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
    }

    std::uint32_t NativeButtonCommitBackend::ToVirtualPadBit(NativeControlCode code)
    {
        const auto& bits = GetPadBits(GetActivePadProfile());
        switch (code) {
        case NativeControlCode::Jump:
        case NativeControlCode::MapPlayerPosition:
        case NativeControlCode::JournalYButton:
        case NativeControlCode::DebugOverlayY:
        case NativeControlCode::MenuDownloadAll:
        case NativeControlCode::CreationsLoadOrderAndDelete:
            return bits.triangle;

        case NativeControlCode::Activate:
        case NativeControlCode::MenuConfirm:
        case NativeControlCode::MapClick:
        case NativeControlCode::ConsoleExecute:
        case NativeControlCode::FavoritesAccept:
        case NativeControlCode::CursorClick:
        case NativeControlCode::CreationsAccept:
            return bits.cross;

        case NativeControlCode::ReadyWeapon:
        case NativeControlCode::MapLocalMap:
        case NativeControlCode::ItemXButton:
        case NativeControlCode::DebugOverlayX:
        case NativeControlCode::TFCLockToZPlane:
        case NativeControlCode::LockpickingDebugMode:
        case NativeControlCode::CreationsPurchaseCredits:
            return bits.square;

        case NativeControlCode::TweenMenu:
        case NativeControlCode::MenuCancel:
        case NativeControlCode::BookClose:
        case NativeControlCode::MapCancel:
        case NativeControlCode::FavoritesCancel:
        case NativeControlCode::DebugOverlayB:
        case NativeControlCode::LockpickingCancel:
        case NativeControlCode::CreationsCancel:
        case NativeControlCode::FavorCancel:
            return bits.circle;

        case NativeControlCode::Sprint:
        case NativeControlCode::MenuPageUp:
        case NativeControlCode::MenuSortByName:
        case NativeControlCode::ConsolePreviousFocus:
        case NativeControlCode::DebugOverlayPreviousFocus:
        case NativeControlCode::TFCWorldZDown:
        case NativeControlCode::CreationsSearchEdit:
            return bits.l1;

        case NativeControlCode::Sneak:
            return bits.l3;

        case NativeControlCode::Shout:
        case NativeControlCode::MenuPageDown:
        case NativeControlCode::MenuSortByValue:
        case NativeControlCode::InventoryChargeItem:
        case NativeControlCode::ConsoleNextFocus:
        case NativeControlCode::DebugOverlayNextFocus:
        case NativeControlCode::TFCWorldZUp:
        case NativeControlCode::CreationsLikeUnlike:
            return bits.r1;

        case NativeControlCode::TogglePOV:
        case NativeControlCode::ItemZoom:
        case NativeControlCode::ItemYButton:
        case NativeControlCode::DebugOverlayToggleMove:
            return bits.r3;

        case NativeControlCode::MenuScrollUp:
        case NativeControlCode::DialoguePreviousOption:
        case NativeControlCode::FavoritesPreviousItem:
        case NativeControlCode::FavoritesUp:
        case NativeControlCode::ConsolePickNext:
        case NativeControlCode::ConsoleHistoryUp:
        case NativeControlCode::DebugOverlayUp:
        case NativeControlCode::CreationsUp:
            return bits.dpadUp;

        case NativeControlCode::MenuScrollDown:
        case NativeControlCode::DialogueNextOption:
        case NativeControlCode::FavoritesNextItem:
        case NativeControlCode::FavoritesDown:
        case NativeControlCode::ConsolePickPrevious:
        case NativeControlCode::ConsoleHistoryDown:
        case NativeControlCode::DebugOverlayDown:
        case NativeControlCode::CreationsDown:
            return bits.dpadDown;

        case NativeControlCode::FavoritesCombo:
            return bits.dpadUp;

        case NativeControlCode::Hotkey1:
        case NativeControlCode::MenuLeft:
        case NativeControlCode::BookPreviousPage:
        case NativeControlCode::MapOpenJournal:
        case NativeControlCode::DebugOverlayLeft:
        case NativeControlCode::CreationsLeft:
            return bits.dpadLeft;

        case NativeControlCode::Hotkey2:
        case NativeControlCode::MenuRight:
        case NativeControlCode::BookNextPage:
        case NativeControlCode::DebugOverlayRight:
        case NativeControlCode::CreationsRight:
            return bits.dpadRight;

        // Keyboard-exclusive native events exposed through a dedicated
        // controlmap combo profile. These are intentionally not default
        // bindings; actionId -> combo ABI stays stable, while physical pad
        // bindings remain configurable in the mapping layer.
        case NativeControlCode::Hotkey3:
            return bits.l1 | bits.create;
        case NativeControlCode::Hotkey4:
            return bits.l1 | bits.dpadUp;
        case NativeControlCode::Hotkey5:
            return bits.l1 | bits.dpadLeft;
        case NativeControlCode::Hotkey6:
            return bits.l1 | bits.dpadDown;
        case NativeControlCode::Hotkey7:
            return bits.l1 | bits.dpadRight;
        case NativeControlCode::Hotkey8:
            return bits.l1 | bits.r1;

        case NativeControlCode::Wait:
        case NativeControlCode::DebugOverlayToggleMinimize:
            return bits.create;

        case NativeControlCode::Journal:
        case NativeControlCode::CreationsOptions:
            return bits.options;

        case NativeControlCode::Pause:
            return bits.circle | bits.triangle;
        case NativeControlCode::NativeScreenshot:
            return bits.circle | bits.r1;

        case NativeControlCode::None:
        case NativeControlCode::MoveStick:
        case NativeControlCode::LookStick:
        case NativeControlCode::MenuStick:
        case NativeControlCode::FavoritesLeftStick:
        case NativeControlCode::ItemLeftEquipTrigger:
        case NativeControlCode::ItemRightEquipTrigger:
        case NativeControlCode::ItemRotateStick:
        case NativeControlCode::MapLookStick:
        case NativeControlCode::MapZoomOutTrigger:
        case NativeControlCode::MapZoomInTrigger:
        case NativeControlCode::MapCursorStick:
        case NativeControlCode::StatsRotateStick:
        case NativeControlCode::CursorMoveStick:
        case NativeControlCode::JournalTabLeftTrigger:
        case NativeControlCode::JournalTabRightTrigger:
        case NativeControlCode::DebugOverlayLeftTrigger:
        case NativeControlCode::DebugOverlayRightTrigger:
        case NativeControlCode::TFCCameraZDownTrigger:
        case NativeControlCode::TFCCameraZUpTrigger:
        case NativeControlCode::DebugMapLookStick:
        case NativeControlCode::DebugMapZoomOutTrigger:
        case NativeControlCode::DebugMapZoomInTrigger:
        case NativeControlCode::DebugMapMoveStick:
        case NativeControlCode::LockpickingRotatePickStick:
        case NativeControlCode::LockpickingRotateLockStick:
        case NativeControlCode::CreationsLeftStick:
        case NativeControlCode::CreationsCategorySideBarTrigger:
        case NativeControlCode::CreationsFilterTrigger:
        case NativeControlCode::LeftTriggerAxis:
        case NativeControlCode::RightTriggerAxis:
        default:
            return 0;
        }
    }

    bool NativeButtonCommitBackend::ShouldLogPollCommit()
    {
        const auto& config = RuntimeConfig::GetSingleton();
        return config.LogActionPlan() || config.LogNativeInjection();
    }

    bool NativeButtonCommitBackend::IsGameplayGateOpen(InputContext context)
    {
        const auto value = static_cast<std::uint16_t>(context);
        return value < 100 || value >= 2000;
    }

    bool NativeButtonCommitBackend::SlotIsDown(const PollCommitSlot& slot)
    {
        return slot.token.active &&
            slot.token.downSubmitted &&
            !slot.token.releaseSubmitted;
    }

    bool NativeButtonCommitBackend::SlotIsManaged(const PollCommitSlot& slot)
    {
        return slot.state != ExecState::Idle ||
            slot.token.active ||
            slot.pending.kind != PendingKind::None ||
            slot.pending.pendingNextPulse ||
            slot.desiredHeld ||
            slot.toggledOn;
    }
}
