#include "pch.h"
#include "input/mapping/PadEventGenerator.h"

#include "input/mapping/EventDebugLogger.h"

namespace dualpad::input
{
    void PadEventGenerator::Reset()
    {
        _comboEvaluator.Reset();
        _tapHoldEvaluator.Reset();
        _touchpadMapper.Reset();
    }

    TouchpadMapper& PadEventGenerator::GetTouchpadMapper()
    {
        return _touchpadMapper;
    }

    const TouchpadMapper& PadEventGenerator::GetTouchpadMapper() const
    {
        return _touchpadMapper;
    }

    void PadEventGenerator::Generate(const PadState& previous, const PadState& current, PadEventBuffer& outEvents)
    {
        outEvents.Clear();

        GenerateButtonEvents(previous, current, outEvents);
        _axisEvaluator.Evaluate(previous, current, outEvents);
        _tapHoldEvaluator.Evaluate(previous, current, outEvents);
        _comboEvaluator.Evaluate(previous, current, outEvents);
        _touchpadMapper.ProcessTouch(current, outEvents);

        LogPadEvents(outEvents);
    }

    void PadEventGenerator::GenerateButtonEvents(
        const PadState& previous,
        const PadState& current,
        PadEventBuffer& outEvents) const
    {
        const auto previousMask = previous.buttons.digitalMask;
        const auto currentMask = current.buttons.digitalMask;
        const auto pressedMask = currentMask & ~previousMask;
        const auto releasedMask = previousMask & ~currentMask;

        for (int bitIndex = 0; bitIndex < 32; ++bitIndex) {
            const auto bit = (1u << bitIndex);
            if ((pressedMask & bit) != 0) {
                PadEvent event{};
                event.type = PadEventType::ButtonPress;
                event.triggerType = TriggerType::Button;
                event.code = bit;
                event.timestampUs = current.timestampUs;
                event.modifierMask = currentMask & ~bit;
                outEvents.Push(event);
            }

            if ((releasedMask & bit) != 0) {
                PadEvent event{};
                event.type = PadEventType::ButtonRelease;
                event.triggerType = TriggerType::Button;
                event.code = bit;
                event.timestampUs = current.timestampUs;
                event.modifierMask = previousMask & ~bit;
                outEvents.Push(event);
            }
        }
    }
}
