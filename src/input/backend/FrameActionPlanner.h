#pragma once

#include "input_v2/compat/LegacyInputContextCompat.h"
#include "input/backend/ActionBackendPolicy.h"
#include "input/backend/FrameActionPlan.h"
#include "input/PadEvent.h"

#include <string_view>

namespace dualpad::input::backend
{
    struct ResolvedBinding
    {
        std::string actionId;
        Trigger trigger{};
        bool ambiguous{ false };
    };

    class FrameActionPlanner
    {
    public:
        bool PlanResolvedEvent(
            const ResolvedBinding& binding,
            const PadEvent& event,
            InputContext context,
            std::uint32_t contextEpoch,
            FrameActionPlan& outPlan) const;

        bool PlanButtonState(
            std::string_view actionId,
            bool down,
            float heldSeconds,
            std::uint32_t sourceCode,
            InputContext context,
            std::uint32_t contextEpoch,
            FrameActionPlan& outPlan) const;

        bool PlanAxisValue(
            std::string_view actionId,
            float valueX,
            float valueY,
            std::uint32_t sourceCode,
            InputContext context,
            std::uint32_t contextEpoch,
            FrameActionPlan& outPlan) const;
    };
}
