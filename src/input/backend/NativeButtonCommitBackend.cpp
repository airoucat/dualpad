#include "pch.h"
#include "input/backend/NativeButtonCommitBackend.h"

#include "input/Action.h"
#include "input/AuthoritativePollState.h"
#include "input/GameplayKbmFactTracker.h"
#include "input/InputContext.h"
#include "input/InputModalityTracker.h"
#include "input/PadProfile.h"
#include "input/RuntimeConfig.h"
#include "input/backend/ActionBackendPolicy.h"
#include "input/backend/NativeActionDescriptor.h"
#include "input/injection/GameplayOwnershipCoordinator.h"
#include "input/injection/UpstreamGamepadHook.h"

#include <chrono>

namespace logger = SKSE::log;

namespace dualpad::input::backend
{
    namespace
    {
        bool IsGameplayDigitalSuppressionCandidate(const PlannedAction& action)
        {
            if (action.context != InputContext::Gameplay) {
                return false;
            }
            if (!action.gateAware ||
                action.backend != PlannedBackend::NativeButtonCommit ||
                action.kind != PlannedActionKind::NativeButton) {
                return false;
            }

            switch (action.digitalPolicy) {
            case NativeDigitalPolicyKind::PulseMinDown:
                return action.phase == PlannedActionPhase::Pulse ||
                    action.phase == PlannedActionPhase::Press;
            case NativeDigitalPolicyKind::ToggleDebounced:
                return action.phase == PlannedActionPhase::Pulse ||
                    action.phase == PlannedActionPhase::Press;
            case NativeDigitalPolicyKind::HoldOwner:
            case NativeDigitalPolicyKind::RepeatOwner:
                // Hold/repeat actions need per-action handoff semantics.
                // Suppressing them at the whole-family DigitalOwner layer
                // breaks ownership transitions such as Sprint.
                return false;
            case NativeDigitalPolicyKind::None:
            default:
                return false;
            }
        }
    }

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
        _lastSprintProbeSnapshot = {};
        _lastSneakProbeSnapshot = {};
        _suppressGameplayDigitalTransientActions = false;
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

    bool NativeButtonCommitBackend::IsActionDown(std::string_view actionId) const
    {
        std::scoped_lock lock(_lock);
        for (const auto& slot : _pollCommit.Slots()) {
            if (slot.actionId.empty()) {
                continue;
            }
            if (slot.actionId.c_str() == actionId) {
                return SlotIsDown(slot);
            }
        }
        return false;
    }

    bool NativeButtonCommitBackend::HasHeldContributor(std::string_view actionId, HeldContributor contributor) const
    {
        const auto mask = static_cast<std::uint8_t>(contributor);
        if (mask == 0) {
            return false;
        }

        std::scoped_lock lock(_lock);
        for (const auto& slot : _pollCommit.Slots()) {
            if (slot.actionId.empty()) {
                continue;
            }
            if (slot.actionId.c_str() == actionId) {
                return (slot.heldContributorMask & mask) != 0;
            }
        }
        return false;
    }

    HeldEmitterSource NativeButtonCommitBackend::GetHeldEmitter(std::string_view actionId) const
    {
        std::scoped_lock lock(_lock);
        for (const auto& slot : _pollCommit.Slots()) {
            if (slot.actionId.empty()) {
                continue;
            }
            if (slot.actionId.c_str() == actionId) {
                return slot.activeHeldEmitter;
            }
        }
        return HeldEmitterSource::None;
    }

    void NativeButtonCommitBackend::BeginFrame(
        InputContext context,
        std::uint32_t contextEpoch,
        std::uint64_t nowUs)
    {
        std::scoped_lock lock(_lock);
        _frameContext = context;
        _frameContextEpoch = contextEpoch;
        _suppressGameplayDigitalTransientActions = false;
        _pollCommit.BeginFrame(context, contextEpoch, nowUs != 0 ? nowUs : NowUs());
        SyncExternalHeldContributors(context, contextEpoch);
    }

    void NativeButtonCommitBackend::SetGameplayDigitalGatePlan(bool suppressNewTransientActions)
    {
        std::scoped_lock lock(_lock);
        _suppressGameplayDigitalTransientActions = suppressNewTransientActions;
    }

