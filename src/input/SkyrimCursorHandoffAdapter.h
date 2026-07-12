#pragma once

#include "input_v2/presentation/PresentationProjection.h"

namespace dualpad::input
{
    class SkyrimCursorHandoffAdapter
    {
    public:
        [[nodiscard]] input_v2::presentation::CursorHandoffAck ExecuteVerifiedHandoff(
            const input_v2::presentation::CursorHandoffPlan& plan) noexcept;
    };
}
