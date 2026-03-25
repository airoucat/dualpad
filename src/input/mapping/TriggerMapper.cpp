#include "pch.h"
#include "input/mapping/TriggerMapper.h"

namespace dualpad::input
{
    namespace
    {
        void FillModifiers(Trigger& trigger, std::uint32_t modifierMask)
        {
            trigger.modifiers.clear();
            for (int bitIndex = 0; bitIndex < 32; ++bitIndex) {
                const auto bit = (1u << bitIndex);
                if ((modifierMask & bit) != 0) {
                    trigger.modifiers.push_back(bit);
                }
            }
        }
    }

    std::optional<Trigger> TriggerMapper::TryMapEvent(const PadEvent& event)
    {
        Trigger trigger{};

        switch (event.type) {
        case PadEventType::ButtonPress:
            trigger.type = TriggerType::Button;
            trigger.code = event.code;
            FillModifiers(trigger, event.modifierMask);
            return trigger;

        case PadEventType::AxisChange:
            trigger.type = TriggerType::Axis;
            trigger.code = event.code;
            return trigger;

        case PadEventType::Layer:
            trigger.type = TriggerType::Layer;
            trigger.code = event.code;
            FillModifiers(trigger, event.modifierMask);
            return trigger;

        case PadEventType::Combo:
            trigger.type = TriggerType::Combo;
            trigger.code = event.code;
            FillModifiers(trigger, event.modifierMask);
            return trigger;

        case PadEventType::Hold:
            trigger.type = TriggerType::Hold;
            trigger.code = event.code;
            FillModifiers(trigger, event.modifierMask);
            return trigger;

        case PadEventType::Tap:
            trigger.type = TriggerType::Tap;
            trigger.code = event.code;
            FillModifiers(trigger, event.modifierMask);
            return trigger;

        case PadEventType::Gesture:
        case PadEventType::TouchpadPress:
        case PadEventType::TouchpadSlide:
            trigger.type = TriggerType::Gesture;
            trigger.code = event.code;
            return trigger.code != 0 ? std::optional<Trigger>{ trigger } : std::nullopt;

        case PadEventType::ButtonRelease:
        case PadEventType::TouchpadRelease:
        case PadEventType::None:
        default:
            return std::nullopt;
        }
    }
}
