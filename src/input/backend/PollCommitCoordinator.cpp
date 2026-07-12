#include "pch.h"
#include "input/backend/PollCommitCoordinator.h"

#include "input/Action.h"
#include "input/RuntimeConfig.h"

#include <SKSE/SKSE.h>

namespace logger = SKSE::log;

namespace dualpad::input::backend
{
    namespace
    {
        constexpr std::uint8_t ToMask(HeldContributor contributor)
        {
            return static_cast<std::uint8_t>(contributor);
        }

        bool IsSprintAction(std::string_view actionId)
        {
            return actionId == actions::Sprint;
        }

        bool ShouldLogCoordinator()
        {
            const auto& config = RuntimeConfig::GetSingleton();
            return config.LogActionPlan() || config.LogNativeInjection();
        }
    }

    void PollCommitCoordinator::Reset()
    {
        _slots = {};
        _currentContext = InputContext::Gameplay;
        _currentEpoch = 0;
        _nowUs = 0;
        _currentGeneration = 0;
        _nextTokenId = 1;
        _lastGameplayGateOpen = true;
        _lastPulseRecord = {};
    }

    void PollCommitCoordinator::BeginFrame(
        InputContext context,
        std::uint32_t contextEpoch,
        std::uint64_t nowUs,
        std::uint64_t runtimeGeneration)
    {
        _currentContext = context;
        _currentEpoch = contextEpoch;
        _nowUs = nowUs;
        _currentGeneration = runtimeGeneration;

        for (auto& slot : _slots) {
            if (!slot.actionId.empty() &&
                slot.epoch != 0 &&
                slot.epoch != _currentEpoch) {
                InvalidateStaleState(slot);
            }
        }
    }

    bool PollCommitCoordinator::QueueRequest(const PollCommitRequest& request)
    {
        if (request.actionId.empty() ||
            request.outputCode == NativeControlCode::None ||
            request.mode == PollCommitMode::None ||
            request.kind == PollCommitRequestKind::None) {
            return false;
        }

        auto* slot = FindOrCreateSlot(request.actionId);
        if (!slot) {
            return false;
        }

        slot->context = request.context;
        slot->outputCode = request.outputCode;
        slot->mode = request.mode;
        slot->gateAware = request.gateAware;
        slot->epoch = request.epoch != 0 ? request.epoch : _currentEpoch;
        slot->minDownMs = request.minDownMs;
        slot->repeatDelayMs = request.repeatDelayMs;
        slot->repeatIntervalMs = request.repeatIntervalMs;

        switch (request.kind) {
        case PollCommitRequestKind::Pulse:
            QueuePulse(*slot, request);
            return true;
        case PollCommitRequestKind::ToggleFire:
            QueueToggle(*slot, request);
            return true;
        case PollCommitRequestKind::HoldSet:
            QueueHoldSet(*slot, request);
            return true;
        case PollCommitRequestKind::HoldClear:
            QueueHoldClear(*slot);
            return true;
        case PollCommitRequestKind::RepeatSet:
            QueueRepeatSet(*slot, request);
            return true;
        case PollCommitRequestKind::RepeatClear:
            QueueRepeatClear(*slot);
            return true;
        case PollCommitRequestKind::ForceCancel:
            QueueForceCancel(*slot);
            return true;
        case PollCommitRequestKind::None:
        default:
            return false;
        }
    }

    void PollCommitCoordinator::Tick(
        std::uint64_t nowUs,
        bool gameplayGateOpen)
    {
        _nowUs = nowUs;
        _lastGameplayGateOpen = gameplayGateOpen;

        for (auto& slot : _slots) {
            if (slot.actionId.empty()) {
                continue;
            }

            TickSlot(slot, nowUs, gameplayGateOpen);
        }
    }

