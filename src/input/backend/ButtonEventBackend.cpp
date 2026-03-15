#include "pch.h"
#include "input/backend/ButtonEventBackend.h"

#include "input/Action.h"
#include "input/InputContext.h"
#include "input/PadProfile.h"
#include "input/RuntimeConfig.h"
#include "input/backend/ActionBackendPolicy.h"
#include "input/injection/UpstreamGamepadHook.h"

#include <chrono>

namespace logger = SKSE::log;

namespace dualpad::input::backend
{
    ButtonEventBackend& ButtonEventBackend::GetSingleton()
    {
        static ButtonEventBackend instance;
        return instance;
    }

    void ButtonEventBackend::Reset()
    {
        std::scoped_lock lock(_lock);
        _pollCommit.Reset();
        _frameContext = InputContext::Gameplay;
        _frameContextEpoch = 0;
        _pollSequence = 0;
    }

    bool ButtonEventBackend::IsRouteActive() const
    {
        return UpstreamGamepadHook::GetSingleton().IsRouteActive();
    }

    bool ButtonEventBackend::CanHandleAction(std::string_view actionId) const
    {
        const auto decision = ActionBackendPolicy::Decide(actionId);
        return decision.backend == PlannedBackend::ButtonEvent &&
            decision.kind == PlannedActionKind::NativeButton &&
            decision.nativeCode != NativeControlCode::None &&
            ToSemanticPadBit(decision.nativeCode) != 0;
    }

    bool ButtonEventBackend::TriggerAction(
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

    bool ButtonEventBackend::SubmitActionState(
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

    void ButtonEventBackend::BeginFrame(
        InputContext context,
        std::uint32_t contextEpoch,
        std::uint64_t nowUs)
    {
        std::scoped_lock lock(_lock);
        _frameContext = context;
        _frameContextEpoch = contextEpoch;
        _pollCommit.BeginFrame(context, contextEpoch, nowUs != 0 ? nowUs : NowUs());
    }

    bool ButtonEventBackend::ApplyPlannedAction(const PlannedAction& action)
    {
        std::scoped_lock lock(_lock);
        PollCommitRequest request{};
        if (!TranslatePlannedActionToCommitRequest(action, request)) {
            return false;
        }
        return _pollCommit.QueueRequest(request);
    }

    PollCommittedButtonState ButtonEventBackend::CommitPollState()
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

        PollCommittedButtonState result{};
        result.pollSequence = ++_pollSequence;

        for (const auto& slot : _pollCommit.Slots()) {
            if (slot.actionId.empty()) {
                continue;
            }

            const auto semanticBit = ToSemanticPadBit(slot.outputCode);
            if (semanticBit == 0) {
                continue;
            }

            if (SlotIsManaged(slot)) {
                result.managedMask |= semanticBit;
            }
            if (SlotIsDown(slot)) {
                result.semanticDownMask |= semanticBit;
            }

            if (ShouldLogPollCommit() && SlotIsManaged(slot)) {
                logger::info(
                    "[DualPad][ButtonEvent] poll={} context={} action={} code={} execState={} commitMode={} managed={} down={} epoch={} token={} nextPulse={} desiredHeld={} downCount={} upCount={}",
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

        return result;
    }

    EmitResult ButtonEventBackend::Emit(const EmitRequest& request)
    {
        const auto decision = ActionBackendPolicy::Decide(request.actionId);
        if (decision.backend != PlannedBackend::ButtonEvent ||
            decision.kind != PlannedActionKind::NativeButton ||
            decision.nativeCode == NativeControlCode::None ||
            ToSemanticPadBit(decision.nativeCode) == 0) {
            return {};
        }

        if (ShouldLogPollCommit()) {
            logger::info(
                "[DualPad][ButtonEvent] emit action={} edge={} epoch={} token={} context={} held={:.3f}",
                request.actionId.c_str(),
                request.edge == EmitEdge::Down ? "Down" : "Up",
                request.epoch,
                request.tokenId,
                ToString(request.context),
                request.heldSeconds);
        }

        // In the poll-owned mainline this is a commit-FSM acknowledgement, not
        // direct BSInputEvent queue injection. Gameplay-visible state is
        // materialized later by CommitPollState() exporting semanticDownMask.
        return {
            .submitted = true,
            .queueFull = false,
            .transientBlocked = false
        };
    }

    bool ButtonEventBackend::SubmitSyntheticActionLocked(
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
        if (decision.backend != PlannedBackend::ButtonEvent ||
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

    bool ButtonEventBackend::TranslatePlannedActionToCommitRequest(
        const PlannedAction& action,
        PollCommitRequest& outRequest)
    {
        if (action.backend != PlannedBackend::ButtonEvent ||
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

    std::uint64_t ButtonEventBackend::NowUs()
    {
        using namespace std::chrono;
        return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
    }

    std::uint32_t ButtonEventBackend::ToSemanticPadBit(NativeControlCode code)
    {
        const auto& bits = GetPadBits(GetActivePadProfile());
        switch (code) {
        case NativeControlCode::Jump:
            return bits.triangle;
        case NativeControlCode::Activate:
        case NativeControlCode::MenuConfirm:
            return bits.cross;
        case NativeControlCode::Sprint:
        case NativeControlCode::MenuPageUp:
            return bits.l1;
        case NativeControlCode::Sneak:
            return bits.l3;
        case NativeControlCode::Shout:
            return bits.r1;
        case NativeControlCode::TogglePOV:
            return bits.r3;
        case NativeControlCode::MenuCancel:
            return bits.circle;
        case NativeControlCode::MenuScrollUp:
            return bits.dpadUp;
        case NativeControlCode::MenuScrollDown:
            return bits.dpadDown;
        case NativeControlCode::MenuLeft:
        case NativeControlCode::BookPreviousPage:
            return bits.dpadLeft;
        case NativeControlCode::MenuRight:
        case NativeControlCode::BookNextPage:
            return bits.dpadRight;
        case NativeControlCode::MenuPageDown:
            return bits.r1;
        case NativeControlCode::None:
        case NativeControlCode::Attack:
        case NativeControlCode::Block:
        case NativeControlCode::MoveStick:
        case NativeControlCode::LookStick:
        case NativeControlCode::MenuStick:
        case NativeControlCode::LeftTriggerAxis:
        case NativeControlCode::RightTriggerAxis:
        default:
            return 0;
        }
    }

    bool ButtonEventBackend::ShouldLogPollCommit()
    {
        const auto& config = RuntimeConfig::GetSingleton();
        return config.LogActionPlan() || config.LogNativeInjection();
    }

    bool ButtonEventBackend::IsGameplayGateOpen(InputContext context)
    {
        const auto value = static_cast<std::uint16_t>(context);
        return value < 100 || value >= 2000;
    }

    bool ButtonEventBackend::SlotIsDown(const PollCommitSlot& slot)
    {
        return slot.token.active &&
            slot.token.downSubmitted &&
            !slot.token.releaseSubmitted;
    }

    bool ButtonEventBackend::SlotIsManaged(const PollCommitSlot& slot)
    {
        return slot.state != ExecState::Idle ||
            slot.token.active ||
            slot.pending.kind != PendingKind::None ||
            slot.pending.pendingNextPulse ||
            slot.desiredHeld ||
            slot.toggledOn;
    }
}
