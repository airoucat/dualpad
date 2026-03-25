#include "pch.h"
#include "input/mapping/LayerEvaluator.h"

namespace dualpad::input
{
    void LayerEvaluator::Evaluate(const PadState& previous, const PadState& current, PadEventBuffer& outEvents) const
    {
        const auto previousMask = previous.buttons.digitalMask;
        const auto currentMask = current.buttons.digitalMask;
        const auto pressedMask = currentMask & ~previousMask;

        for (int bitIndex = 0; bitIndex < 32; ++bitIndex) {
            const auto bit = (1u << bitIndex);
            if ((pressedMask & bit) == 0) {
                continue;
            }

            const auto modifierMask = (previousMask & currentMask) & ~bit;
            if (modifierMask == 0) {
                continue;
            }

            PadEvent event{};
            event.type = PadEventType::Layer;
            event.triggerType = TriggerType::Layer;
            event.code = bit;
            event.timestampUs = current.timestampUs;
            event.modifierMask = modifierMask;
            outEvents.Push(event);
        }
    }
}