    void PollCommitCoordinator::Flush(IPollCommitEmitter& emitter, std::uint64_t nowUs)
    {
        for (auto& slot : _slots) {
            if (slot.actionId.empty() || !HasManagedState(slot)) {
                continue;
            }

            if ((slot.state == ExecState::PulseDownVisible ||
                 slot.state == ExecState::HoldDownVisible) &&
                slot.token.active &&
                !slot.token.downSubmitted) {
                const auto result = emitter.Emit({
                    .actionId = slot.actionId,
                    .context = slot.context,
                    .epoch = slot.token.epoch,
                    .tokenId = slot.token.tokenId,
                    .runtimeGeneration = _currentGeneration,
                    .edge = EmitEdge::Down,
                    .heldSeconds = 0.0f,
                    .nowUs = nowUs
                });
                if (result.submitted) {
                    slot.token.downAtUs = nowUs;
                    slot.token.earliestReleaseAtUs =
                        nowUs + static_cast<std::uint64_t>(slot.minDownMs) * 1000ULL;
                    slot.token.downGeneration = _currentGeneration;
                    slot.token.downSubmitted = true;
                    ++slot.emittedDownCount;
                    if (slot.mode == PollCommitMode::Pulse || slot.mode == PollCommitMode::Toggle) {
                        _lastPulseRecord = {
                            .tokenId = slot.token.tokenId,
                            .epoch = slot.token.epoch,
                            .downGeneration = slot.token.downGeneration
                        };
                    }
                }
            }

            if (slot.state == ExecState::ReleaseGap &&
                slot.token.active &&
                slot.token.downSubmitted &&
                !slot.token.releaseSubmitted) {
                const auto heldSeconds = slot.token.downAtUs == 0 || nowUs <= slot.token.downAtUs ?
                    0.0f :
                    static_cast<float>(nowUs - slot.token.downAtUs) / 1'000'000.0f;
                const auto result = emitter.Emit({
                    .actionId = slot.actionId,
                    .context = slot.context,
                    .epoch = slot.token.epoch,
                    .tokenId = slot.token.tokenId,
                    .runtimeGeneration = _currentGeneration,
                    .edge = EmitEdge::Up,
                    .heldSeconds = heldSeconds,
                    .nowUs = nowUs
                });
                if (result.submitted) {
                    slot.token.upGeneration = _currentGeneration;
                    slot.token.releaseSubmitted = true;
                    ++slot.emittedUpCount;
                    if (slot.mode == PollCommitMode::Pulse || slot.mode == PollCommitMode::Toggle) {
                        _lastPulseRecord = {
                            .tokenId = slot.token.tokenId,
                            .epoch = slot.token.epoch,
                            .downGeneration = slot.token.downGeneration,
                            .upGeneration = slot.token.upGeneration
                        };
                    }
                    CompleteRelease(slot, nowUs);
                }
            }
        }
    }

    void PollCommitCoordinator::CancelForBoundary(
        PulseBoundaryReason reason,
        std::uint64_t runtimeGeneration)
    {
        _currentGeneration = runtimeGeneration;
        for (auto& slot : _slots) {
            const bool transient = slot.mode == PollCommitMode::Pulse || slot.mode == PollCommitMode::Toggle;
            if (slot.actionId.empty()) {
                continue;
            }

            if (transient && slot.token.active && slot.token.downSubmitted && !slot.token.releaseSubmitted) {
                _lastPulseRecord = {
                    .tokenId = slot.token.tokenId,
                    .epoch = slot.token.epoch,
                    .downGeneration = slot.token.downGeneration,
                    .upGeneration = runtimeGeneration,
                    .boundaryReason = reason,
                    .cancelled = true
                };
            }
            if (HasManagedState(slot)) {
                ++slot.cancelledCount;
            }
            slot.pending = {};
            slot.heldContributorMask = 0;
            slot.virtualBridgeDesired = false;
            slot.activeHeldEmitter = HeldEmitterSource::None;
            ClearToken(slot);
            TransitionState(slot, ExecState::Idle, _nowUs);
        }
    }

