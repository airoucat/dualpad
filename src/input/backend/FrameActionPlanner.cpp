#include "pch.h"
#include "input/backend/FrameActionPlanner.h"

#include "input/Action.h"
#include "input_v2/compat/LegacyInputContextCompat.h"
#include "input/backend/NativeDigitalPolicyResolver.h"

namespace dualpad::input::backend
{
    using namespace std::literals;

    namespace
    {
        bool IsDispatchableRoute(const ActionRoutingDecision& decision)
        {
            return decision.backend != PlannedBackend::None;
        }

        PlannedActionPhase PhaseFromEvent(const PadEvent& event)
        {
            switch (event.type) {
            case PadEventType::ButtonPress:
            case PadEventType::Layer:
            case PadEventType::Combo:
            case PadEventType::Hold:
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

        void ApplyDigitalMetadata(PlannedAction& action, std::uint32_t contextEpoch)
        {
            action.digitalPolicy = ResolveNativeDigitalPolicy(
                action.backend,
                action.kind,
                action.contract,
                action.lifecyclePolicy);
            action.gateAware = IsNativeDigitalGateAwareAction(action.actionId, action.digitalPolicy);
            action.minDownMs = ResolveNativeMinDownMs(action.digitalPolicy);
            action.repeatDelayMs = ResolveNativeRepeatDelayMs(action.digitalPolicy);
            action.repeatIntervalMs = ResolveNativeRepeatIntervalMs(action.digitalPolicy);
            action.contextEpoch = contextEpoch;
        }
    }

    bool FrameActionPlanner::PlanResolvedEvent(
        const ResolvedBinding& binding,
        const PadEvent& event,
        InputContext context,
        std::uint32_t contextEpoch,
        FrameActionPlan& outPlan) const
    {
        const auto decision = ActionBackendPolicy::Decide(binding.actionId);
        if (!IsDispatchableRoute(decision)) {
            return false;
        }
        auto action = PlannedAction{};
        action.backend = decision.backend;
        action.kind = decision.kind;
        action.phase = PhaseFromEvent(event);
        action.context = context;
        action.actionId = binding.actionId;
        action.contract = decision.contract;
        action.lifecyclePolicy = decision.lifecyclePolicy;
        action.sourceCode = event.code;
        action.outputCode = static_cast<std::uint32_t>(decision.nativeCode);
        action.modifierMask = event.modifierMask;
        action.timestampUs = event.timestampUs;
        action.valueX = event.value;
        ApplyDigitalMetadata(action, contextEpoch);

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
        std::uint32_t contextEpoch,
        FrameActionPlan& outPlan) const
    {
        const auto decision = ActionBackendPolicy::Decide(actionId);
        if (!IsDispatchableRoute(decision)) {
            return false;
        }
        auto action = PlannedAction{};
        action.backend = decision.backend;
        action.kind = decision.kind;
        action.phase = down ? (heldSeconds > 0.0f ? PlannedActionPhase::Hold : PlannedActionPhase::Press) : PlannedActionPhase::Release;
        action.context = context;
        action.actionId = actionId;
        action.contract = decision.contract;
        action.lifecyclePolicy = decision.lifecyclePolicy;
        action.sourceCode = sourceCode;
        action.outputCode = static_cast<std::uint32_t>(decision.nativeCode);
        action.heldSeconds = heldSeconds;
        ApplyDigitalMetadata(action, contextEpoch);
        return outPlan.Push(action);
    }

    bool FrameActionPlanner::PlanAxisValue(
        std::string_view actionId,
        float valueX,
        float valueY,
        std::uint32_t sourceCode,
        InputContext context,
        std::uint32_t contextEpoch,
        FrameActionPlan& outPlan) const
    {
        const auto decision = ActionBackendPolicy::Decide(actionId);
        if (!IsDispatchableRoute(decision)) {
            return false;
        }
        auto action = PlannedAction{};
        action.backend = decision.backend;
        action.kind = decision.kind;
        action.phase = PlannedActionPhase::Value;
        action.context = context;
        action.actionId = actionId;
        action.contract = decision.contract;
        action.lifecyclePolicy = decision.lifecyclePolicy;
        action.sourceCode = sourceCode;
        action.outputCode = static_cast<std::uint32_t>(decision.nativeCode);
        action.valueX = valueX;
        action.valueY = valueY;
        ApplyDigitalMetadata(action, contextEpoch);
        return outPlan.Push(action);
    }
}
