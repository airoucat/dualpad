#include "pch.h"
#include "input/injection/SyntheticStateReducer.h"

#include <cmath>

namespace dualpad::input
{
    namespace
    {
        constexpr std::size_t AxisIndex(PadAxisId axis)
        {
            switch (axis) {
            case PadAxisId::LeftStickX: return 0;
            case PadAxisId::LeftStickY: return 1;
            case PadAxisId::RightStickX: return 2;
            case PadAxisId::RightStickY: return 3;
            case PadAxisId::LeftTrigger: return 4;
            case PadAxisId::RightTrigger: return 5;
            default: return 0;
            }
        }

        void UpdateAxisState(
            SyntheticAxisState& axisState,
            float currentValue,
            float previousValue,
            bool changed)
        {
            axisState.value = currentValue;
            axisState.previousValue = previousValue;
            axisState.changed = changed || std::fabs(currentValue - previousValue) > 0.0001f;
        }
    }

    void SyntheticStateReducer::Reset()
    {
        _latest = {};
        _previousDownMask = 0;
        _pressedAtUs.fill(0);
        _releasedAtUs.fill(0);
        _previousAxisValues.fill(0.0f);
    }

    const SyntheticPadFrame& SyntheticStateReducer::GetLatestFrame() const
    {
        return _latest;
    }

    const SyntheticPadFrame& SyntheticStateReducer::Reduce(
        const PadEventSnapshot& snapshot,
        InputContext context)
    {
        _latest = {};
        _latest.firstSequence = snapshot.firstSequence;
        _latest.sequence = snapshot.sequence;
        _latest.sourceTimestampUs = snapshot.sourceTimestampUs;
        _latest.context = context;
        _latest.overflowed = snapshot.overflowed || snapshot.events.overflowed;
        _latest.coalesced = snapshot.coalesced;

        ReduceButtons(snapshot);
        ReduceAxes(snapshot);
        ReduceSemanticEvents(snapshot);

        return _latest;
    }

    void SyntheticStateReducer::ReduceButtons(const PadEventSnapshot& snapshot)
    {
        const auto currentMask = snapshot.state.buttons.digitalMask;

        _latest.downMask = currentMask;
        _latest.pressedMask = currentMask & ~_previousDownMask;
        _latest.releasedMask = _previousDownMask & ~currentMask;
        _latest.heldMask = currentMask & _previousDownMask;

        for (int bitIndex = 0; bitIndex < 32; ++bitIndex) {
            const auto bit = (1u << bitIndex);
            auto& button = _latest.buttons[bitIndex];
            button.code = bit;
            button.down = (currentMask & bit) != 0;
            button.pressed = (_latest.pressedMask & bit) != 0;
            button.released = (_latest.releasedMask & bit) != 0;
            button.held = (_latest.heldMask & bit) != 0;

            if (button.pressed) {
                _pressedAtUs[bitIndex] = snapshot.sourceTimestampUs;
            }

            if (button.released) {
                _releasedAtUs[bitIndex] = snapshot.sourceTimestampUs;
            }

            button.pressedAtUs = _pressedAtUs[bitIndex];
            button.releasedAtUs = _releasedAtUs[bitIndex];

            if (button.down &&
                button.pressedAtUs != 0 &&
                snapshot.sourceTimestampUs >= button.pressedAtUs) {
                button.heldSeconds = static_cast<float>(snapshot.sourceTimestampUs - button.pressedAtUs) / 1000000.0f;
            }
        }

        _previousDownMask = currentMask;
    }