    void PollCommitCoordinator::ForceCancelGateAwareTransientSlots()
    {
        for (auto& slot : _slots) {
            const auto contextValue = static_cast<std::uint16_t>(slot.context);
            const bool isGameplayContext = !(contextValue >= 100 && contextValue < 2000) && slot.context != InputContext::Console;
            const bool isTransientMode = slot.mode == PollCommitMode::Pulse || slot.mode == PollCommitMode::Toggle;
            if (slot.actionId.empty() || !slot.gateAware || !isGameplayContext || !isTransientMode) {
                continue;
            }

            QueueForceCancel(slot);
        }
    }

    bool PollCommitCoordinator::SyncHeldContributors(
        std::string_view actionId,
        NativeControlCode outputCode,
        PollCommitMode mode,
        std::uint8_t activeSourceMask,
        bool virtualBridgeDesired,
        std::uint32_t epoch)
    {
        constexpr auto kKnownMask =
            ToMask(HeldContributor::Gamepad) |
            ToMask(HeldContributor::KeyboardMouse) |
            ToMask(HeldContributor::MousePhysical);
        if (actionId.empty() || outputCode == NativeControlCode::None ||
            mode == PollCommitMode::None || (activeSourceMask & ~kKnownMask) != 0) {
            return false;
        }
        auto* slot = FindOrCreateSlot(actionId);
        if (!slot) {
            return false;
        }
        slot->context = _currentContext;
        slot->outputCode = outputCode;
        slot->mode = mode;
        slot->epoch = epoch != 0 ? epoch : _currentEpoch;
        slot->heldContributorMask = activeSourceMask;
        slot->virtualBridgeDesired = virtualBridgeDesired && activeSourceMask != 0;
        return true;
    }

    void PollCommitCoordinator::DumpState() const
    {
        if (!ShouldLogCoordinator()) {
            return;
        }

        for (const auto& slot : _slots) {
            if (slot.actionId.empty() || !HasManagedState(slot)) {
                continue;
            }

            logger::info(
                "[DualPad][PollCommit] action={} state={} epoch={} token={} mode={} pending={} nextPulse={} desiredHeld={} activeEmitter={} downGeneration={} upGeneration={} downSubmitted={} releaseSubmitted={} downCount={} upCount={} coalesced={} dropped={}",
                slot.actionId.c_str(),
                ToString(slot.state),
                slot.epoch,
                slot.token.tokenId,
                ToString(slot.mode),
                ToString(slot.pending.kind),
                slot.pending.pendingNextPulse,
                HasSyntheticHoldDemand(slot),
                ToString(slot.activeHeldEmitter),
                slot.token.downGeneration,
                slot.token.upGeneration,
                slot.token.downSubmitted,
                slot.token.releaseSubmitted,
                slot.emittedDownCount,
                slot.emittedUpCount,
                slot.coalescedPulseCount,
                slot.droppedPulseCount);
        }
    }

    const std::array<PollCommitSlot, PollCommitCoordinator::kMaxSlots>& PollCommitCoordinator::Slots() const
    {
        return _slots;
    }

    PulseGenerationRecord PollCommitCoordinator::LastPulseRecord() const noexcept
    {
        return _lastPulseRecord;
    }

    PollCommitSlot* PollCommitCoordinator::FindOrCreateSlot(std::string_view actionId)
    {
        if (auto* existing = FindSlot(actionId)) {
            return existing;
        }

        for (auto& slot : _slots) {
            if (slot.actionId.empty()) {
                slot.actionId = actionId;
                return &slot;
            }
        }

        return nullptr;
    }

    PollCommitSlot* PollCommitCoordinator::FindSlot(std::string_view actionId)
    {
        for (auto& slot : _slots) {
            if (slot.actionId == actionId) {
                return &slot;
            }
        }

        return nullptr;
    }

