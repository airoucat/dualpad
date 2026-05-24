#include "pch.h"

#include "input_v2/gameplay/GameplayProjectionFrame.h"

#include "input/backend/ActionBackendPolicy.h"
#include "input/backend/ModEventKeyPool.h"
#include "input/backend/NativeActionDescriptor.h"

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

        presentation::PresentationOwner ToPresentationOwner(ChannelOwner owner)
        {
            return owner == ChannelOwner::Gamepad ?
                presentation::PresentationOwner::Gamepad :
                presentation::PresentationOwner::KeyboardMouse;
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
        if (frame.recoveryPlan.mode == RecoveryMode::SoftResyncOutputs) {
            frame.reasons.recovery = GameplayReasonCode::SoftResync;
        } else if (frame.recoveryPlan.mode == RecoveryMode::HardResetOutputs) {
            frame.reasons.recovery = GameplayReasonCode::HardReset;
        }
        frame.helperPlan.enqueueBridgeResetBeforeApply = frame.recoveryPlan.mode != RecoveryMode::None;

        bool hasTransientGamepadDigital = false;
        for (const auto& change : resolved.changes) {
            const auto decision = dualpad::input::backend::ActionBackendPolicy::Decide(change.actionId);
            if (decision.backend == PlannedBackend::NativeButtonCommit && IsTransientContract(decision.contract)) {
                hasTransientGamepadDigital = true;
            }
        }

        const auto lookMagnitude = AxisMagnitudeForTarget(resolved, NativeAxisTarget::LookStick);
        const auto moveMagnitude = AxisMagnitudeForTarget(resolved, NativeAxisTarget::MoveStick);
        const auto leftTriggerMagnitude = AxisMagnitudeForTarget(resolved, NativeAxisTarget::LeftTrigger);
        const auto rightTriggerMagnitude = AxisMagnitudeForTarget(resolved, NativeAxisTarget::RightTrigger);
        const auto triggerMagnitude = std::max(leftTriggerMagnitude, rightTriggerMagnitude);

        if (!policy.gameplayContext) {
            frame.lookOwner = ChannelOwner::KeyboardMouse;
            frame.moveOwner = ChannelOwner::KeyboardMouse;
            frame.combatOwner = ChannelOwner::KeyboardMouse;
            frame.digitalOwner = ChannelOwner::KeyboardMouse;
            frame.reasons.look = GameplayReasonCode::NonGameplayContext;
            frame.reasons.move = GameplayReasonCode::NonGameplayContext;
            frame.reasons.combat = GameplayReasonCode::NonGameplayContext;
            frame.reasons.digital = GameplayReasonCode::NonGameplayContext;
        } else {
            frame.lookOwner = previous.lookOwner;
            frame.moveOwner = previous.moveOwner;
            frame.combatOwner = previous.combatOwner;
            frame.digitalOwner = previous.digitalOwner;

            if (policy.mouseLookActive) {
                frame.lookOwner = ChannelOwner::KeyboardMouse;
                frame.reasons.look = GameplayReasonCode::MouseLookActive;
            } else if (lookMagnitude >= policy.lookEnterThreshold ||
                       (previous.lookOwner == ChannelOwner::Gamepad && lookMagnitude >= policy.lookSustainThreshold)) {
                frame.lookOwner = ChannelOwner::Gamepad;
                frame.reasons.look = GameplayReasonCode::MeaningfulRightStick;
            } else {
                frame.reasons.look = GameplayReasonCode::CarryPreviousOwner;
            }

            if (policy.keyboardMoveActive) {
                frame.moveOwner = ChannelOwner::KeyboardMouse;
                frame.reasons.move = GameplayReasonCode::KeyboardMoveActive;
            } else if (moveMagnitude >= policy.moveEnterThreshold ||
                       (previous.moveOwner == ChannelOwner::Gamepad && moveMagnitude >= policy.moveSustainThreshold)) {
                frame.moveOwner = ChannelOwner::Gamepad;
                frame.reasons.move = GameplayReasonCode::MeaningfulLeftStick;
            } else {
                frame.reasons.move = GameplayReasonCode::CarryPreviousOwner;
            }

            if (policy.keyboardMouseCombatActive) {
                frame.combatOwner = ChannelOwner::KeyboardMouse;
                frame.reasons.combat = GameplayReasonCode::KeyboardMouseCombatActive;
            } else if (triggerMagnitude >= policy.triggerEnterThreshold ||
                       (previous.combatOwner == ChannelOwner::Gamepad && triggerMagnitude >= policy.triggerSustainThreshold)) {
                frame.combatOwner = ChannelOwner::Gamepad;
                frame.reasons.combat = GameplayReasonCode::MeaningfulTrigger;
            } else {
                frame.reasons.combat = GameplayReasonCode::CarryPreviousOwner;
            }

            if (policy.keyboardMouseDigitalActive) {
                frame.digitalOwner = ChannelOwner::KeyboardMouse;
                frame.reasons.digital = GameplayReasonCode::KeyboardMouseTransientDigitalActive;
            } else if (hasTransientGamepadDigital) {
                frame.digitalOwner = ChannelOwner::Gamepad;
                frame.reasons.digital = GameplayReasonCode::GamepadTransientDigitalActive;
            } else {
                frame.reasons.digital = GameplayReasonCode::CarryPreviousOwner;
            }
        }

        frame.gatePlan.lookGate = frame.lookOwner == ChannelOwner::KeyboardMouse ? AnalogGateMode::ZeroedByKeyboardMouse : AnalogGateMode::Open;
        frame.gatePlan.moveGate = frame.moveOwner == ChannelOwner::KeyboardMouse ? AnalogGateMode::ZeroedByKeyboardMouse : AnalogGateMode::Open;
        frame.gatePlan.leftTriggerGate = frame.combatOwner == ChannelOwner::KeyboardMouse ? AnalogGateMode::ZeroedByKeyboardMouse : AnalogGateMode::Open;
        frame.gatePlan.rightTriggerGate = frame.combatOwner == ChannelOwner::KeyboardMouse ? AnalogGateMode::ZeroedByKeyboardMouse : AnalogGateMode::Open;
        if (frame.digitalOwner == ChannelOwner::KeyboardMouse) {
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
        for (const auto& change : resolved.changes) {
            const auto decision = dualpad::input::backend::ActionBackendPolicy::Decide(change.actionId);
            if (decision.backend == PlannedBackend::NativeButtonCommit) {
                if (IsTransientContract(decision.contract)) {
                    if (frame.gatePlan.transientDigitalGate == DigitalGateMode::Open) {
                        overflow = !TryAppend(
                            frame.gamepadPlan.transientDigital,
                            NativeTransientCommand{
                                .actionId = change.actionId,
                                .control = decision.nativeCode,
                                .phase = change.phase,
                                .contract = decision.contract,
                                .gateAware = true,
                                .contextRevision = frame.contextRevision
                            }) || overflow;
                    }
                } else if (IsSustainedContract(decision.contract)) {
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

        const bool analogPresentationGamepad =
            frame.lookOwner == ChannelOwner::Gamepad ||
            frame.moveOwner == ChannelOwner::Gamepad ||
            frame.combatOwner == ChannelOwner::Gamepad;
        frame.presentationPlan.engineOwner =
            policy.keyboardMouseDigitalActive || policy.keyboardMouseCombatActive || policy.keyboardMoveActive || policy.mouseLookActive ?
            presentation::PresentationOwner::KeyboardMouse :
            (analogPresentationGamepad ?
                    presentation::PresentationOwner::Gamepad :
                    presentation::PresentationOwner::KeyboardMouse);
        frame.presentationPlan.menuEntryOwner = frame.presentationPlan.engineOwner;
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
