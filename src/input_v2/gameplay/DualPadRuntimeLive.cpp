#include "pch.h"

#include "input_v2/gameplay/DualPadRuntime.h"

#include "input/AuthoritativePollState.h"
#include "input/backend/ActionBackendPolicy.h"
#include "input/backend/KeyboardHelperBackend.h"
#include "input/backend/ModEventKeyPool.h"
#include "input/backend/NativeButtonCommitBackend.h"

namespace dualpad::input_v2::gameplay
{
    namespace
    {
        using dualpad::input::backend::ActionOutputContract;
        using dualpad::input::backend::NativeDigitalPolicyKind;
        using dualpad::input::backend::PlannedAction;
        using dualpad::input::backend::PlannedActionKind;
        using dualpad::input::backend::PlannedActionPhase;
        using dualpad::input::backend::PlannedBackend;

        constexpr std::uint32_t kDefaultPulseMinDownMs = 40;
        constexpr std::uint32_t kDefaultRepeatDelayMs = 350;
        constexpr std::uint32_t kDefaultRepeatIntervalMs = 75;

        PlannedActionPhase ToPlannedPhase(actions::ActionPhase phase)
        {
            switch (phase) {
            case actions::ActionPhase::Press:
                return PlannedActionPhase::Press;
            case actions::ActionPhase::Hold:
            case actions::ActionPhase::Repeat:
                return PlannedActionPhase::Hold;
            case actions::ActionPhase::Release:
                return PlannedActionPhase::Release;
            case actions::ActionPhase::Pulse:
                return PlannedActionPhase::Pulse;
            case actions::ActionPhase::Value:
                return PlannedActionPhase::Value;
            default:
                return PlannedActionPhase::None;
            }
        }