    void PollCommitCoordinator::QueuePulse(PollCommitSlot& slot, const PollCommitRequest& request)
    {
        slot.context = request.context;

        if (!CanStartNewTransaction(slot)) {
            if (!slot.pending.pendingNextPulse) {
                slot.pending.pendingNextPulse = true;
                ++slot.coalescedPulseCount;
            } else {
                ++slot.droppedPulseCount;
            }
            return;
        }

        if (slot.pending.kind == PendingKind::None) {
            slot.pending.kind = PendingKind::Pulse;
            slot.pending.epoch = slot.epoch;
            slot.pending.queuedAtUs = request.timestampUs != 0 ? request.timestampUs : _nowUs;
            return;
        }

        if (!slot.pending.pendingNextPulse) {
            slot.pending.pendingNextPulse = true;
            ++slot.coalescedPulseCount;
        } else {
            ++slot.droppedPulseCount;
        }
    }

    void PollCommitCoordinator::QueueHoldSet(PollCommitSlot& slot, const PollCommitRequest& request)
    {
        slot.context = request.context;
        SetHeldContributor(slot, request.contributor, true);
        if (IsSprintAction(slot.actionId) && request.contributor == HeldContributor::Gamepad) {
            slot.virtualBridgeDesired = true;
        }
        if (slot.state == ExecState::Idle) {
            slot.pending.kind = PendingKind::HoldStart;
            slot.pending.queuedAtUs = request.timestampUs != 0 ? request.timestampUs : _nowUs;
            slot.pending.epoch = slot.epoch;
        }
    }

    void PollCommitCoordinator::QueueHoldClear(PollCommitSlot& slot)
    {
        SetHeldContributor(slot, HeldContributor::Gamepad, false);
        if (IsSprintAction(slot.actionId)) {
            slot.virtualBridgeDesired = HasHeldContributors(slot) && slot.token.active;
        }
        if (!IsSingleEmitterHoldAction(slot.actionId)) {
            slot.pending.kind = PendingKind::HoldEnd;
        }
    }

    void PollCommitCoordinator::QueueToggle(PollCommitSlot& slot, const PollCommitRequest& request)
    {
        slot.context = request.context;

        if (!CanStartNewTransaction(slot)) {
            ++slot.droppedPulseCount;
            return;
        }

        slot.pending.pendingNextPulse = false;
        slot.pending.kind = PendingKind::Toggle;
        slot.pending.epoch = slot.epoch;
        slot.pending.queuedAtUs = request.timestampUs != 0 ? request.timestampUs : _nowUs;
    }

    void PollCommitCoordinator::QueueRepeatSet(PollCommitSlot& slot, const PollCommitRequest& request)
    {
        slot.context = request.context;
        SetHeldContributor(slot, request.contributor, true);
        if (slot.state == ExecState::Idle &&
            slot.pending.kind == PendingKind::None) {
            slot.pending.kind = PendingKind::RepeatStart;
            slot.pending.queuedAtUs = request.timestampUs != 0 ? request.timestampUs : _nowUs;
            slot.pending.epoch = slot.epoch;
        }
    }

    void PollCommitCoordinator::QueueRepeatClear(PollCommitSlot& slot)
    {
        // Keep the first repeat edge visible for one Poll even when release
        // arrives before the next Poll, but clear sustained held intent.
        SetHeldContributor(slot, HeldContributor::Gamepad, false);
    }

    void PollCommitCoordinator::QueueForceCancel(PollCommitSlot& slot)
    {
        slot.pending.kind = PendingKind::ForceCancel;
        slot.heldContributorMask = 0;
        slot.virtualBridgeDesired = false;
        slot.activeHeldEmitter = HeldEmitterSource::None;
    }

    void PollCommitCoordinator::TickSlot(PollCommitSlot& slot, std::uint64_t nowUs, bool gateOpen)
    {
        switch (slot.mode) {
        case PollCommitMode::Pulse:
            TickPulseSlot(slot, nowUs, gateOpen);
            break;
        case PollCommitMode::Toggle:
            TickToggleSlot(slot, nowUs, gateOpen);
            break;
        case PollCommitMode::Hold:
            TickHoldSlot(slot, nowUs, gateOpen);
            break;
        case PollCommitMode::Repeat:
            TickRepeatSlot(slot, nowUs, gateOpen);
            break;
        case PollCommitMode::None:
        default:
            break;
        }
    }

