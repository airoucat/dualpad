#pragma once

#include "input/InputContext.h"
#include "input/backend/FrameActionPlan.h"
#include "input_v2/actions/InteractionEngine.h"

namespace dualpad::input_v2::actions
{
    class LegacyLifecycleBridge
    {
    public:
        static dualpad::input::backend::FrameActionPlan BuildShadowFrameActionPlan(
            const ResolvedActionFrame& resolved,
            dualpad::input::InputContext legacyContext);
    };
}
