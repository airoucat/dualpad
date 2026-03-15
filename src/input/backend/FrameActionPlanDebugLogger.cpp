#include "pch.h"
#include "input/backend/FrameActionPlanDebugLogger.h"

#include "input/RuntimeConfig.h"

#include <SKSE/SKSE.h>

namespace logger = SKSE::log;

namespace dualpad::input::backend
{
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
                "[DualPad][ActionPlan] idx={} backend={} kind={} contract={} policy={} digitalPolicy={} phase={} context={} epoch={} gateAware={} action='{}' source=0x{:08X} output=0x{:08X} modifiers=0x{:08X} valueX={:.3f} valueY={:.3f} held={:.3f} minDownMs={} repeatDelayMs={} repeatIntervalMs={}",
                i,
                ToString(action.backend),
                ToString(action.kind),
                ToString(action.contract),
                ToString(action.lifecyclePolicy),
                ToString(action.digitalPolicy),
                ToString(action.phase),
                input::ToString(action.context),
                action.contextEpoch,
                action.gateAware,
                action.actionId,
                action.sourceCode,
                action.outputCode,
                action.modifierMask,
                action.valueX,
                action.valueY,
                action.heldSeconds,
                action.minDownMs,
                action.repeatDelayMs,
                action.repeatIntervalMs);
        }
    }

    void LogVirtualGamepadState(const VirtualGamepadState& state)
    {
        if (!input::RuntimeConfig::GetSingleton().LogActionPlan()) {
            return;
        }

        logger::info(
            "[DualPad][ActionPlan] virtualPad down=0x{:08X} pressed=0x{:08X} released=0x{:08X} move=({:.3f},{:.3f}) look=({:.3f},{:.3f}) triggers=({:.3f},{:.3f})",
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
