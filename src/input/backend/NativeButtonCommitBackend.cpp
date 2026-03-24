#include "pch.h"
#include "input/backend/NativeButtonCommitBackend.h"

#include "input/AuthoritativePollState.h"
#include "input/InputContext.h"
#include "input/PadProfile.h"
#include "input/RuntimeConfig.h"
#include "input/backend/ActionBackendPolicy.h"
#include "input/backend/NativeActionDescriptor.h"
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
        const auto* descriptor = FindNativeActionDescriptor(actionId);
        return descriptor != nullptr &&
            descriptor->backend == PlannedBackend::NativeButtonCommit &&
            descriptor->kind == PlannedActionKind::NativeButton &&
            descriptor->nativeCode != NativeControlCode::None &&
            ResolveVirtualPadBitMask(descriptor->virtualButtonRoles, GetPadBits(GetActivePadProfile())) != 0;
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
        return ResolveVirtualPadBitMask(code, GetPadBits(GetActivePadProfile()));
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
