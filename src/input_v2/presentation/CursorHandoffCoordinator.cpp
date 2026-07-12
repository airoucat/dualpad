#include "pch.h"

#include "input_v2/presentation/CursorHandoffCoordinator.h"

namespace dualpad::input_v2::presentation
{
    CursorHandoffPlan BuildCursorHandoffPlan(
        CursorOwner from,
        CursorOwner to,
        std::uint64_t token,
        std::uint32_t contextRevision,
        std::uint32_t presentationEpoch,
        const context::ResolvedContextSnapshot& context) noexcept
    {
        return CursorHandoffPlan{
            .token = token,
            .from = from,
            .to = to,
            .contextRevision = contextRevision,
            .presentationEpoch = presentationEpoch,
            .targetMenuInstanceId = context.topMenuInstanceId.value_or(0),
            .targetMenuPtr = context.topMenuPtr,
            .targetMoviePtr = context.topMenuMoviePtr
        };
    }

    bool CursorHandoffAckMatches(
        const CursorHandoffPlan& plan,
        const CursorHandoffAck& ack) noexcept
    {
        return ack.failure == CursorHandoffFailure::None &&
            ack.positionSynchronized &&
            ack.token == plan.token &&
            ack.targetMenuInstanceId == plan.targetMenuInstanceId &&
            ack.contextRevision == plan.contextRevision &&
            ack.presentationEpoch == plan.presentationEpoch;
    }
}
