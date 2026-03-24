#include "pch.h"
#include "input/backend/FrameActionPlanDebugLogger.h"

#include "input/RuntimeConfig.h"
#include "input/XInputButtonSerialization.h"

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

    void LogAuthoritativePollFrame(const AuthoritativePollFrame& frame)
    {
        if (!input::RuntimeConfig::GetSingleton().LogActionPlan()) {
            return;
        }

        const auto xinputButtons = input::ToXInputButtons(frame.downMask);
        const auto xinputPressedButtons = input::ToXInputButtons(frame.pressedMask);
        const auto xinputReleasedButtons = input::ToXInputButtons(frame.releasedMask);
        const auto xinputPulseButtons = input::ToXInputButtons(frame.pulseMask);

        logger::info(
            "[DualPad][AuthoritativePoll] poll={} ctx={} epoch={} srcTs={} down=0x{:08X} pressed=0x{:08X} released=0x{:08X} pulse=0x{:08X} xinputButtons=0x{:04X} xinputPressed=0x{:04X} xinputReleased=0x{:04X} xinputPulse=0x{:04X} unmanagedDown=0x{:08X} unmanagedPressed=0x{:08X} unmanagedReleased=0x{:08X} unmanagedPulse=0x{:08X} committedDown=0x{:08X} committedPressed=0x{:08X} committedReleased=0x{:08X} managed=0x{:08X} hasDigital={} hasAnalog={} overflowed={} coalesced={} move=({:.3f},{:.3f}) look=({:.3f},{:.3f}) triggers=({:.3f},{:.3f})",
            frame.pollSequence,
            input::ToString(frame.context),
            frame.contextEpoch,
            frame.sourceTimestampUs,
            frame.downMask,
            frame.pressedMask,
            frame.releasedMask,
            frame.pulseMask,
            xinputButtons,
            xinputPressedButtons,
            xinputReleasedButtons,
            xinputPulseButtons,
            frame.unmanagedDownMask,
            frame.unmanagedPressedMask,
            frame.unmanagedReleasedMask,
            frame.unmanagedPulseMask,
            frame.committedDownMask,
            frame.committedPressedMask,
            frame.committedReleasedMask,
            frame.managedMask,
            frame.hasDigital,
            frame.hasAnalog,
            frame.overflowed,
            frame.coalesced,
            frame.moveX,
            frame.moveY,
            frame.lookX,
            frame.lookY,
            frame.leftTrigger,
            frame.rightTrigger);
    }
}