    bool NativeButtonCommitBackend::ApplyPlannedAction(const PlannedAction& action)
    {
        std::scoped_lock lock(_lock);
        if (RuntimeConfig::GetSingleton().EnableGameplayOwnership() &&
            IsGameplayDigitalSuppressionCandidate(action) &&
            _suppressGameplayDigitalTransientActions) {
            if (ShouldLogPollCommit()) {
                logger::info(
                    "[DualPad][NativeButtonCommit] Suppressed gameplay digital action={} phase={} via frame digital gate plan",
                    action.actionId.c_str(),
                    ToString(action.phase));
            }
            return true;
        }

        if (ShouldLogPollCommit() &&
            action.actionId == actions::Sneak) {
            logger::info(
                "[DualPad][SneakProbe] apply phase={} contract={} digitalPolicy={} lifecycle={} gateAware={} context={} epoch={}",
                ToString(action.phase),
                ToString(action.contract),
                ToString(action.digitalPolicy),
                ToString(action.lifecyclePolicy),
                action.gateAware,
                ToString(action.context),
                action.contextEpoch);
        }

        PollCommitRequest request{};
        if (!TranslatePlannedActionToCommitRequest(action, request)) {
            return false;
        }
        return _pollCommit.QueueRequest(request);
    }

    void NativeButtonCommitBackend::ForceCancelGateAwareGameplayTransientActions()
    {
        std::scoped_lock lock(_lock);
        _pollCommit.ForceCancelGateAwareTransientSlots();
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
        SyncExternalHeldContributors(context, contextEpoch);
        _pollCommit.Tick(nowUs, IsGameplayGateOpen(context));
        _pollCommit.Flush(*this, nowUs);

        SprintProbeSnapshot sprintSnapshot{};
        sprintSnapshot.valid = true;
        sprintSnapshot.kbmHeld =
            context == InputContext::Gameplay &&
            GameplayKbmFactTracker::GetSingleton().GetFacts().IsKeyboardMouseSprintActive();
        sprintSnapshot.gameplayOwnerGamepad =
            InputModalityTracker::GetSingleton().IsGameplayUsingGamepad();
        sprintSnapshot.context = context;
        sprintSnapshot.contextEpoch = contextEpoch;

        CommittedButtonState result{};
        result.context = context;
        result.contextEpoch = contextEpoch;
        result.pollSequence = ++_pollSequence;
        SneakProbeSnapshot sneakSnapshot{};
        sneakSnapshot.valid = true;
        sneakSnapshot.context = context;
        sneakSnapshot.contextEpoch = contextEpoch;
        sneakSnapshot.pollSequence = result.pollSequence;

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
                    slot.actionId == RE::BSFixedString(actions::Sprint.data()) ?
                        slot.activeHeldEmitter == HeldEmitterSource::Gamepad :
                        slot.heldContributorMask != 0,
                    slot.emittedDownCount,
                    slot.emittedUpCount);
            }

            if (slot.actionId == RE::BSFixedString(actions::Sprint.data())) {
                sprintSnapshot.gamepadContributor =
                    (slot.heldContributorMask & static_cast<std::uint8_t>(HeldContributor::Gamepad)) != 0;
                sprintSnapshot.keyboardMouseContributor =
                    (slot.heldContributorMask & static_cast<std::uint8_t>(HeldContributor::KeyboardMouse)) != 0;
                sprintSnapshot.effectiveHeld = slot.heldContributorMask != 0;
                sprintSnapshot.actionDown = SlotIsDown(slot);
                sprintSnapshot.managed = SlotIsManaged(slot);
                sprintSnapshot.state = slot.state;
                sprintSnapshot.activeEmitter = slot.activeHeldEmitter;
            }

            if (slot.actionId == RE::BSFixedString(actions::Sneak.data())) {
                sneakSnapshot.actionDown = SlotIsDown(slot);
                sneakSnapshot.managed = SlotIsManaged(slot);
                sneakSnapshot.gateAware = slot.gateAware;
                sneakSnapshot.state = slot.state;
                sneakSnapshot.mode = slot.mode;
            }
        }

        if (ShouldLogPollCommit()) {
            const auto changed =
                !_lastSprintProbeSnapshot.valid ||
                sprintSnapshot.kbmHeld != _lastSprintProbeSnapshot.kbmHeld ||
                sprintSnapshot.gamepadContributor != _lastSprintProbeSnapshot.gamepadContributor ||
                sprintSnapshot.keyboardMouseContributor != _lastSprintProbeSnapshot.keyboardMouseContributor ||
                sprintSnapshot.effectiveHeld != _lastSprintProbeSnapshot.effectiveHeld ||
                sprintSnapshot.actionDown != _lastSprintProbeSnapshot.actionDown ||
                sprintSnapshot.managed != _lastSprintProbeSnapshot.managed ||
                sprintSnapshot.gameplayOwnerGamepad != _lastSprintProbeSnapshot.gameplayOwnerGamepad ||
                sprintSnapshot.state != _lastSprintProbeSnapshot.state ||
                sprintSnapshot.activeEmitter != _lastSprintProbeSnapshot.activeEmitter ||
                sprintSnapshot.context != _lastSprintProbeSnapshot.context ||
                sprintSnapshot.contextEpoch != _lastSprintProbeSnapshot.contextEpoch;
            if (changed) {
                logger::info(
                    "[DualPad][SprintProbe] snapshot poll={} ctx={} epoch={} kbmHeld={} gpContributor={} kbmContributor={} effectiveHeld={} actionDown={} managed={} activeEmitter={} gameplayOwnerGamepad={} state={}",
                    result.pollSequence,
                    ToString(sprintSnapshot.context),
                    sprintSnapshot.contextEpoch,
                    sprintSnapshot.kbmHeld,
                    sprintSnapshot.gamepadContributor,
                    sprintSnapshot.keyboardMouseContributor,
                    sprintSnapshot.effectiveHeld,
                    sprintSnapshot.actionDown,
                    sprintSnapshot.managed,
                    ToString(sprintSnapshot.activeEmitter),
                    sprintSnapshot.gameplayOwnerGamepad,
                    ToString(sprintSnapshot.state));
                _lastSprintProbeSnapshot = sprintSnapshot;
            }
        }

        if (ShouldLogPollCommit()) {
            const auto changed =
                !_lastSneakProbeSnapshot.valid ||
                sneakSnapshot.actionDown != _lastSneakProbeSnapshot.actionDown ||
                sneakSnapshot.managed != _lastSneakProbeSnapshot.managed ||
                sneakSnapshot.gateAware != _lastSneakProbeSnapshot.gateAware ||
                sneakSnapshot.state != _lastSneakProbeSnapshot.state ||
                sneakSnapshot.mode != _lastSneakProbeSnapshot.mode ||
                sneakSnapshot.context != _lastSneakProbeSnapshot.context ||
                sneakSnapshot.contextEpoch != _lastSneakProbeSnapshot.contextEpoch;
            if (changed) {
                logger::info(
                    "[DualPad][SneakProbe] snapshot poll={} ctx={} epoch={} actionDown={} managed={} gateAware={} mode={} state={}",
                    sneakSnapshot.pollSequence,
                    ToString(sneakSnapshot.context),
                    sneakSnapshot.contextEpoch,
                    sneakSnapshot.actionDown,
                    sneakSnapshot.managed,
                    sneakSnapshot.gateAware,
                    ToString(sneakSnapshot.mode),
                    ToString(sneakSnapshot.state));
                _lastSneakProbeSnapshot = sneakSnapshot;
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
            outRequest.contributor = HeldContributor::Gamepad;
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
            outRequest.contributor = HeldContributor::Gamepad;
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
                outRequest.mode = PollCommitMode::Toggle;
                outRequest.kind = PollCommitRequestKind::ToggleFire;
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
        if (slot.actionId == RE::BSFixedString(actions::Sprint.data()) &&
            slot.mode == PollCommitMode::Hold &&
            slot.activeHeldEmitter == HeldEmitterSource::KeyboardMouse &&
            !slot.token.active) {
            return false;
        }

        return slot.state != ExecState::Idle ||
            slot.token.active ||
            slot.pending.kind != PendingKind::None ||
            slot.pending.pendingNextPulse ||
            slot.heldContributorMask != 0;
    }

    void NativeButtonCommitBackend::SyncExternalHeldContributors(InputContext context, std::uint32_t)
    {
        constexpr bool kbmSprintHeld = false;
        if (ShouldLogPollCommit()) {
            logger::info(
                "[DualPad][SprintProbe] SyncExternalHeldContributors kbmSprintHeld={} ctx={}",
                kbmSprintHeld,
                ToString(context));
        }
        _pollCommit.SyncHeldContributor(
            RE::BSFixedString(actions::Sprint.data()),
            HeldContributor::KeyboardMouse,
            kbmSprintHeld);
    }
}
