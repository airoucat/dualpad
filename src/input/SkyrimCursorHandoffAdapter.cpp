#include "pch.h"

#include "input/SkyrimCursorHandoffAdapter.h"

namespace dualpad::input
{
    input_v2::presentation::CursorHandoffAck SkyrimCursorHandoffAdapter::ExecuteVerifiedHandoff(
        const input_v2::presentation::CursorHandoffPlan& plan) noexcept
    {
        // Gate I-CURSOR is not closed. Keep the platform seam observable but
        // perform no coordinate read/write and never claim synchronization.
        return input_v2::presentation::CursorHandoffAck{
            .token = plan.token,
            .targetMenuInstanceId = plan.targetMenuInstanceId,
            .contextRevision = plan.contextRevision,
            .presentationEpoch = plan.presentationEpoch,
            .positionSynchronized = false,
            .failure = input_v2::presentation::CursorHandoffFailure::MappingUnverified
        };
    }
}
