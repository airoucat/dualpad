#pragma once

#include "input_v2/presentation/PresentationProjection.h"

namespace dualpad::input_v2::presentation
{
    [[nodiscard]] CursorHandoffPlan BuildCursorHandoffPlan(
        CursorOwner from,
        CursorOwner to,
        std::uint64_t token,
        std::uint32_t contextRevision,
        std::uint32_t presentationEpoch,
        const context::ResolvedContextSnapshot& context) noexcept;

    [[nodiscard]] bool CursorHandoffAckMatches(
        const CursorHandoffPlan& plan,
        const CursorHandoffAck& ack) noexcept;
}
