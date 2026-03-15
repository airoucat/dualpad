#include "pch.h"
#include "input/backend/ActionLifecycleCoordinator.h"

#include "input/Action.h"
#include "input/InputContext.h"
#include "input/mapping/PadEvent.h"

#include <bit>

namespace dualpad::input::backend
{
    using namespace std::literals;

    namespace
    {
        constexpr std::uint32_t kDefaultPulseMinDownMs = 40;
        constexpr std::uint32_t kDefaultRepeatDelayMs = 350;
        constexpr std::uint32_t kDefaultRepeatIntervalMs = 75;

        NativeDigitalPolicyKind ResolveDigitalPolicy(
            std::string_view actionId,
            PlannedBackend backend,
            PlannedActionKind kind,
            ActionOutputContract contract)
        {
            (void)actionId;
            if (backend != PlannedBackend::ButtonEvent ||
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

        std::uint32_t ResolveMinDownMs(NativeDigitalPolicyKind policy)
        {
            return policy == NativeDigitalPolicyKind::PulseMinDown ? kDefaultPulseMinDownMs : 0;
        }

        std::uint32_t ResolveRepeatDelayMs(NativeDigitalPolicyKind policy)
        {
            return policy == NativeDigitalPolicyKind::RepeatOwner ? kDefaultRepeatDelayMs : 0;
        }

        std::uint32_t ResolveRepeatIntervalMs(NativeDigitalPolicyKind policy)
        {
            return policy == NativeDigitalPolicyKind::RepeatOwner ? kDefaultRepeatIntervalMs : 0;
        }

        bool SawPressEdge(const SyntheticButtonState& button)
        {
            return button.sawPressEdge || button.pressed;
        }

        float ResolveReleaseHeldSeconds(const SyntheticButtonState& button)
        {
            const auto releasedAtUs = button.lastReleaseUs != 0 ? button.lastReleaseUs : button.releasedAtUs;
            const auto pressedAtUs = button.firstPressUs != 0 ? button.firstPressUs : button.pressedAtUs;
            if (releasedAtUs > pressedAtUs &&
                pressedAtUs != 0) {
                return static_cast<float>(releasedAtUs - pressedAtUs) / 1000000.0f;
            }

            return button.heldSeconds;
        }
    }

    void ActionLifecycleCoordinator::Reset()
    {
        _activeActions = {};
    }

    bool ActionLifecycleCoordinator::RegisterOwningAction(
        std::uint32_t sourceCode,
        std::string_view actionId,
        const ActionRoutingDecision& routingDecision)
    {
        if (!IsSyntheticPadBitCode(sourceCode) ||
            !routingDecision.ownsLifecycle) {
            return false;
        }

        auto& activeAction = _activeActions[BitIndex(sourceCode)];
        activeAction.active = true;
        activeAction.actionId = actionId;
        activeAction.routingDecision = routingDecision;
        return true;
    }

    bool ActionLifecycleCoordinator::ReleaseOwningAction(
        std::uint32_t sourceCode,
        std::uint64_t timestampUs,
        InputContext context,
        std::uint32_t contextEpoch,
        FrameActionPlan& outPlan)
    {
        if (!IsSyntheticPadBitCode(sourceCode)) {
            return false;
        }

        auto& activeAction = _activeActions[BitIndex(sourceCode)];
        if (!activeAction.active) {
            return false;
        }

        LifecycleTransaction transaction{};
        transaction.actionId = activeAction.actionId;
        transaction.routingDecision = activeAction.routingDecision;
        transaction.phase = PlannedActionPhase::Release;
        transaction.sourceCode = sourceCode;
        transaction.timestampUs = timestampUs;
        transaction.context = context;
        transaction.contextEpoch = contextEpoch;

        const auto pushed = outPlan.Push(BuildLifecycleAction(transaction));
        activeAction = {};
        return pushed;
    }

    std::uint32_t ActionLifecycleCoordinator::PlanFrame(
        const SyntheticPadFrame& frame,
        InputContext context,
        FrameActionPlan& outPlan)
    {
        std::uint32_t releasedSourceMask = 0;
        LifecycleTransactionBuffer transactions{};
        const auto contextEpoch = ContextManager::GetSingleton().GetCurrentEpoch();

        for (std::size_t bitIndex = 0; bitIndex < _activeActions.size(); ++bitIndex) {
            auto& activeAction = _activeActions[bitIndex];
            if (!activeAction.active) {
                continue;
            }

            const auto sourceCode = (1u << bitIndex);
            const auto& button = frame.buttons[bitIndex];
            BuildLifecycleTransaction(
                activeAction,
                button,
                sourceCode,
                frame.sourceTimestampUs,
                context,
                contextEpoch,
                transactions);

            if (button.down ||
                SawPressEdge(button)) {
                continue;
            }

            releasedSourceMask |= sourceCode;
            activeAction = {};
        }

        for (const auto& transaction : transactions) {
            outPlan.Push(BuildLifecycleAction(transaction));
        }

        return releasedSourceMask;
    }

    std::size_t ActionLifecycleCoordinator::BitIndex(std::uint32_t sourceCode)
    {
        return static_cast<std::size_t>(std::countr_zero(sourceCode));
    }

    bool ActionLifecycleCoordinator::BuildLifecycleTransaction(
        const ActiveSourceAction& activeAction,
        const SyntheticButtonState& button,
        std::uint32_t sourceCode,
        std::uint64_t timestampUs,
        InputContext context,
        std::uint32_t contextEpoch,
        LifecycleTransactionBuffer& outTransactions)
    {
        LifecycleTransaction transaction{};
        transaction.actionId = activeAction.actionId;
        transaction.routingDecision = activeAction.routingDecision;
        transaction.sourceCode = sourceCode;
        transaction.timestampUs = timestampUs;
        transaction.context = context;
        transaction.contextEpoch = contextEpoch;

        if (button.down) {
            transaction.phase = SawPressEdge(button) ? PlannedActionPhase::Press : PlannedActionPhase::Hold;
            transaction.heldSeconds = transaction.phase == PlannedActionPhase::Press ? 0.0f : button.heldSeconds;
            return outTransactions.Push(transaction);
        }

        if (SawPressEdge(button)) {
            transaction.phase = PlannedActionPhase::Press;
            transaction.heldSeconds = 0.0f;
            return outTransactions.Push(transaction);
        }

        transaction.phase = PlannedActionPhase::Release;
        transaction.heldSeconds = ResolveReleaseHeldSeconds(button);
        return outTransactions.Push(transaction);
    }

    PlannedAction ActionLifecycleCoordinator::BuildLifecycleAction(
        const LifecycleTransaction& transaction)
    {
        PlannedAction action{};
        action.backend = transaction.routingDecision.backend;
        action.kind = transaction.routingDecision.kind;
        action.phase = transaction.phase;
        action.context = transaction.context;
        action.actionId = transaction.actionId;
        action.contract = transaction.routingDecision.contract;
        action.lifecyclePolicy = transaction.routingDecision.lifecyclePolicy;
        action.sourceCode = transaction.sourceCode;
        action.outputCode = static_cast<std::uint32_t>(transaction.routingDecision.nativeCode);
        action.timestampUs = transaction.timestampUs;
        action.heldSeconds = transaction.heldSeconds;
        action.digitalPolicy = ResolveDigitalPolicy(
            action.actionId,
            action.backend,
            action.kind,
            action.contract);
        action.gateAware = ResolveGateAware(action.actionId, action.digitalPolicy);
        action.minDownMs = ResolveMinDownMs(action.digitalPolicy);
        action.repeatDelayMs = ResolveRepeatDelayMs(action.digitalPolicy);
        action.repeatIntervalMs = ResolveRepeatIntervalMs(action.digitalPolicy);
        action.contextEpoch = transaction.contextEpoch;
        return action;
    }
}
