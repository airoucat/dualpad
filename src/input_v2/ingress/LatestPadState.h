#pragma once

#include "input/injection/PadEventSnapshot.h"
#include "input_v2/actions/LegacyInteractionInputAdapter.h"
#include "input_v2/ingress/InputResetReason.h"
#include "input_v2/presentation/SourceEvidenceCollector.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dualpad::input_v2::ingress
{
    struct LatestPadState
    {
        std::uint64_t generation{ 0 };
        std::uint64_t sourceSequence{ 0 };
        std::uint64_t sourceTimestampUs{ 0 };
        dualpad::input::InputContext context{ dualpad::input::InputContext::Gameplay };
        std::uint32_t contextEpoch{ 0 };
        std::uint32_t contextRevision{ 0 };
        std::uint32_t currentDownMask{ 0 };
        dualpad::input::PadState state{};
        std::uint64_t causalOrderedTailSeq{ 0 };
        std::uint64_t inputStateEpoch{ 0 };
        std::uint64_t gamepadSessionId{ 0 };
        bool virtualGameplayEligible{ true };
        InputResetReasonMask recoveryReasons{ 0 };
    };

    struct LatestSourceEvidence
    {
        std::uint64_t generation{ 0 };
        presentation::SourceEvidenceSnapshot snapshot{};
    };

    inline std::uint64_t LatestTimestampUs(const dualpad::input::PadEventSnapshot& snapshot) noexcept
    {
        return snapshot.sourceTimestampUs != 0 ? snapshot.sourceTimestampUs : snapshot.state.timestampUs;
    }

    inline actions::ControlSample DigitalEdgeSample(
        std::uint32_t code,
        bool down,
        bool pressed,
        bool released,
        std::uint64_t downAtUs,
        std::uint64_t timestampUs)
    {
        return actions::ControlSample{
            .path = actions::ControlPath{
                .kind = actions::ControlPathKind::DigitalButton,
                .code = code
            },
            .down = down,
            .pressed = pressed,
            .released = released,
            .scalar = down ? 1.0f : 0.0f,
            .downAtUs = downAtUs,
            .timestampUs = timestampUs
        };
    }

    inline std::vector<actions::ControlSample> BuildOrderedDigitalEdges(
        const dualpad::input::PadEventSnapshot& snapshot,
        std::uint32_t previousDownMask,
        std::array<std::uint64_t, 32>& downAtUs)
    {
        std::vector<actions::ControlSample> samples;
        const auto timestampUs = LatestTimestampUs(snapshot);
        const auto currentMask = snapshot.state.buttons.digitalMask;

        if (snapshot.events.count == 0) {
            const auto pressedMask = currentMask & ~previousDownMask;
            const auto releasedMask = previousDownMask & ~currentMask;
            for (std::uint32_t index = 0; index < 32; ++index) {
                const auto bit = 1u << index;
                if ((pressedMask & bit) != 0) {
                    downAtUs[index] = timestampUs;
                    samples.push_back(DigitalEdgeSample(bit, true, true, false, timestampUs, timestampUs));
                } else if ((releasedMask & bit) != 0) {
                    const auto startedAt = downAtUs[index] != 0 ? downAtUs[index] : timestampUs;
                    samples.push_back(DigitalEdgeSample(bit, false, false, true, startedAt, timestampUs));
                    downAtUs[index] = 0;
                }
            }
            return samples;
        }

        for (std::size_t index = 0; index < snapshot.events.count; ++index) {
            const auto& event = snapshot.events[index];
            const auto eventTimestampUs = event.timestampUs != 0 ? event.timestampUs : timestampUs;
            switch (event.type) {
            case dualpad::input::PadEventType::ButtonPress:
            case dualpad::input::PadEventType::Hold:
            case dualpad::input::PadEventType::Tap:
            case dualpad::input::PadEventType::TouchpadPress: {
                const auto bitIndex = static_cast<std::uint32_t>(std::countr_zero(event.code));
                if (event.code != 0 && bitIndex < downAtUs.size()) {
                    downAtUs[bitIndex] = eventTimestampUs;
                }
                samples.push_back(DigitalEdgeSample(event.code, true, true, false, eventTimestampUs, eventTimestampUs));
                break;
            }
            case dualpad::input::PadEventType::ButtonRelease:
            case dualpad::input::PadEventType::TouchpadRelease: {
                const auto bitIndex = static_cast<std::uint32_t>(std::countr_zero(event.code));
                auto startedAt = eventTimestampUs;
                if (event.code != 0 && bitIndex < downAtUs.size()) {
                    startedAt = downAtUs[bitIndex] != 0 ? downAtUs[bitIndex] : eventTimestampUs;
                    downAtUs[bitIndex] = 0;
                }
                samples.push_back(DigitalEdgeSample(event.code, false, false, true, startedAt, eventTimestampUs));
                break;
            }
            default:
                break;
            }
        }
        return samples;
    }

    inline std::vector<actions::ControlSample> BuildLatestAnalogSamples(const LatestPadState& latest)
    {
        const auto makeAxis = [&](dualpad::input::PadAxisId axis, float value) {
            return actions::ControlSample{
                .path = actions::ControlPath{
                    .kind = actions::ControlPathKind::AnalogAxis1D,
                    .code = static_cast<std::uint32_t>(axis)
                },
                .down = value != 0.0f,
                .scalar = value,
                .timestampUs = latest.sourceTimestampUs
            };
        };

        return {
            makeAxis(dualpad::input::PadAxisId::LeftStickX, latest.state.leftStick.x),
            makeAxis(dualpad::input::PadAxisId::LeftStickY, latest.state.leftStick.y),
            makeAxis(dualpad::input::PadAxisId::RightStickX, latest.state.rightStick.x),
            makeAxis(dualpad::input::PadAxisId::RightStickY, latest.state.rightStick.y),
            makeAxis(dualpad::input::PadAxisId::LeftTrigger, latest.state.leftTrigger.normalized),
            makeAxis(dualpad::input::PadAxisId::RightTrigger, latest.state.rightTrigger.normalized)
        };
    }
}