    void SyntheticStateReducer::ReduceAxes(const PadEventSnapshot& snapshot)
    {
        std::array<bool, 6> changed{};
        for (std::size_t i = 0; i < snapshot.events.count; ++i) {
            const auto& event = snapshot.events[i];
            if (event.type == PadEventType::AxisChange && event.axis != PadAxisId::None) {
                changed[AxisIndex(event.axis)] = true;
            }
        }

        UpdateAxisState(_latest.leftStickX, snapshot.state.leftStick.x, _previousAxisValues[0], changed[0]);
        UpdateAxisState(_latest.leftStickY, snapshot.state.leftStick.y, _previousAxisValues[1], changed[1]);
        UpdateAxisState(_latest.rightStickX, snapshot.state.rightStick.x, _previousAxisValues[2], changed[2]);
        UpdateAxisState(_latest.rightStickY, snapshot.state.rightStick.y, _previousAxisValues[3], changed[3]);
        UpdateAxisState(_latest.leftTrigger, snapshot.state.leftTrigger.normalized, _previousAxisValues[4], changed[4]);
        UpdateAxisState(_latest.rightTrigger, snapshot.state.rightTrigger.normalized, _previousAxisValues[5], changed[5]);

        _previousAxisValues[0] = snapshot.state.leftStick.x;
        _previousAxisValues[1] = snapshot.state.leftStick.y;
        _previousAxisValues[2] = snapshot.state.rightStick.x;
        _previousAxisValues[3] = snapshot.state.rightStick.y;
        _previousAxisValues[4] = snapshot.state.leftTrigger.normalized;
        _previousAxisValues[5] = snapshot.state.rightTrigger.normalized;
    }

    void SyntheticStateReducer::ReduceSemanticEvents(const PadEventSnapshot& snapshot)
    {
        std::uint32_t transientPressedMask = 0;
        std::uint32_t transientReleasedMask = 0;

        for (std::size_t i = 0; i < snapshot.events.count; ++i) {
            const auto& event = snapshot.events[i];
            const auto eventTimestampUs = event.timestampUs != 0 ? event.timestampUs : snapshot.sourceTimestampUs;

            const auto updateButtonFlag = [&](std::uint32_t mask, auto flagSetter) {
                if (mask == 0 || !IsSyntheticPadBitCode(mask)) {
                    return;
                }

                const auto bitIndex = static_cast<std::size_t>(std::countr_zero(mask));
                flagSetter(_latest.buttons[bitIndex]);
            };

            switch (event.type) {
            case PadEventType::ButtonPress:
                if (IsSyntheticPadBitCode(event.code)) {
                    transientPressedMask |= event.code;
                    updateButtonFlag(event.code, [&](SyntheticButtonState& button) {
                        button.sawPressEdge = true;
                        if (button.firstPressUs == 0 || eventTimestampUs < button.firstPressUs) {
                            button.firstPressUs = eventTimestampUs;
                        }
                        });
                }
                break;

            case PadEventType::ButtonRelease:
                if (IsSyntheticPadBitCode(event.code)) {
                    transientReleasedMask |= event.code;
                    updateButtonFlag(event.code, [&](SyntheticButtonState& button) {
                        button.sawReleaseEdge = true;
                        if (eventTimestampUs > button.lastReleaseUs) {
                            button.lastReleaseUs = eventTimestampUs;
                        }
                        });
                }
                break;

            case PadEventType::Tap:
                if (IsSyntheticPadBitCode(event.code)) {
                    _latest.tapMask |= event.code;
                    updateButtonFlag(event.code, [](SyntheticButtonState& button) {
                        button.tapTriggered = true;
                        });
                }
                break;

            case PadEventType::Hold:
                if (IsSyntheticPadBitCode(event.code)) {
                    _latest.holdMask |= event.code;
                    updateButtonFlag(event.code, [](SyntheticButtonState& button) {
                        button.holdTriggered = true;
                        });
                }
                break;

            case PadEventType::Combo:
                if (IsSyntheticPadBitCode(event.code)) {
                    _latest.comboMask |= event.code;
                    updateButtonFlag(event.code, [](SyntheticButtonState& button) {
                        button.comboTriggered = true;
                        });
                }
                break;

            case PadEventType::Gesture:
                ++_latest.gestureCount;
                break;

            case PadEventType::TouchpadPress:
                ++_latest.touchpadPressCount;
                break;

            case PadEventType::TouchpadRelease:
                ++_latest.touchpadReleaseCount;
                break;

            case PadEventType::TouchpadSlide:
                ++_latest.touchpadSlideCount;
                break;

            default:
                break;
            }
        }

        _latest.transientPressedMask = transientPressedMask;
        _latest.transientReleasedMask = transientReleasedMask;
        _latest.pulseMask = (transientPressedMask & transientReleasedMask) & ~_latest.downMask;

        for (int bitIndex = 0; bitIndex < 32; ++bitIndex) {
            const auto bit = (1u << bitIndex);
            if ((_latest.pulseMask & bit) != 0) {
                _latest.buttons[bitIndex].sawPulse = true;
            }
        }
    }
}
