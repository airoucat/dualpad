#include "pch.h"
#include "input/backend/FrameActionPlanner.h"

namespace dualpad::input::backend
{
    namespace
    {
        PlannedActionPhase PhaseFromEvent(const PadEvent& event)
        {
            switch (event.type) {
            case PadEventType::ButtonPress:
                return PlannedActionPhase::Press;

            case PadEventType::Hold:
                return PlannedActionPhase::Hold;

            case PadEventType::Combo:
            case PadEventType::Tap:
            case PadEventType::Gesture:
            case PadEventType::TouchpadPress:
            case PadEventType::TouchpadSlide:
                return PlannedActionPhase::Pulse;

            case PadEventType::ButtonRelease:
            case PadEventType::TouchpadRelease:
                return PlannedActionPhase::Release;

            case PadEventType::AxisChange:
                return PlannedActionPhase::Value;

            case PadEventType::None:
            default:
                return PlannedActionPhase::None;
            }
        }
    }

    bool FrameActionPlanner::PlanResolvedEvent(
        const ResolvedBinding& binding,
        const PadEvent& event,
        InputContext context,
        FrameActionPlan& outPlan) const
    {
        const auto decision = ActionBackendPolicy::Decide(binding.actionId);
        auto action = PlannedAction{};
        action.backend = decision.backend;
        action.kind = decision.kind;
        action.phase = PhaseFromEvent(event);
        action.context = context;
        action.actionId = binding.actionId;
        action.sourceCode = event.code;
        action.outputCode = static_cast<std::uint32_t>(decision.nativeCode);
        action.modifierMask = event.modifierMask;
        action.timestampUs = event.timestampUs;
        action.valueX = event.value;
        action.lifecycle = decision.lifecycle;

        if (action.phase == PlannedActionPhase::None) {
            return false;
        }

        return outPlan.Push(action);
    }

    bool FrameActionPlanner::PlanButtonState(
        std::string_view actionId,
        bool down,
        float heldSeconds,
        std::uint32_t sourceCode,
        InputContext context,
        FrameActionPlan& outPlan) const
    {
        const auto decision = ActionBackendPolicy::Decide(actionId);
        auto action = PlannedAction{};
        action.backend = decision.backend;
        action.kind = decision.kind;
        action.phase = down ? (heldSeconds > 0.0f ? PlannedActionPhase::Hold : PlannedActionPhase::Press) : PlannedActionPhase::Release;
        action.context = context;
        action.actionId = actionId;
        action.sourceCode = sourceCode;
        action.outputCode = static_cast<std::uint32_t>(decision.nativeCode);
        action.heldSeconds = heldSeconds;
        action.lifecycle = decision.lifecycle;
        return outPlan.Push(action);
    }

    bool FrameActionPlanner::PlanAxisValue(
        std::string_view actionId,
        float valueX,
        float valueY,
        std::uint32_t sourceCode,
        InputContext context,
        FrameActionPlan& outPlan) const
    {
        const auto decision = ActionBackendPolicy::Decide(actionId);
        auto action = PlannedAction{};
        action.backend = decision.backend;
        action.kind = decision.kind;
        action.phase = PlannedActionPhase::Value;
        action.context = context;
        action.actionId = actionId;
        action.sourceCode = sourceCode;
        action.outputCode = static_cast<std::uint32_t>(decision.nativeCode);
        action.valueX = valueX;
        action.valueY = valueY;
        action.lifecycle = decision.lifecycle;
        return outPlan.Push(action);
    }
}
