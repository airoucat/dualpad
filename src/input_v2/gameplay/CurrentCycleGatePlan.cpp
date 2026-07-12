#include "pch.h"

#include "input_v2/gameplay/CurrentCycleGatePlan.h"

namespace dualpad::input_v2::gameplay
{
    namespace
    {
        CurrentCycleGateFailure ReceiptFailure(dualpad::input::PollReceiptConsumeFailure failure)
        {
            using dualpad::input::PollReceiptConsumeFailure;
            switch (failure) {
            case PollReceiptConsumeFailure::Ambiguous:
                return CurrentCycleGateFailure::AmbiguousReceipt;
            case PollReceiptConsumeFailure::AlreadyConsumed:
                return CurrentCycleGateFailure::AlreadyConsumedReceipt;
            case PollReceiptConsumeFailure::ThreadMismatch:
                return CurrentCycleGateFailure::ThreadMismatch;
            case PollReceiptConsumeFailure::Missing:
                return CurrentCycleGateFailure::MissingReceipt;
            case PollReceiptConsumeFailure::None:
            default:
                return CurrentCycleGateFailure::None;
            }
        }

        CurrentCycleGateFailure IdentityFailure(
            const dualpad::input::PollFrameIdentity& materialized,
            const dualpad::input::PollFrameIdentity& observed)
        {
            if (materialized.publicationGeneration != observed.publicationGeneration ||
                materialized.runtimeGeneration != observed.runtimeGeneration ||
                materialized.packetNumber != observed.packetNumber) {
                return CurrentCycleGateFailure::PollFrameMismatch;
            }
            if (materialized.inputStateEpoch != observed.inputStateEpoch) {
                return CurrentCycleGateFailure::InputStateEpochMismatch;
            }
            if (materialized.gamepadSessionId != observed.gamepadSessionId) {
                return CurrentCycleGateFailure::GamepadSessionMismatch;
            }
            if (materialized.contextRevision != observed.contextRevision) {
                return CurrentCycleGateFailure::ContextMismatch;
            }
            if (materialized.controlMapRevision != observed.controlMapRevision) {
                return CurrentCycleGateFailure::ControlMapMismatch;
            }
            if (materialized.orderedCutoffSeq != observed.orderedCutoffSeq ||
                materialized.eventBatchToken != observed.eventBatchToken) {
                return CurrentCycleGateFailure::CutoffMismatch;
            }
            return CurrentCycleGateFailure::None;
        }
    }

    CurrentCycleGatePlan BuildCurrentCycleGatePlan(const CurrentCycleGateInput& input) noexcept
    {
        CurrentCycleGatePlan plan{};
        if (!input.receipt) {
            plan.failure = ReceiptFailure(input.receiptFailure);
            if (plan.failure == CurrentCycleGateFailure::None) {
                plan.failure = CurrentCycleGateFailure::MissingReceipt;
            }
            return plan;
        }
        if ((plan.failure = IdentityFailure(input.receipt->identity, input.observedIdentity)) !=
            CurrentCycleGateFailure::None) {
            return plan;
        }
        if (!input.physicalFactsComplete) {
            plan.failure = CurrentCycleGateFailure::PhysicalFactsIncomplete;
            return plan;
        }
        if (!input.routeAvailable) {
            plan.failure = CurrentCycleGateFailure::RouteUnavailable;
            return plan;
        }
        if (input.mutationCapabilityEnabled && !input.consumerOrderProven) {
            plan.failure = CurrentCycleGateFailure::ConsumerOrderUnproven;
            return plan;
        }
        if (!input.scratchCapacitySufficient) {
            plan.failure = CurrentCycleGateFailure::ScratchCapacityExceeded;
            return plan;
        }

        const auto apply = [&](bool physicalActivation,
                               bool materialized,
                               CurrentCycleChannel channel,
                               CurrentCycleEventDisposition disposition,
                               CurrentCycleEventDisposition& target) {
            if (!physicalActivation || !materialized) {
                return;
            }
            target = disposition;
            plan.affectedChannels |= CurrentCycleChannelMask(channel);
            plan.requiresEventMutation = true;
        };
        apply(input.physicalLookActivation, input.materializedLookEvent,
            CurrentCycleChannel::Look, CurrentCycleEventDisposition::Neutralize, plan.look);
        apply(input.physicalMoveActivation, input.materializedMoveEvent,
            CurrentCycleChannel::Move, CurrentCycleEventDisposition::Neutralize, plan.move);
        apply(input.physicalCombatActivation, input.materializedCombatEvent,
            CurrentCycleChannel::Combat, CurrentCycleEventDisposition::Neutralize, plan.combat);
        apply(input.physicalTransientActivation, input.materializedTransientEvent,
            CurrentCycleChannel::TransientDigital,
            CurrentCycleEventDisposition::CancelAndSuppress,
            plan.transientDigital);

        plan.currentEventWriterCount = plan.requiresEventMutation ? 1 : 0;
        plan.nextPollWriterCount = plan.affectedChannels != 0 ? 1 : 0;
        plan.commitCurrentCycleSensitiveState = !plan.requiresEventMutation;
        return plan;
    }

    bool IsCurrentCycleAuditCommitSafe(
        const CurrentCycleGatePlan& plan,
        const CurrentCycleAdapterAudit& audit) noexcept
    {
        if (!plan.requiresEventMutation) {
            return audit.success &&
                (plan.commitCurrentCycleSensitiveState || plan.affectedChannels == 0);
        }
        return audit.success && audit.mutationApplied && !audit.shadowOnly;
    }
}
