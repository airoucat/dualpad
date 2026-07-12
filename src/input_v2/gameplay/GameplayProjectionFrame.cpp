#include "pch.h"

#include "input_v2/gameplay/GameplayProjectionFrame.h"

#include "input/Action.h"
#include "input/backend/ActionBackendPolicy.h"
#include "input/backend/ModEventKeyPool.h"
#include "input/backend/NativeActionDescriptor.h"
#include "input/backend/NativeDigitalPolicyResolver.h"

#include <algorithm>
#include <cmath>
#include <charconv>
#include <string_view>

namespace dualpad::input_v2::gameplay
{
    namespace
    {
        using dualpad::input::backend::ActionOutputContract;
        using dualpad::input::backend::NativeAxisTarget;
        using dualpad::input::backend::NativeControlCode;
        using dualpad::input::backend::PlannedBackend;

        bool IsTransientContract(ActionOutputContract contract)
        {
            return contract == ActionOutputContract::Pulse || contract == ActionOutputContract::Toggle;
        }

        bool IsSustainedContract(ActionOutputContract contract)
        {
            return contract == ActionOutputContract::Hold || contract == ActionOutputContract::Repeat;
        }

        bool IsPresentationHandoffEdge(actions::ActionPhase phase)
        {
            return phase == actions::ActionPhase::Press ||
                phase == actions::ActionPhase::Pulse;
        }

        bool TryParseTrailingNumber(std::string_view value, std::uint16_t& out)
        {
            const auto dot = value.find_last_of('.');
            const auto token = dot == std::string_view::npos ? value : value.substr(dot + 1);
            std::uint16_t parsed = 0;
            const auto* begin = token.data();
            const auto* end = token.data() + token.size();
            const auto result = std::from_chars(begin, end, parsed);
            if (result.ec != std::errc{} || result.ptr != end) {
                return false;
            }
            out = parsed;
            return true;
        }

        std::uint16_t ResolveHelperCode(std::string_view actionId)
        {
            std::uint16_t numeric = 0;
            if (TryParseTrailingNumber(actionId, numeric)) {
                return numeric;
            }
            if (const auto* slot = dualpad::input::backend::FindModEventKeySlot(actionId)) {
                return slot->directInputScancode;
            }
            return 0;
        }

        template <class T, std::size_t N>
        bool TryAppend(FixedCommandList<T, N>& list, const T& item)
        {
            if (list.count >= list.items.size()) {
                return false;
            }
            list.items[list.count++] = item;
            return true;
        }

        void ForceOverflowHardReset(GameplayProjectionFrame& frame)
        {
            frame.gamepadPlan.transientDigital.count = 0;
            frame.gamepadPlan.sustainedDigital.count = 0;
            frame.helperPlan.commands.count = 0;
            frame.recoveryPlan.mode = RecoveryMode::HardResetOutputs;
            frame.recoveryPlan.resetNativeCommitBackend = true;
            frame.recoveryPlan.resetKeyboardHelperBackend = true;
            frame.recoveryPlan.resetSustainedDigitalAggregator = true;
            frame.recoveryPlan.clearProjectionStickyOwners = true;
            frame.helperPlan.enqueueBridgeResetBeforeApply = true;
            frame.reasons.recovery = GameplayReasonCode::HardReset;
        }

        float Magnitude(float x, float y)
        {
            return std::sqrt((x * x) + (y * y));
        }

        const actions::ActionValueSnapshot* FindValue(
            const actions::ResolvedActionFrame& resolved,
            std::string_view actionId)
        {
            const auto found = std::find_if(
                resolved.values.begin(),
                resolved.values.end(),
                [actionId](const actions::ActionValueSnapshot& value) {
                    return value.actionId == actionId;
                });
            return found == resolved.values.end() ? nullptr : &*found;
        }

        float AxisMagnitudeForTarget(const actions::ResolvedActionFrame& resolved, NativeAxisTarget target)
        {
            for (const auto& value : resolved.values) {
                const auto* descriptor = dualpad::input::backend::FindNativeActionDescriptor(value.actionId);
                if (!descriptor || descriptor->axisTarget != target) {
                    continue;
                }
                if (value.kind == actions::ActionValueKind::Axis2D) {
                    return Magnitude(value.x, value.y);
                }
                return std::abs(value.scalar);
            }
            return 0.0f;
        }