        NativeDigitalPolicyKind ResolveDigitalPolicy(ActionOutputContract contract)
        {
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

        void ApplyDigitalMetadata(PlannedAction& action, bool gateAware, std::uint32_t contextRevision)
        {
            action.digitalPolicy = ResolveDigitalPolicy(action.contract);
            action.gateAware = gateAware;
            action.minDownMs = action.digitalPolicy == NativeDigitalPolicyKind::PulseMinDown ?
                kDefaultPulseMinDownMs :
                0;
            action.repeatDelayMs = action.digitalPolicy == NativeDigitalPolicyKind::RepeatOwner ?
                kDefaultRepeatDelayMs :
                0;
            action.repeatIntervalMs = action.digitalPolicy == NativeDigitalPolicyKind::RepeatOwner ?
                kDefaultRepeatIntervalMs :
                0;
            action.contextEpoch = contextRevision;
        }

        PlannedAction BuildNativeAction(
            std::string_view actionId,
            dualpad::input::backend::NativeControlCode control,
            PlannedActionPhase phase,
            ActionOutputContract contract,
            bool gateAware,
            dualpad::input::InputContext legacyContext,
            std::uint32_t contextRevision)
        {
            PlannedAction action{};
            action.backend = PlannedBackend::NativeButtonCommit;
            action.kind = PlannedActionKind::NativeButton;
            action.phase = phase;
            action.context = legacyContext;
            action.actionId = std::string(actionId);
            action.contract = contract;
            action.outputCode = static_cast<std::uint32_t>(control);
            ApplyDigitalMetadata(action, gateAware, contextRevision);
            return action;
        }

        PlannedAction BuildHelperAction(
            std::string_view actionId,
            HelperOutputKind kind,
            actions::ActionPhase phase,
            ActionOutputContract contract,
            float heldSeconds,
            dualpad::input::InputContext legacyContext,
            std::uint32_t contextRevision)
        {
            auto helperActionId = actionId;
            if (kind == HelperOutputKind::ModEvent) {
                if (const auto* slot = dualpad::input::backend::FindModEventKeySlot(actionId)) {
                    helperActionId = slot->helperActionId;
                }
            }

            PlannedAction action{};
            action.backend = PlannedBackend::KeyboardHelper;
            action.kind = PlannedActionKind::KeyboardKey;
            action.phase = ToPlannedPhase(phase);
            action.context = legacyContext;
            action.actionId = std::string(helperActionId);
            action.contract = contract;
            action.heldSeconds = heldSeconds;
            action.contextEpoch = contextRevision;
            return action;
        }

        class RuntimePollOutputExecutor final : public IPollOutputExecutor
        {
        public:
            RuntimePollOutputExecutor(
                dualpad::input::InputContext legacyContext,
                std::uint32_t contextRevision,
                std::uint64_t nowUs) :
                _legacyContext(legacyContext),
                _contextRevision(contextRevision),
                _nowUs(nowUs)
            {
                dualpad::input::backend::NativeButtonCommitBackend::GetSingleton().BeginFrame(
                    _legacyContext,
                    _contextRevision,
                    _nowUs);
            }

            bool ClearNativeOutput() override
            {
                auto& native = dualpad::input::backend::NativeButtonCommitBackend::GetSingleton();
                native.Reset();
                native.BeginFrame(_legacyContext, _contextRevision, _nowUs);
                return true;
            }

            bool ClearHelperOutput() override
            {
                dualpad::input::backend::KeyboardHelperBackend::GetSingleton().Reset();
                return true;
            }

            bool ClearSustainedDigitalAggregator() override
            {
                return true;
            }

            bool ClearProjectionStickyOwners() override
            {
                return true;
            }

            bool ApplyGatePlan(const GatePlan& gatePlan) override
            {
                auto& native = dualpad::input::backend::NativeButtonCommitBackend::GetSingleton();
                native.SetGameplayDigitalGatePlan(gatePlan.transientDigitalGate != DigitalGateMode::Open);
                if (gatePlan.transientDigitalGate == DigitalGateMode::CancelAndSuppressNewTransient) {
                    native.ForceCancelGateAwareGameplayTransientActions();
                }
                return true;
            }

            bool ApplySustainedDigital(const NativeSustainedCommand& command) override
            {
                const auto phase = command.activeSourceMask == 0 ?
                    PlannedActionPhase::Release :
                    PlannedActionPhase::Hold;
                return dualpad::input::backend::NativeButtonCommitBackend::GetSingleton().ApplyPlannedAction(
                    BuildNativeAction(
                        command.actionId,
                        command.control,
                        phase,
                        command.contract,
                        false,
                        _legacyContext,
                        command.contextRevision));
            }

            bool ApplyTransientDigital(const NativeTransientCommand& command) override
            {
                return dualpad::input::backend::NativeButtonCommitBackend::GetSingleton().ApplyPlannedAction(
                    BuildNativeAction(
                        command.actionId,
                        command.control,
                        ToPlannedPhase(command.phase),
                        command.contract,
                        command.gateAware,
                        _legacyContext,
                        command.contextRevision));
            }

            bool ApplyHelperCommand(const HelperOutputCommand& command) override
            {
                auto& helper = dualpad::input::backend::KeyboardHelperBackend::GetSingleton();
                if (!helper.IsRouteActive()) {
                    return true;
                }

                const auto action = BuildHelperAction(
                    command.actionId,
                    command.kind,
                    command.phase,
                    command.contract,
                    command.heldSeconds,
                    _legacyContext,
                    command.contextRevision);
                if (!helper.CanHandleAction(action.actionId)) {
                    return false;
                }

                switch (action.phase) {
                case PlannedActionPhase::Pulse:
                    return helper.TriggerAction(action.actionId, action.contract, action.context);
                case PlannedActionPhase::Press:
                case PlannedActionPhase::Hold:
                    return helper.SubmitActionState(
                        action.actionId,
                        action.contract,
                        true,
                        action.heldSeconds,
                        action.context);
                case PlannedActionPhase::Release:
                    return helper.SubmitActionState(
                        action.actionId,
                        action.contract,
                        false,
                        action.heldSeconds,
                        action.context);
                case PlannedActionPhase::Value:
                case PlannedActionPhase::None:
                default:
                    return false;
                }
            }

            bool PublishAnalogState(const ProjectedAnalogState& analog) override
            {
                dualpad::input::AuthoritativePollState::GetSingleton().PublishAnalogState(
                    analog.moveX,
                    analog.moveY,
                    analog.lookX,
                    analog.lookY,
                    analog.leftTrigger,
                    analog.rightTrigger);
                return true;
            }

            bool CommitCleanRecoveryBaseline() override
            {
                return true;
            }

        private:
            dualpad::input::InputContext _legacyContext{ dualpad::input::InputContext::Gameplay };
            std::uint32_t _contextRevision{ 0 };
            std::uint64_t _nowUs{ 0 };
        };
    }

    DualPadRuntimeResult DualPadRuntime::ProcessGameplayFrame(const DualPadRuntimeInput& input)
    {
        if (HasRuntimeHealthReason(input.runtimeHealthReasons, RuntimeHealthReason::HookInstallFailed)) {
            return DualPadRuntimeResult{
                .projectionFrame = GameplayProjectionFrame{},
                .output = PollOutputApplyResult{},
                .gameplayPresentation = _presentationPublisher.GetPublished(),
                .runtimeHealthReasons = input.runtimeHealthReasons,
                .runtimeHealthDebugReason = input.runtimeHealthDebugReason
            };
        }

        RuntimePollOutputExecutor executor(
            input.legacyContext,
            input.kernel.facts.contextRevision,
            input.kernel.facts.monotonicUs);
        return ProcessGameplayFrameWithExecutor(input, executor);
    }

    DualPadRuntimeResult DualPadRuntime::ProcessAssembledFrame(const ingress::AssembledFactFrame& frame)
    {
        if (frame.kind == ingress::AssembledFrameKind::Transition) {
            return ProcessTransitionFrame(frame);
        }

        auto envelope = BindRuntimeEnvelope(frame);
        auto input = BuildStableRuntimeInput(envelope);
        auto result = ProcessGameplayFrame(input);
        PublishStablePresentationSurface(envelope, result);
        return result;
    }
}
