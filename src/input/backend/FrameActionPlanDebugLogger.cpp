#include "pch.h"
#include "input/backend/FrameActionPlanDebugLogger.h"

#include "input/RuntimeConfig.h"

#include <SKSE/SKSE.h>

namespace logger = SKSE::log;

namespace dualpad::input::backend
{
    namespace
    {
        constexpr std::string_view ToString(PlannedBackend backend)
        {
            switch (backend) {
            case PlannedBackend::NativeState:
                return "NativeState";
            case PlannedBackend::KeyboardNative:
                return "KeyboardNative";
            case PlannedBackend::Plugin:
                return "Plugin";
            case PlannedBackend::ModEvent:
                return "ModEvent";
            case PlannedBackend::CompatibilityFallback:
            default:
                return "CompatibilityFallback";
            }
        }

        constexpr std::string_view ToString(PlannedActionKind kind)
        {
            switch (kind) {
            case PlannedActionKind::NativeButton:
                return "NativeButton";
            case PlannedActionKind::KeyboardKey:
                return "KeyboardKey";
            case PlannedActionKind::NativeAxis1D:
                return "NativeAxis1D";
            case PlannedActionKind::NativeAxis2D:
                return "NativeAxis2D";
            case PlannedActionKind::PluginAction:
                return "PluginAction";
            case PlannedActionKind::ModEvent:
            default:
                return "ModEvent";
            }
        }

        constexpr std::string_view ToString(PlannedActionPhase phase)
        {
            switch (phase) {
            case PlannedActionPhase::Pulse:
                return "Pulse";
            case PlannedActionPhase::Press:
                return "Press";
            case PlannedActionPhase::Hold:
                return "Hold";
            case PlannedActionPhase::Release:
                return "Release";
            case PlannedActionPhase::Value:
                return "Value";
            case PlannedActionPhase::None:
            default:
                return "None";
            }
        }

        constexpr std::string_view ToString(ButtonCommitPolicy policy)
        {
            switch (policy) {
            case ButtonCommitPolicy::HoldOwner:
                return "HoldOwner";
            case ButtonCommitPolicy::DeferredPulseWhenAllowed:
                return "DeferredPulseWhenAllowed";
            case ButtonCommitPolicy::MinDownWindowPulse:
                return "MinDownWindowPulse";
            case ButtonCommitPolicy::None:
            default:
                return "None";
            }
        }
    }

    void LogFrameActionPlan(const FrameActionPlan& plan)
    {
        if (!input::RuntimeConfig::GetSingleton().LogActionPlan()) {
            return;
        }

        logger::info(
            "[DualPad][ActionPlan] size={} overflowed={}",
            plan.Size(),
            plan.Overflowed());

        for (std::size_t i = 0; i < plan.Size(); ++i) {
            const auto& action = plan[i];
            logger::info(
                "[DualPad][ActionPlan] idx={} backend={} kind={} phase={} context={} action='{}' source=0x{:08X} output=0x{:08X} modifiers=0x{:08X} valueX={:.3f} valueY={:.3f} held={:.3f} policy={} minDownUs={} minVisiblePolls={} maxDeferredPolls={} gateClass={}",
                i,
                ToString(action.backend),
                ToString(action.kind),
                ToString(action.phase),
                input::ToString(action.context),
                action.actionId,
                action.sourceCode,
                action.outputCode,
                action.modifierMask,
                action.valueX,
                action.valueY,
                action.heldSeconds,
                ToString(action.lifecycle.policy),
                action.lifecycle.minDownUs,
                action.lifecycle.minVisiblePolls,
                action.lifecycle.maxDeferredPolls,
                backend::ToString(action.lifecycle.gateClass));
        }
    }

    void LogVirtualGamepadState(const VirtualGamepadState& state)
    {
        if (!input::RuntimeConfig::GetSingleton().LogActionPlan()) {
            return;
        }

        logger::info(
            "[DualPad][ActionPlan] virtualPad poll={} packet={} rawButtons=0x{:04X} prevDown=0x{:08X} down=0x{:08X} pressed=0x{:08X} released=0x{:08X} move=({:.3f},{:.3f}) look=({:.3f},{:.3f}) triggers=({:.3f},{:.3f})",
            state.pollIndex,
            state.packetNumber,
            state.rawButtons,
            state.previousDownMask,
            state.downMask,
            state.pressedMask,
            state.releasedMask,
            state.moveX,
            state.moveY,
            state.lookX,
            state.lookY,
            state.leftTrigger,
            state.rightTrigger);
    }
}