        void ApplyAnalogValue(GameplayProjectionFrame& frame, const actions::ActionValueSnapshot& value)
        {
            const auto* descriptor = dualpad::input::backend::FindNativeActionDescriptor(value.actionId);
            if (!descriptor || descriptor->backend != PlannedBackend::NativeState) {
                return;
            }

            switch (descriptor->axisTarget) {
            case NativeAxisTarget::LookStick:
                frame.gamepadPlan.analog.lookX = value.x;
                frame.gamepadPlan.analog.lookY = value.y;
                break;
            case NativeAxisTarget::MoveStick:
                frame.gamepadPlan.analog.moveX = value.x;
                frame.gamepadPlan.analog.moveY = value.y;
                break;
            case NativeAxisTarget::LeftTrigger:
                frame.gamepadPlan.analog.leftTrigger = value.scalar;
                break;
            case NativeAxisTarget::RightTrigger:
                frame.gamepadPlan.analog.rightTrigger = value.scalar;
                break;
            case NativeAxisTarget::None:
            default:
                break;
            }
        }

        ChannelArbitrationState PreviousChannelState(
            const ChannelArbitrationState& state,
            ChannelOwner compatibilityOwner)
        {
            if (state.owner != ChannelOwner::None || compatibilityOwner == ChannelOwner::None) {
                return state;
            }
            auto seeded = state;
            seeded.owner = compatibilityOwner;
            return seeded;
        }
    }

