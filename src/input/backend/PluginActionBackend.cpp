#include "pch.h"
#include "input/backend/PluginActionBackend.h"

#include "input/ActionExecutor.h"

namespace dualpad::input::backend
{
    void PluginActionBackend::Dispatch(const FrameActionPlan& plan)
    {
        for (const auto& action : plan) {
            if (action.backend != PlannedBackend::Plugin) {
                continue;
            }

            if (action.phase != PlannedActionPhase::Pulse &&
                action.phase != PlannedActionPhase::Press) {
                continue;
            }

            ActionExecutor::GetSingleton().Execute(action.actionId, action.context);
        }
    }
}
