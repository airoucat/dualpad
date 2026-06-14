#include "pch.h"
#include "input/backend/ActionLifecycleCoordinator.h"

#include "input/Action.h"
#include "input_v2/compat/LegacyInputContextCompat.h"
#include "input/backend/NativeDigitalPolicyResolver.h"
#include "input/PadEvent.h"

#include <bit>

namespace dualpad::input::backend
{
    using namespace std::literals;

    namespace
    {
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
        activeAction.suppressNextPress = false;
        activeAction.actionId = actionId;
        activeAction.routingDecision = routingDecision;
        return true;
    }

    bool ActionLifecycleCoordinator::RegisterRecoveredOwningAction(
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
        activeAction.suppressNextPress = true;
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
        std::uint32_t contextEpoch,
        FrameActionPlan& outPlan)
    {
        std::uint32_t releasedSourceMask = 0;
        LifecycleTransactionBuffer transactions{};

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
        ActiveSourceAction& activeAction,
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
            const bool suppressNextPress = activeAction.suppressNextPress;
            activeAction.suppressNextPress = false;
            transaction.phase =
                (SawPressEdge(button) && !suppressNextPress) ?
                    PlannedActionPhase::Press :
                    PlannedActionPhase::Hold;
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
        action.digitalPolicy = ResolveNativeDigitalPolicy(
            action.backend,
            action.kind,
            action.contract,
            action.lifecyclePolicy);
        action.gateAware = IsNativeDigitalGateAwareAction(action.actionId, action.digitalPolicy);
        action.minDownMs = ResolveNativeMinDownMs(action.digitalPolicy);
        action.repeatDelayMs = ResolveNativeRepeatDelayMs(action.digitalPolicy);
        action.repeatIntervalMs = ResolveNativeRepeatIntervalMs(action.digitalPolicy);
        action.contextEpoch = transaction.contextEpoch;
        return action;
    }
}
