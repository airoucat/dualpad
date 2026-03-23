#pragma once

#include "input/backend/ActionLifecyclePolicy.h"
#include "input/backend/ActionOutputContract.h"
#include "input/backend/FrameActionPlan.h"
#include "input/backend/NativeControlCode.h"

#include <string_view>

namespace dualpad::input::backend
{
    struct ActionRoutingDecision
    {
        PlannedBackend backend{ PlannedBackend::None };
        PlannedActionKind kind{ PlannedActionKind::NativeButton };
        ActionOutputContract contract{ ActionOutputContract::Pulse };
        ActionLifecyclePolicy lifecyclePolicy{ ActionLifecyclePolicy::None };
        NativeControlCode nativeCode{ NativeControlCode::None };
        bool ownsLifecycle{ false };
    };

    class ActionBackendPolicy
    {
    public:
        static ActionRoutingDecision Decide(std::string_view actionId);
        static bool IsPluginAction(std::string_view actionId);
        static bool IsLikelyModAction(std::string_view actionId);
    };
}
