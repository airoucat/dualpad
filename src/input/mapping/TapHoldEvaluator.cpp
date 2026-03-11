#include "pch.h"
#include "input/mapping/TapHoldEvaluator.h"

namespace dualpad::input
{
    void TapHoldEvaluator::SetConfig(const TapHoldConfig& config)
    {
        _config = config;
    }

    void TapHoldEvaluator::Reset()
    {
        _pressStartUs.fill(0);
        _holdEmitted.fill(false);
    }

    void TapHoldEvaluator::Evaluate(const PadState& previous, const PadState& current, PadEventBuffer& outEvents)
    {
        const auto previousMask = previous.buttons.digitalMask;
        const auto currentMask = current.buttons.digitalMask;

        for (int bitIndex = 0; bitIndex < 32; ++bitIndex) {
            const auto bit = (1u << bitIndex);
            const bool wasDown = (previousMask & bit) != 0;
            const bool isDown = (currentMask & bit) != 0;

            if (isDown && !wasDown) {
                _pressStartUs[bitIndex] = current.timestampUs;
                _holdEmitted[bitIndex] = false;
                continue;
            }

            if (isDown && wasDown) {
                const auto startedAt = _pressStartUs[bitIndex];
                if (startedAt != 0 &&
                    !_holdEmitted[bitIndex] &&
                    current.timestampUs >= startedAt &&
                    (current.timestampUs - startedAt) >= _config.holdThresholdUs) {
                    PadEvent event{};
                    event.type = PadEventType::Hold;
                    event.triggerType = TriggerType::Hold;
                    event.code = bit;
                    event.timestampUs = current.timestampUs;
                    event.modifierMask = currentMask & ~bit;
                    outEvents.Push(event);
                    _holdEmitted[bitIndex] = true;
                }
                continue;
            }

            if (!isDown && wasDown) {
                const auto startedAt = _pressStartUs[bitIndex];
                if (startedAt != 0 &&
                    !_holdEmitted[bitIndex] &&
                    current.timestampUs >= startedAt &&
                    (current.timestampUs - startedAt) <= _config.tapThresholdUs) {
                    PadEvent event{};
                    event.type = PadEventType::Tap;
                    event.triggerType = TriggerType::Tap;
                    event.code = bit;
                    event.timestampUs = current.timestampUs;
                    event.modifierMask = previousMask & ~bit;
                    outEvents.Push(event);
                }

                _pressStartUs[bitIndex] = 0;
                _holdEmitted[bitIndex] = false;
            }
        }
    }
}
