#include "pch.h"
#include "input/mapping/ComboEvaluator.h"

namespace dualpad::input
{
    void ComboEvaluator::Reset()
    {
    }

    void ComboEvaluator::Evaluate(const PadState& previous, const PadState& current, PadEventBuffer& outEvents)
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
            event.type = PadEventType::Combo;
            event.triggerType = TriggerType::Combo;
            event.code = bit;
            event.timestampUs = current.timestampUs;
            event.modifierMask = modifierMask;
            outEvents.Push(event);
        }
    }
}
