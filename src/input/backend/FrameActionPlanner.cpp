#include "pch.h"
#include "input/backend/FrameActionPlanner.h"

#include "input/Action.h"
#include "input/InputContext.h"

namespace dualpad::input::backend
{
    using namespace std::literals;

    namespace
    {
        constexpr std::uint32_t kDefaultPulseMinDownMs = 40;
        constexpr std::uint32_t kDefaultRepeatDelayMs = 350;
        constexpr std::uint32_t kDefaultRepeatIntervalMs = 75;

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

        NativeDigitalPolicyKind ResolveDigitalPolicy(
            std::string_view actionId,
            PlannedBackend backend,
            PlannedActionKind kind,
            ActionOutputContract contract)
        {
            (void)actionId;
            if (backend != PlannedBackend::NativeButtonCommit ||
                kind != PlannedActionKind::NativeButton) {
                return NativeDigitalPolicyKind::None;
            }

            switch (contract) {
            case ActionOutputContract::Hold:
                return NativeDigitalPolicyKind::HoldOwner;
            case ActionOutputContract::Repeat:
                return NativeDigitalPolicyKind::RepeatOwner;
            case ActionOutputContract::Toggle:
                return NativeDigitalPolicyKind::ToggleDebounced;
            case ActionOutputContract::Pulse:
                return NativeDigitalPolicyKind::PulseMinDown;
            case ActionOutputContract::Axis:
            case ActionOutputContract::None:
            default:
                return NativeDigitalPolicyKind::None;
            }
        }

        bool ResolveGateAware(std::string_view actionId, NativeDigitalPolicyKind policy)
        {
            if (policy == NativeDigitalPolicyKind::None) {
                return false;
            }

            return actionId == actions::Jump ||
                actionId == actions::Activate ||
                actionId == actions::Sprint;
        }

        std::uint32_t ResolveMinDownMs(std::string_view actionId, NativeDigitalPolicyKind policy)
        {
            (void)actionId;
            if (policy != NativeDigitalPolicyKind::PulseMinDown) {
                return 0;
            }

            return kDefaultPulseMinDownMs;
        }

        std::uint32_t ResolveRepeatDelayMs(NativeDigitalPolicyKind policy)
        {
            return policy == NativeDigitalPolicyKind::RepeatOwner ? kDefaultRepeatDelayMs : 0;
        }

        std::uint32_t ResolveRepeatIntervalMs(NativeDigitalPolicyKind policy)
        {
            return policy == NativeDigitalPolicyKind::RepeatOwner ? kDefaultRepeatIntervalMs : 0;
        }

        void ApplyDigitalMetadata(PlannedAction& action, std::uint32_t contextEpoch)
        {
            action.digitalPolicy = ResolveDigitalPolicy(
                action.actionId,
                action.backend,
                action.kind,
                action.contract);
            action.gateAware = ResolveGateAware(action.actionId, action.digitalPolicy);
            action.minDownMs = ResolveMinDownMs(action.actionId, action.digitalPolicy);
            action.repeatDelayMs = ResolveRepeatDelayMs(action.digitalPolicy);
            action.repeatIntervalMs = ResolveRepeatIntervalMs(action.digitalPolicy);
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