    void PollCommitCoordinator::TickPulseSlot(PollCommitSlot& slot, std::uint64_t nowUs, bool gateOpen)
    {
        const auto canOpen = ShouldOpenGateForSlot(slot, gateOpen);

        switch (slot.state) {
        case ExecState::Idle:
            if (slot.pending.kind == PendingKind::ForceCancel) {
                slot.pending = {};
                break;
            }
            if (slot.pending.kind == PendingKind::Pulse && CanStartNewTransaction(slot)) {
                if (canOpen) {
                    StartPulseTransaction(slot, nowUs);
                    slot.pending.kind = PendingKind::None;
                } else {
                    TransitionState(slot, ExecState::WaitingForGate, nowUs);
                }
            }
            break;

        case ExecState::WaitingForGate:
            if (slot.pending.kind == PendingKind::ForceCancel) {
                slot.pending = {};
                TransitionState(slot, ExecState::Idle, nowUs);
                break;
            }

            if (slot.pending.kind != PendingKind::Pulse && !slot.pending.pendingNextPulse) {
                TransitionState(slot, ExecState::Idle, nowUs);
                break;
            }

            if (canOpen && CanStartNewTransaction(slot)) {
                if (slot.pending.kind != PendingKind::Pulse) {
                    slot.pending.kind = PendingKind::Pulse;
                    slot.pending.pendingNextPulse = false;
                }
                StartPulseTransaction(slot, nowUs);
                slot.pending.kind = PendingKind::None;
            }
            break;

        case ExecState::PulseDownVisible:
            if (slot.token.active &&
                slot.token.downSubmitted &&
                slot.token.downGeneration != 0 &&
                _currentGeneration > slot.token.downGeneration &&
                nowUs >= slot.token.earliestReleaseAtUs) {
                TransitionState(slot, ExecState::ReleaseGap, nowUs);
            }
            break;

        case ExecState::ReleaseGap:
        case ExecState::HoldDownVisible:
        default:
            break;
        }
    }

    void PollCommitCoordinator::TickHoldSlot(PollCommitSlot& slot, std::uint64_t nowUs, bool gateOpen)
    {
        const auto canOpen = ShouldOpenGateForSlot(slot, gateOpen);
        const auto desiredEmitter = ResolveHeldEmitter(slot);
        const bool syntheticHoldDemand = HasSyntheticHoldDemand(slot);

        switch (slot.state) {
        case ExecState::Idle:
            if (slot.pending.kind == PendingKind::ForceCancel) {
                slot.pending = {};
                slot.activeHeldEmitter = HeldEmitterSource::None;
                break;
            }
            if (syntheticHoldDemand) {
                if (canOpen) {
                    StartHoldTransaction(slot, nowUs);
                } else {
                    slot.activeHeldEmitter = HeldEmitterSource::Gamepad;
                    TransitionState(slot, ExecState::WaitingForGate, nowUs);
                }
            } else {
                slot.activeHeldEmitter = desiredEmitter;
                if (slot.pending.kind != PendingKind::None) {
                    slot.pending.kind = PendingKind::None;
                }
            }
            break;

        case ExecState::WaitingForGate:
            if (slot.pending.kind == PendingKind::ForceCancel) {
                slot.pending = {};
                slot.activeHeldEmitter = HeldEmitterSource::None;
                TransitionState(slot, ExecState::Idle, nowUs);
                break;
            }
            if (!syntheticHoldDemand) {
                slot.pending.kind = PendingKind::None;
                slot.activeHeldEmitter = desiredEmitter;
                TransitionState(slot, ExecState::Idle, nowUs);
                break;
            }

            if (canOpen) {
                StartHoldTransaction(slot, nowUs);
            }
            break;

        case ExecState::HoldDownVisible:
            if (slot.pending.kind == PendingKind::ForceCancel || !syntheticHoldDemand) {
                TransitionState(slot, ExecState::ReleaseGap, nowUs);
            }
            break;

        case ExecState::PulseDownVisible:
        case ExecState::ReleaseGap:
        default:
            break;
        }
    }

