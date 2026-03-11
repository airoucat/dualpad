#include "pch.h"
#include "input/mapping/AxisEvaluator.h"

#include <cmath>

namespace dualpad::input
{
    namespace
    {
        void EmitAxisEvent(
            PadAxisId axis,
            float previousValue,
            float currentValue,
            std::uint64_t timestampUs,
            PadEventBuffer& outEvents)
        {
            PadEvent event{};
            event.type = PadEventType::AxisChange;
            event.triggerType = TriggerType::Axis;
            event.code = static_cast<std::uint32_t>(axis);
            event.timestampUs = timestampUs;
            event.axis = axis;
            event.previousValue = previousValue;
            event.value = currentValue;
            outEvents.Push(event);
        }
    }

    void AxisEvaluator::SetConfig(const AxisEvaluatorConfig& config)
    {
        _config = config;
    }

    void AxisEvaluator::Evaluate(const PadState& previous, const PadState& current, PadEventBuffer& outEvents) const
    {
        const auto emitStickIfChanged = [timestampUs = current.timestampUs, &outEvents](
                                            PadAxisId axis,
                                            float previousValue,
                                            float currentValue,
                                            float threshold) {
            if (std::fabs(currentValue - previousValue) >= threshold) {
                EmitAxisEvent(axis, previousValue, currentValue, timestampUs, outEvents);
            }
        };

        emitStickIfChanged(
            PadAxisId::LeftStickX,
            previous.leftStick.x,
            current.leftStick.x,
            _config.stickThreshold);
        emitStickIfChanged(
            PadAxisId::LeftStickY,
            previous.leftStick.y,
            current.leftStick.y,
            _config.stickThreshold);
        emitStickIfChanged(
            PadAxisId::RightStickX,
            previous.rightStick.x,
            current.rightStick.x,
            _config.stickThreshold);
        emitStickIfChanged(
            PadAxisId::RightStickY,
            previous.rightStick.y,
            current.rightStick.y,
            _config.stickThreshold);
        emitStickIfChanged(
            PadAxisId::LeftTrigger,
            previous.leftTrigger.normalized,
            current.leftTrigger.normalized,
            _config.triggerThreshold);
        emitStickIfChanged(
            PadAxisId::RightTrigger,
            previous.rightTrigger.normalized,
            current.rightTrigger.normalized,
            _config.triggerThreshold);
    }
}
