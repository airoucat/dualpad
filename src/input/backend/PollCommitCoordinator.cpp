#include "pch.h"
#include "input/backend/PollCommitCoordinator.h"

#include "input/RuntimeConfig.h"

#include <SKSE/SKSE.h>

namespace logger = SKSE::log;

namespace dualpad::input::backend
{
    namespace
    {
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
        _nextTokenId = 1;
        _lastGameplayGateOpen = true;
    }

    void PollCommitCoordinator::BeginFrame(
        InputContext context,
        std::uint32_t contextEpoch,
        std::uint64_t nowUs)
    {
        _currentContext = context;
        _currentEpoch = contextEpoch;
        _nowUs = nowUs;

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
                    .edge = EmitEdge::Down,
                    .heldSeconds = 0.0f,
                    .nowUs = nowUs
                });
                if (result.submitted) {
                    slot.token.downSubmitted = true;
                    ++slot.emittedDownCount;
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
                    .edge = EmitEdge::Up,
                    .heldSeconds = heldSeconds,
                    .nowUs = nowUs
                });
                if (result.submitted) {
                    slot.token.releaseSubmitted = true;
                    ++slot.emittedUpCount;
                    CompleteRelease(slot, nowUs);
                }
            }
        }
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
                "[DualPad][PollCommit] action={} state={} epoch={} token={} mode={} pending={} nextPulse={} desiredHeld={} downSubmitted={} releaseSubmitted={} downCount={} upCount={} coalesced={} dropped={}",
                slot.actionId.c_str(),
                ToString(slot.state),
                slot.epoch,
                slot.token.tokenId,
                ToString(slot.mode),
                ToString(slot.pending.kind),
                slot.pending.pendingNextPulse,
                slot.desiredHeld,
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

    PollCommitSlot* PollCommitCoordinator::FindOrCreateSlot(RE::BSFixedString actionId)
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

    PollCommitSlot* PollCommitCoordinator::FindSlot(RE::BSFixedString actionId)
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
                ++slot.pending.suppressedPulseCount;
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
            ++slot.pending.suppressedPulseCount;
            ++slot.droppedPulseCount;
        }
    }

    void PollCommitCoordinator::QueueHoldSet(PollCommitSlot& slot, const PollCommitRequest& request)
    {
        slot.context = request.context;
        slot.desiredHeld = true;
        if (slot.state == ExecState::Idle) {
            slot.pending.kind = PendingKind::HoldStart;
            slot.pending.queuedAtUs = request.timestampUs != 0 ? request.timestampUs : _nowUs;
            slot.pending.epoch = slot.epoch;
        }
    }

    void PollCommitCoordinator::QueueHoldClear(PollCommitSlot& slot)
    {
        slot.desiredHeld = false;
        slot.pending.kind = PendingKind::HoldEnd;
    }

    void PollCommitCoordinator::QueueRepeatSet(PollCommitSlot& slot, const PollCommitRequest& request)
    {
        slot.context = request.context;
        slot.desiredHeld = true;
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
        slot.desiredHeld = false;
    }

    void PollCommitCoordinator::QueueForceCancel(PollCommitSlot& slot)
    {
        slot.pending.kind = PendingKind::ForceCancel;
        slot.desiredHeld = false;
    }

    void PollCommitCoordinator::TickSlot(PollCommitSlot& slot, std::uint64_t nowUs, bool gateOpen)
    {
        switch (slot.mode) {
        case PollCommitMode::Pulse:
            TickPulseSlot(slot, nowUs, gateOpen);
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
                nowUs >= slot.token.earliestReleaseAtUs) {
                TransitionState(slot, ExecState::ReleaseGap, nowUs);
            }
            break;

        case ExecState::ReleaseGap:
        case ExecState::HoldDownVisible:
        case ExecState::Cooldown:
        default:
            break;
        }
    }

    void PollCommitCoordinator::TickHoldSlot(PollCommitSlot& slot, std::uint64_t nowUs, bool gateOpen)
    {
        const auto canOpen = ShouldOpenGateForSlot(slot, gateOpen);

        switch (slot.state) {
        case ExecState::Idle:
            if (slot.pending.kind == PendingKind::ForceCancel) {
                slot.pending = {};
                break;
            }
            if (slot.desiredHeld) {
                if (canOpen) {
                    StartHoldTransaction(slot, nowUs);
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
            if (!slot.desiredHeld) {
                slot.pending.kind = PendingKind::None;
                TransitionState(slot, ExecState::Idle, nowUs);
                break;
            }

            if (canOpen) {
                StartHoldTransaction(slot, nowUs);
            }
            break;

        case ExecState::HoldDownVisible:
            if (slot.pending.kind == PendingKind::ForceCancel || !slot.desiredHeld) {
                TransitionState(slot, ExecState::ReleaseGap, nowUs);
            }
            break;

        case ExecState::PulseDownVisible:
        case ExecState::ReleaseGap:
        case ExecState::Cooldown:
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
            if (!slot.desiredHeld &&
                slot.token.active &&
                slot.token.downSubmitted) {
                TransitionState(slot, ExecState::ReleaseGap, nowUs);
            }
            break;

        case ExecState::PulseDownVisible:
        case ExecState::ReleaseGap:
        case ExecState::Cooldown:
        default:
            break;
        }
    }

    void PollCommitCoordinator::InvalidateStaleState(PollCommitSlot& slot)
    {
        slot.pending = {};
        slot.desiredHeld = false;
        slot.toggledOn = false;
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
        slot.token.downAtUs = nowUs;
        slot.token.earliestReleaseAtUs = nowUs + static_cast<std::uint64_t>(slot.minDownMs) * 1000ULL;
        TransitionState(slot, ExecState::PulseDownVisible, nowUs);
    }

    void PollCommitCoordinator::StartHoldTransaction(PollCommitSlot& slot, std::uint64_t nowUs)
    {
        ClearToken(slot);
        slot.token.active = true;
        slot.token.tokenId = _nextTokenId++;
        slot.token.epoch = slot.epoch != 0 ? slot.epoch : _currentEpoch;
        slot.token.downAtUs = nowUs;
        slot.token.earliestReleaseAtUs = nowUs;
        slot.pending.kind = PendingKind::None;
        TransitionState(slot, ExecState::HoldDownVisible, nowUs);
    }

    void PollCommitCoordinator::StartRepeatTransaction(PollCommitSlot& slot, std::uint64_t nowUs)
    {
        StartHoldTransaction(slot, nowUs);
        // Stage 2 repeat keeps the first visible edge and then relies on
        // sustained current-state down so Skyrim's native producer generates
        // subsequent repeat events. nextRepeatAtUs is retained as future
        // groundwork for an explicit cadence, but is not actively consumed by
        // TickRepeatSlot() yet.
        if (slot.repeatDelayMs != 0) {
            slot.nextRepeatAtUs = nowUs + static_cast<std::uint64_t>(slot.repeatDelayMs) * 1000ULL;
        }
    }

    void PollCommitCoordinator::CompleteRelease(PollCommitSlot& slot, std::uint64_t nowUs)
    {
        const auto mode = slot.mode;
        ClearToken(slot);

        if (mode == PollCommitMode::Pulse) {
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

        if (slot.desiredHeld) {
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
                TransitionState(slot, ExecState::WaitingForGate, nowUs);
            }
            return;
        }

        slot.pending.kind = PendingKind::None;
        TransitionState(slot, ExecState::Idle, nowUs);
    }

    void PollCommitCoordinator::ClearToken(PollCommitSlot& slot)
    {
        slot.token = {};
        slot.nextRepeatAtUs = 0;
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
             slot.desiredHeld ||
             slot.toggledOn ||
             slot.state != ExecState::Idle);
    }
}