    void PollCommitCoordinator::TickRepeatSlot(PollCommitSlot& slot, std::uint64_t nowUs, bool gateOpen)
    {
        const auto canOpen = ShouldOpenGateForSlot(slot, gateOpen);

        switch (slot.state) {
        case ExecState::Idle:
            if (slot.pending.kind == PendingKind::ForceCancel) {
                slot.pending = {};
                break;
            }
            if (slot.pending.kind == PendingKind::RepeatStart) {
                if (canOpen) {
                    StartRepeatTransaction(slot, nowUs);
                } else {
                    TransitionState(slot, ExecState::WaitingForGate, nowUs);
                }
            } else if (slot.pending.kind != PendingKind::None) {
                slot.pending.kind = PendingKind::None;
            }
            break;

        case ExecState::WaitingForGate:
            if (slot.pending.kind == PendingKind::ForceCancel) {
                slot.pending = {};
                TransitionState(slot, ExecState::Idle, nowUs);
                break;
            }
            if (slot.pending.kind != PendingKind::RepeatStart) {
                TransitionState(slot, ExecState::Idle, nowUs);
                break;
            }

            if (canOpen) {
                StartRepeatTransaction(slot, nowUs);
            }
            break;

        case ExecState::HoldDownVisible:
            if (slot.pending.kind == PendingKind::ForceCancel) {
                TransitionState(slot, ExecState::ReleaseGap, nowUs);
                break;
            }
            if (!HasHeldContributors(slot) &&
                slot.token.active &&
                slot.token.downSubmitted) {
                TransitionState(slot, ExecState::ReleaseGap, nowUs);
            }
            break;

        case ExecState::PulseDownVisible:
        case ExecState::ReleaseGap:
        default:
            break;
        }
    }

    void PollCommitCoordinator::InvalidateStaleState(PollCommitSlot& slot)
    {
        slot.pending = {};
        slot.heldContributorMask = 0;
        slot.virtualBridgeDesired = false;
        slot.activeHeldEmitter = HeldEmitterSource::None;
        ++slot.cancelledCount;

        if (slot.token.active && slot.token.downSubmitted && !slot.token.releaseSubmitted) {
            TransitionState(slot, ExecState::ReleaseGap, _nowUs);
            slot.epoch = _currentEpoch;
            return;
        }

        ClearToken(slot);
        TransitionState(slot, ExecState::Idle, _nowUs);
        slot.epoch = _currentEpoch;
    }

    bool PollCommitCoordinator::CanStartNewTransaction(const PollCommitSlot& slot) const
    {
        return !slot.token.active &&
            slot.state != ExecState::PulseDownVisible &&
            slot.state != ExecState::HoldDownVisible &&
            slot.state != ExecState::ReleaseGap;
    }

    void PollCommitCoordinator::StartPulseTransaction(PollCommitSlot& slot, std::uint64_t nowUs)
    {
        ClearToken(slot);
        slot.token.active = true;
        slot.token.tokenId = _nextTokenId++;
        slot.token.epoch = slot.epoch != 0 ? slot.epoch : _currentEpoch;
        TransitionState(slot, ExecState::PulseDownVisible, nowUs);
    }

    void PollCommitCoordinator::StartHoldTransaction(PollCommitSlot& slot, std::uint64_t nowUs)
    {
        ClearToken(slot);
        slot.token.active = true;
        slot.token.tokenId = _nextTokenId++;
        slot.token.epoch = slot.epoch != 0 ? slot.epoch : _currentEpoch;
        slot.pending.kind = PendingKind::None;
        slot.activeHeldEmitter = HeldEmitterSource::Gamepad;
        TransitionState(slot, ExecState::HoldDownVisible, nowUs);
    }

    void PollCommitCoordinator::StartRepeatTransaction(PollCommitSlot& slot, std::uint64_t nowUs)
    {
        StartHoldTransaction(slot, nowUs);
        // Repeat currently relies on sustained current-state down after the
        // first visible edge so Skyrim's native producer generates subsequent
        // repeat events.
    }

