#pragma once

#include "input/injection/PollMaterializationReceipt.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace dualpad::input_v2::gameplay
{
    enum class CurrentCycleChannel : std::uint8_t
    {
        Look = 1 << 0,
        Move = 1 << 1,
        Combat = 1 << 2,
        TransientDigital = 1 << 3,
        SustainedDigital = 1 << 4
    };

    using CurrentCycleChannelMaskType = std::uint8_t;

    constexpr CurrentCycleChannelMaskType CurrentCycleChannelMask(CurrentCycleChannel channel) noexcept
    {
        return static_cast<CurrentCycleChannelMaskType>(channel);
    }

    enum class CurrentCycleEventDisposition : std::uint8_t
    {
        Keep = 0,
        Neutralize,
        Suppress,
        CancelAndSuppress
    };

    enum class CurrentCycleGateFailure : std::uint8_t
    {
        None = 0,
        MissingReceipt,
        AmbiguousReceipt,
        AlreadyConsumedReceipt,
        ThreadMismatch,
        PollFrameMismatch,
        InputStateEpochMismatch,
        GamepadSessionMismatch,
        ContextMismatch,
        ControlMapMismatch,
        CutoffMismatch,
        PhysicalFactsIncomplete,
        RouteUnavailable,
        AdapterFailure,
        ConsumerOrderUnproven,
        ScratchCapacityExceeded
    };

    struct CurrentCycleGateInput
    {
        std::optional<dualpad::input::PollMaterializationReceipt> receipt;
        dualpad::input::PollReceiptConsumeFailure receiptFailure{
            dualpad::input::PollReceiptConsumeFailure::None
        };
        dualpad::input::PollFrameIdentity observedIdentity{};
        bool physicalFactsComplete{ false };
        bool routeAvailable{ false };
        bool consumerOrderProven{ false };
        bool mutationCapabilityEnabled{ false };
        bool scratchCapacitySufficient{ false };
        bool physicalLookActivation{ false };
        bool physicalMoveActivation{ false };
        bool physicalCombatActivation{ false };
        bool physicalTransientActivation{ false };
        bool materializedLookEvent{ false };
        bool materializedMoveEvent{ false };
        bool materializedCombatEvent{ false };
        bool materializedTransientEvent{ false };
    };

    struct CurrentCycleGatePlan
    {
        CurrentCycleEventDisposition look{ CurrentCycleEventDisposition::Keep };
        CurrentCycleEventDisposition move{ CurrentCycleEventDisposition::Keep };
        CurrentCycleEventDisposition combat{ CurrentCycleEventDisposition::Keep };
        CurrentCycleEventDisposition transientDigital{ CurrentCycleEventDisposition::Keep };
        CurrentCycleEventDisposition sustainedDigital{ CurrentCycleEventDisposition::Keep };
        CurrentCycleGateFailure failure{ CurrentCycleGateFailure::None };
        CurrentCycleChannelMaskType affectedChannels{ 0 };
        bool requiresEventMutation{ false };
        bool commitCurrentCycleSensitiveState{ false };
        std::uint8_t currentEventWriterCount{ 0 };
        std::uint8_t nextPollWriterCount{ 0 };
    };

    struct CurrentCycleAdapterAudit
    {
        bool success{ false };
        bool shadowOnly{ true };
        bool mutationApplied{ false };
        CurrentCycleGateFailure failure{ CurrentCycleGateFailure::None };
        CurrentCycleChannelMaskType affectedChannels{ 0 };
        std::size_t wouldMutateCount{ 0 };
        std::uint8_t currentEventWriterCount{ 0 };
    };

    CurrentCycleGatePlan BuildCurrentCycleGatePlan(const CurrentCycleGateInput& input) noexcept;
    bool IsCurrentCycleAuditCommitSafe(
        const CurrentCycleGatePlan& plan,
        const CurrentCycleAdapterAudit& audit) noexcept;
}
