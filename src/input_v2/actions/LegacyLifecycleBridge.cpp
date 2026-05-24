#include "pch.h"

#include "input_v2/actions/LegacyLifecycleBridge.h"

namespace dualpad::input_v2::actions
{
    namespace
    {
        dualpad::input::backend::PlannedActionPhase ToLegacyPhase(ActionPhase phase)
        {
            using dualpad::input::backend::PlannedActionPhase;
            switch (phase) {
            case ActionPhase::Press:
                return PlannedActionPhase::Press;
            case ActionPhase::Hold:
            case ActionPhase::Repeat:
                return PlannedActionPhase::Hold;
            case ActionPhase::Release:
                return PlannedActionPhase::Release;
            case ActionPhase::Pulse:
                return PlannedActionPhase::Pulse;
            case ActionPhase::Value:
                return PlannedActionPhase::Value;
            default:
                return PlannedActionPhase::None;
            }
        }
    }

    dualpad::input::backend::FrameActionPlan LegacyLifecycleBridge::BuildShadowFrameActionPlan(
        const ResolvedActionFrame& resolved,
        dualpad::input::InputContext legacyContext)
    {
        dualpad::input::backend::FrameActionPlan plan;
        for (const auto& change : resolved.changes) {
            dualpad::input::backend::PlannedAction action{};
            action.actionId = change.actionId;
            action.context = legacyContext;
            action.phase = ToLegacyPhase(change.phase);
            action.sourceCode = change.bindingId;
            action.timestampUs = change.timestampUs;
            action.contextEpoch = resolved.contextRevision;
            plan.Push(action);
        }
        return plan;
    }
}