    void PollCommitCoordinator::CompleteRelease(PollCommitSlot& slot, std::uint64_t nowUs)
    {
        const auto mode = slot.mode;
        ClearToken(slot);

        if (mode == PollCommitMode::Pulse || mode == PollCommitMode::Toggle) {
            if (slot.pending.pendingNextPulse) {
                slot.pending.pendingNextPulse = false;
                slot.pending.kind = PendingKind::Pulse;
                slot.pending.queuedAtUs = nowUs;
                slot.pending.epoch = _currentEpoch;

                if (ShouldOpenGateForSlot(slot, _lastGameplayGateOpen)) {
                    StartPulseTransaction(slot, nowUs);
                    slot.pending.kind = PendingKind::None;
                } else {
                    TransitionState(slot, ExecState::WaitingForGate, nowUs);
                }
                return;
            }

            slot.pending.kind = PendingKind::None;
            TransitionState(slot, ExecState::Idle, nowUs);
            return;
        }

        if (HasSyntheticHoldDemand(slot)) {
            if (ShouldOpenGateForSlot(slot, _lastGameplayGateOpen)) {
                if (mode == PollCommitMode::Repeat) {
                    StartRepeatTransaction(slot, nowUs);
                } else {
                    StartHoldTransaction(slot, nowUs);
                }
            } else {
                slot.pending.kind = mode == PollCommitMode::Repeat ?
                    PendingKind::RepeatStart :
                    PendingKind::HoldStart;
                slot.activeHeldEmitter = HeldEmitterSource::Gamepad;
                TransitionState(slot, ExecState::WaitingForGate, nowUs);
            }
            return;
        }

        slot.activeHeldEmitter = ResolveHeldEmitter(slot);
        slot.pending.kind = PendingKind::None;
        TransitionState(slot, ExecState::Idle, nowUs);
    }

    void PollCommitCoordinator::ClearToken(PollCommitSlot& slot)
    {
        slot.token = {};
    }

    void PollCommitCoordinator::TransitionState(
        PollCommitSlot& slot,
        ExecState newState,
        std::uint64_t nowUs)
    {
        if (slot.state == newState) {
            return;
        }

        if (ShouldLogCoordinator()) {
            logger::info(
                "[DualPad][PollCommit] action={} state={} -> {} epoch={} token={} mode={} pending={} nextPulse={} gateAware={}",
                slot.actionId.c_str(),
                ToString(slot.state),
                ToString(newState),
                slot.epoch,
                slot.token.tokenId,
                ToString(slot.mode),
                ToString(slot.pending.kind),
                slot.pending.pendingNextPulse,
                slot.gateAware);
        }

        slot.state = newState;
        slot.lastTransitionUs = nowUs;
    }

    bool PollCommitCoordinator::ShouldOpenGateForSlot(
        const PollCommitSlot& slot,
        bool gameplayGateOpen) const
    {
        return !slot.gateAware || gameplayGateOpen;
    }

    bool PollCommitCoordinator::HasManagedState(const PollCommitSlot& slot) const
    {
        return !slot.actionId.empty() &&
            (slot.token.active ||
             slot.pending.kind != PendingKind::None ||
             slot.pending.pendingNextPulse ||
             HasHeldContributors(slot) ||
             slot.state != ExecState::Idle);
    }

    bool PollCommitCoordinator::IsSingleEmitterHoldAction(std::string_view actionId) const
    {
        return actionId == actions::Sprint;
    }

    HeldEmitterSource PollCommitCoordinator::ResolveHeldEmitter(const PollCommitSlot& slot) const
    {
        if (IsSingleEmitterHoldAction(slot.actionId)) {
            if (slot.virtualBridgeDesired) {
                return HeldEmitterSource::Gamepad;
            }
            const bool hasKeyboardMouse =
                HasHeldContributor(slot, HeldContributor::KeyboardMouse) ||
                HasHeldContributor(slot, HeldContributor::MousePhysical);
            return hasKeyboardMouse ?
                HeldEmitterSource::KeyboardMouse : HeldEmitterSource::None;
        }

        if (HasHeldContributor(slot, HeldContributor::Gamepad)) {
            return HeldEmitterSource::Gamepad;
        }
        if (HasHeldContributor(slot, HeldContributor::KeyboardMouse)) {
            return HeldEmitterSource::KeyboardMouse;
        }
        return HeldEmitterSource::None;
    }

