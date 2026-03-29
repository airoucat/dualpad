#pragma once

#include "input/InputContext.h"
#include "input/backend/ActionBackendPolicy.h"
#include "input/backend/FrameActionPlan.h"
#include "input/mapping/BindingResolver.h"
#include "input/mapping/PadEvent.h"

#include <string_view>

namespace dualpad::input::backend
{
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
