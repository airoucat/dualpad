#pragma once

#include "input/AuthoritativePollState.h"
#include "input/backend/FrameActionPlan.h"

namespace dualpad::input::backend
{
    void LogFrameActionPlan(const FrameActionPlan& plan);
    void LogAuthoritativePollFrame(const AuthoritativePollFrame& frame);
}