    bool PollCommitCoordinator::HasSyntheticHoldDemand(const PollCommitSlot& slot) const
    {
        if (slot.mode != PollCommitMode::Hold && slot.mode != PollCommitMode::Repeat) {
            return HasHeldContributors(slot);
        }

        if (!IsSingleEmitterHoldAction(slot.actionId)) {
            return HasHeldContributors(slot);
        }

        return slot.virtualBridgeDesired;
    }

    bool PollCommitCoordinator::HasHeldContributors(const PollCommitSlot& slot) const
    {
        return slot.heldContributorMask != 0;
    }

    bool PollCommitCoordinator::HasHeldContributor(const PollCommitSlot& slot, HeldContributor contributor) const
    {
        const auto mask = ToMask(contributor);
        return mask != 0 && (slot.heldContributorMask & mask) != 0;
    }

    void PollCommitCoordinator::SetHeldContributor(PollCommitSlot& slot, HeldContributor contributor, bool held)
    {
        const auto mask = ToMask(contributor);
        if (mask == 0) {
            return;
        }

        const auto previousMask = slot.heldContributorMask;
        if (held) {
            slot.heldContributorMask |= mask;
        } else {
            slot.heldContributorMask &= static_cast<std::uint8_t>(~mask);
        }

        if (ShouldLogCoordinator() &&
            IsSprintAction(slot.actionId) &&
            previousMask != slot.heldContributorMask) {
            logger::info(
                "[DualPad][SprintProbe] contributor {} -> {} (mask {:02X} -> {:02X}, state={}, pending={}, tokenActive={})",
                ToString(contributor),
                held,
                previousMask,
                slot.heldContributorMask,
                ToString(slot.state),
                ToString(slot.pending.kind),
                slot.token.active);
        }
    }

    void PollCommitCoordinator::TickToggleSlot(PollCommitSlot& slot, std::uint64_t nowUs, bool gateOpen)
    {
        const auto canOpen = ShouldOpenGateForSlot(slot, gateOpen);

        switch (slot.state) {
        case ExecState::Idle:
            if (slot.pending.kind == PendingKind::ForceCancel) {
                slot.pending = {};
                break;
            }
            if (slot.pending.kind == PendingKind::Toggle && CanStartNewTransaction(slot)) {
                if (canOpen) {
                    StartPulseTransaction(slot, nowUs);
                    slot.pending.kind = PendingKind::None;
                } else {
                    TransitionState(slot, ExecState::WaitingForGate, nowUs);
                }
            }
            break;

        case ExecState::WaitingForGate:
            if (slot.pending.kind == PendingKind::ForceCancel) {
                slot.pending = {};
                TransitionState(slot, ExecState::Idle, nowUs);
                break;
            }
            if (slot.pending.kind != PendingKind::Toggle) {
                TransitionState(slot, ExecState::Idle, nowUs);
                break;
            }

            if (canOpen && CanStartNewTransaction(slot)) {
                StartPulseTransaction(slot, nowUs);
                slot.pending.kind = PendingKind::None;
            }
            break;

        case ExecState::PulseDownVisible:
            if (slot.token.active &&
                slot.token.downSubmitted &&
                slot.token.downGeneration != 0 &&
                _currentGeneration > slot.token.downGeneration &&
                nowUs >= slot.token.earliestReleaseAtUs) {
                TransitionState(slot, ExecState::ReleaseGap, nowUs);
            }
            break;

        case ExecState::ReleaseGap:
        case ExecState::HoldDownVisible:
        default:
            break;
        }
    }
}