    GameplayProjectionFrame ResolveGameplayProjection(
        const actions::KernelFrame& kernel,
        const actions::ResolvedActionFrame& resolved,
        const GameplayPolicy& policy,
        const GameplayProjectionFrame& previous,
        const GameplayRecoveryInput& recoveryInput)
    {
        GameplayProjectionFrame frame{};
        frame.context = policy.gameplayContext ? LegacyInputContextCompat::Gameplay : LegacyInputContextCompat::Menu;
        frame.contextRevision = kernel.facts.contextRevision;
        frame.recoveryPlan = BuildRecoveryPlan(recoveryInput);
        if (!frame.recoveryPlan.resetSustainedDigitalAggregator) {
            frame.sprintDecision.next = previous.sprintDecision.next;
        }
        if (frame.recoveryPlan.mode == RecoveryMode::SoftResyncOutputs) {
            frame.reasons.recovery = GameplayReasonCode::SoftResync;
        } else if (frame.recoveryPlan.mode == RecoveryMode::HardResetOutputs) {
            frame.reasons.recovery = GameplayReasonCode::HardReset;
        }
        frame.helperPlan.enqueueBridgeResetBeforeApply =
            frame.recoveryPlan.resetKeyboardHelperBackend;

        bool hasTransientGamepadDigital = false;
        bool hasGameplayMenuEntryDigital = false;
        for (const auto& change : resolved.changes) {
            const auto decision = dualpad::input::backend::ActionBackendPolicy::Decide(change.actionId);
            if (decision.backend == PlannedBackend::NativeButtonCommit && IsTransientContract(decision.contract)) {
                hasTransientGamepadDigital = true;
                if (policy.gameplayContext &&
                    IsPresentationHandoffEdge(change.phase) &&
                    dualpad::input::backend::RequiresGameplayMenuEntryPresentationHandoff(change.actionId)) {
                    hasGameplayMenuEntryDigital = true;
                }
            }
        }

        const auto lookMagnitude = AxisMagnitudeForTarget(resolved, NativeAxisTarget::LookStick);
        const auto moveMagnitude = AxisMagnitudeForTarget(resolved, NativeAxisTarget::MoveStick);
        const auto leftTriggerMagnitude = AxisMagnitudeForTarget(resolved, NativeAxisTarget::LeftTrigger);
        const auto rightTriggerMagnitude = AxisMagnitudeForTarget(resolved, NativeAxisTarget::RightTrigger);
        const auto triggerMagnitude = std::max(leftTriggerMagnitude, rightTriggerMagnitude);

        const auto nowUs = policy.outputTickUs != 0 ? policy.outputTickUs : kernel.facts.monotonicUs;
        const auto look = ResolveChannelArbitration(ChannelArbitrationInput{
            .previous = PreviousChannelState(previous.nextArbitration.look, previous.lookOwner),
            .gameplayContext = policy.gameplayContext,
            .keyboardMouseActive = policy.mouseLookActive,
            .keyboardMouseActivation = policy.mouseLookActivatedThisFrame,
            .gamepadMagnitude = lookMagnitude,
            .gamepadEnterThreshold = policy.lookEnterThreshold,
            .gamepadSustainThreshold = policy.lookSustainThreshold,
            .nowUs = nowUs,
            .lastKeyboardMouseActivityUs = policy.lastPhysicalMouseMoveOwnerUs,
            .keyboardMouseQuietWindowUs = policy.mouseLookQuietWindowUs,
            .keyboardMouseReason = GameplayReasonCode::MouseLookActive,
            .gamepadReason = GameplayReasonCode::MeaningfulRightStick,
            .resetMode = policy.arbitrationResetMode
        });
        const auto move = ResolveChannelArbitration(ChannelArbitrationInput{
            .previous = PreviousChannelState(previous.nextArbitration.move, previous.moveOwner),
            .gameplayContext = policy.gameplayContext,
            .keyboardMouseActive = policy.keyboardMoveActive,
            .keyboardMouseActivation = policy.keyboardMoveActivatedThisFrame,
            .gamepadMagnitude = moveMagnitude,
            .gamepadEnterThreshold = policy.moveEnterThreshold,
            .gamepadSustainThreshold = policy.moveSustainThreshold,
            .nowUs = nowUs,
            .lastKeyboardMouseActivityUs = policy.keyboardMoveActive ? nowUs : 0,
            .keyboardMouseReason = GameplayReasonCode::KeyboardMoveActive,
            .gamepadReason = GameplayReasonCode::MeaningfulLeftStick,
            .resetMode = policy.arbitrationResetMode
        });
        const auto combat = ResolveChannelArbitration(ChannelArbitrationInput{
            .previous = PreviousChannelState(previous.nextArbitration.combat, previous.combatOwner),
            .gameplayContext = policy.gameplayContext,
            .keyboardMouseActive = policy.keyboardMouseCombatActive,
            .keyboardMouseActivation = policy.keyboardMouseCombatActivatedThisFrame,
            .gamepadMagnitude = triggerMagnitude,
            .gamepadEnterThreshold = policy.triggerEnterThreshold,
            .gamepadSustainThreshold = policy.triggerSustainThreshold,
            .nowUs = nowUs,
            .lastKeyboardMouseActivityUs = policy.keyboardMouseCombatActive ? nowUs : 0,
            .keyboardMouseReason = GameplayReasonCode::KeyboardMouseCombatActive,
            .gamepadReason = GameplayReasonCode::MeaningfulTrigger,
            .resetMode = policy.arbitrationResetMode
        });
        const auto digital = ResolveChannelArbitration(ChannelArbitrationInput{
            .previous = PreviousChannelState(previous.nextArbitration.digital, previous.digitalOwner),
            .gameplayContext = policy.gameplayContext,
            .keyboardMouseActive = policy.keyboardMouseDigitalActive,
            .keyboardMouseActivation = policy.keyboardMouseDigitalActivatedThisFrame,
            .gamepadMagnitude = hasTransientGamepadDigital ? 1.0f : 0.0f,
            .gamepadEnterThreshold = 0.5f,
            .gamepadSustainThreshold = 0.5f,
            .nowUs = nowUs,
            .lastKeyboardMouseActivityUs = policy.keyboardMouseDigitalActive ? nowUs : 0,
            .keyboardMouseReason = GameplayReasonCode::KeyboardMouseTransientDigitalActive,
            .gamepadReason = GameplayReasonCode::GamepadTransientDigitalActive,
            .resetMode = policy.arbitrationResetMode
        });

        const auto recoveryReason = frame.reasons.recovery;
        frame.lookOwner = look.owner;
        frame.moveOwner = move.owner;
        frame.combatOwner = combat.owner;
        frame.digitalOwner = digital.owner;
        frame.nextArbitration = ChannelArbitrationStateSet{
            .look = look.next,
            .move = move.next,
            .combat = combat.next,
            .digital = digital.next
        };
        frame.reasons.look = look.reason;
        frame.reasons.move = move.reason;
        frame.reasons.combat = combat.reason;
        frame.reasons.digital = digital.reason;
        frame.reasons.recovery = recoveryReason;

        if (policy.gameplayContext) {
            frame.gatePlan.lookGate = look.gateGamepad ? AnalogGateMode::ZeroedByKeyboardMouse : AnalogGateMode::Open;
            frame.gatePlan.moveGate = move.gateGamepad ? AnalogGateMode::ZeroedByKeyboardMouse : AnalogGateMode::Open;
            frame.gatePlan.leftTriggerGate = combat.gateGamepad ? AnalogGateMode::ZeroedByKeyboardMouse : AnalogGateMode::Open;
            frame.gatePlan.rightTriggerGate = combat.gateGamepad ? AnalogGateMode::ZeroedByKeyboardMouse : AnalogGateMode::Open;
        }
        if (policy.gameplayContext && digital.gateGamepad) {
            frame.gatePlan.transientDigitalGate = previous.digitalOwner == ChannelOwner::Gamepad ?
                DigitalGateMode::CancelAndSuppressNewTransient :
                DigitalGateMode::SuppressNewTransient;
        }

        for (const auto& value : resolved.values) {
            ApplyAnalogValue(frame, value);
        }
        if (frame.gatePlan.lookGate == AnalogGateMode::ZeroedByKeyboardMouse) {
            frame.gamepadPlan.analog.lookX = 0.0f;
            frame.gamepadPlan.analog.lookY = 0.0f;
        }
        if (frame.gatePlan.moveGate == AnalogGateMode::ZeroedByKeyboardMouse) {
            frame.gamepadPlan.analog.moveX = 0.0f;
            frame.gamepadPlan.analog.moveY = 0.0f;
        }
        if (frame.gatePlan.leftTriggerGate == AnalogGateMode::ZeroedByKeyboardMouse) {
            frame.gamepadPlan.analog.leftTrigger = 0.0f;
        }
        if (frame.gatePlan.rightTriggerGate == AnalogGateMode::ZeroedByKeyboardMouse) {
            frame.gamepadPlan.analog.rightTrigger = 0.0f;
        }

        bool overflow = false;
        auto sprintMask = frame.sprintDecision.next.activeSourceMask;
        const auto gamepadSprintMask = SustainedContributorMask(SustainedContributorBit::Gamepad);
        const auto keyboardSprintMask = SustainedContributorMask(SustainedContributorBit::KeyboardPhysical);
        const auto mouseSprintMask = SustainedContributorMask(SustainedContributorBit::MousePhysical);
        if (policy.clearGamepadSustainedContributor) {
            sprintMask = static_cast<std::uint8_t>(sprintMask & ~gamepadSprintMask);
        }
        sprintMask = policy.keyboardPhysicalSustainedActive ?
            static_cast<std::uint8_t>(sprintMask | keyboardSprintMask) :
            static_cast<std::uint8_t>(sprintMask & ~keyboardSprintMask);
        sprintMask = policy.mousePhysicalSustainedActive ?
            static_cast<std::uint8_t>(sprintMask | mouseSprintMask) :
            static_cast<std::uint8_t>(sprintMask & ~mouseSprintMask);
        bool sprintChanged = false;
        std::uint64_t gamepadSprintOrdinal = policy.gamepadSustainedEventOrdinal;
        for (const auto& change : resolved.changes) {
            const auto decision = dualpad::input::backend::ActionBackendPolicy::Decide(change.actionId);
            const auto* descriptor = dualpad::input::backend::FindNativeActionDescriptor(change.actionId);
            if (decision.backend == PlannedBackend::NativeButtonCommit) {
                if (IsTransientContract(decision.contract)) {
                    if (frame.gatePlan.transientDigitalGate == DigitalGateMode::Open) {
                        const auto digitalPolicy = dualpad::input::backend::ResolveNativeDigitalPolicy(
                            decision.backend,
                            decision.kind,
                            decision.contract,
                            decision.lifecyclePolicy);
                        overflow = !TryAppend(
                            frame.gamepadPlan.transientDigital,
                            NativeTransientCommand{
                                .actionId = change.actionId,
                                .control = decision.nativeCode,
                                .phase = change.phase,
                                .contract = decision.contract,
                                .lifecyclePolicy = decision.lifecyclePolicy,
                                .gateAware = dualpad::input::backend::IsNativeDigitalGateAwareAction(
                                    change.actionId,
                                    digitalPolicy),
                                .presentationHandoff = descriptor ?
                                    descriptor->presentationHandoff :
                                    dualpad::input::backend::NativePresentationHandoff::None,
                                .contextRevision = frame.contextRevision
                            }) || overflow;
                    }
                } else if (IsSustainedContract(decision.contract)) {
                    if (change.actionId == dualpad::input::actions::Sprint) {
                        sprintChanged = true;
                        gamepadSprintOrdinal = gamepadSprintOrdinal != 0 ?
                            gamepadSprintOrdinal : change.timestampUs;
                        sprintMask = change.phase == actions::ActionPhase::Release ?
                            static_cast<std::uint8_t>(sprintMask & ~gamepadSprintMask) :
                            static_cast<std::uint8_t>(sprintMask | gamepadSprintMask);
                        continue;
                    }
                    std::uint8_t mask = 0;
                    if (change.phase != actions::ActionPhase::Release) {
                        mask = static_cast<std::uint8_t>(SustainedSourceBit::GamepadResolved);
                        if (policy.keyboardPhysicalSustainedActive) {
                            mask |= static_cast<std::uint8_t>(SustainedSourceBit::KeyboardPhysical);
                        }
                        if (policy.mousePhysicalSustainedActive) {
                            mask |= static_cast<std::uint8_t>(SustainedSourceBit::MousePhysical);
                        }
                    }
                    overflow = !TryAppend(
                        frame.gamepadPlan.sustainedDigital,
                        NativeSustainedCommand{
                            .actionId = change.actionId,
                            .control = decision.nativeCode,
                            .activeSourceMask = mask,
                            .contract = decision.contract,
                            .lifecyclePolicy = decision.lifecyclePolicy,
                            .contextRevision = frame.contextRevision
                        }) || overflow;
                }
            } else if (decision.backend == PlannedBackend::KeyboardHelper || decision.backend == PlannedBackend::ModEvent) {
                overflow = !TryAppend(
                    frame.helperPlan.commands,
                    HelperOutputCommand{
                        .actionId = change.actionId,
                        .kind = decision.backend == PlannedBackend::ModEvent ? HelperOutputKind::ModEvent : HelperOutputKind::KeyboardKey,
                        .helperCode = ResolveHelperCode(change.actionId),
                        .phase = change.phase,
                        .contract = decision.contract,
                        .contextRevision = frame.contextRevision
                    }) || overflow;
            }
        }

        if (sprintChanged || sprintMask != 0 ||
            previous.sprintDecision.next.activeSourceMask != 0) {
            frame.sprintDecision = ResolveSustainedContributor(SustainedContributorInput{
                .previous = frame.sprintDecision.next,
                .activeSourceMask = sprintMask,
                .gamepadEventOrdinal = gamepadSprintOrdinal,
                .keyboardEventOrdinal = policy.keyboardSustainedEventOrdinal,
                .mouseEventOrdinal = policy.mouseSustainedEventOrdinal,
                .inputStateEpoch = previous.sprintDecision.next.inputStateEpoch,
                .contextRevision = frame.contextRevision,
                .runtimeGeneration = kernel.kernelRevision
            });
            overflow = !TryAppend(
                frame.gamepadPlan.sustainedDigital,
                NativeSustainedCommand{
                    .actionId = std::string(dualpad::input::actions::Sprint),
                    .control = NativeControlCode::Sprint,
                    .activeSourceMask = frame.sprintDecision.next.activeSourceMask,
                    .virtualBridgeDesired = frame.sprintDecision.virtualBridgeDesired,
                    .joiningPressSuppressionMask = frame.sprintDecision.joiningPressSuppressionMask,
                    .nonFinalReleaseSuppressionMask = frame.sprintDecision.nonFinalReleaseSuppressionMask,
                    .releaseToken = frame.sprintDecision.releaseToken,
                    .contract = ActionOutputContract::Hold,
                    .lifecyclePolicy = dualpad::input::backend::ActionLifecyclePolicy::HoldOwner,
                    .contextRevision = frame.contextRevision
                }) || overflow;
        }

        const bool keyboardMousePrimary =
            policy.keyboardMouseDigitalActive ||
            policy.keyboardMouseCombatActive ||
            policy.keyboardMoveActive ||
            policy.mouseLookActive;
        const bool gamepadPrimary =
            frame.lookOwner == ChannelOwner::Gamepad ||
            frame.moveOwner == ChannelOwner::Gamepad ||
            frame.combatOwner == ChannelOwner::Gamepad ||
            frame.digitalOwner == ChannelOwner::Gamepad ||
            hasGameplayMenuEntryDigital;
        frame.presentationPlan.engineOwner = keyboardMousePrimary ?
            presentation::PresentationOwner::KeyboardMouse :
            (gamepadPrimary ? presentation::PresentationOwner::Gamepad : presentation::PresentationOwner::KeyboardMouse);
        frame.presentationPlan.menuEntryOwner =
            hasGameplayMenuEntryDigital && !keyboardMousePrimary ?
            presentation::PresentationOwner::Gamepad :
            frame.presentationPlan.engineOwner;
        frame.presentationPlan.preOutputPresentationHandoff =
            hasGameplayMenuEntryDigital &&
            frame.presentationPlan.engineOwner == presentation::PresentationOwner::Gamepad;
        if (frame.recoveryPlan.mode == RecoveryMode::HardResetOutputs) {
            frame.presentationPlan.reason = presentation::GameplayPresentationReasonCode::RecoveryRepublish;
        } else {
            frame.presentationPlan.reason = presentation::GameplayPresentationReasonCode::CarryDigitalOwner;
        }

        if (overflow) {
            ForceOverflowHardReset(frame);
        }

        return frame;
    }
}
