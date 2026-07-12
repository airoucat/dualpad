#include "pch.h"

#include "input_v2/ingress/GamepadActivityClassifier.h"

#include "input/PadEvent.h"
#include "input/protocol/DualSenseButtons.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>

namespace dualpad::input_v2::ingress
{
    namespace
    {
        constexpr float kStickEnterThreshold = 0.25F;
        constexpr float kStickL1ChangeThreshold = 0.12F;
        constexpr float kStickDirectionChangeDegrees = 12.0F;
        constexpr float kTriggerEnterThreshold = 0.15F;
        constexpr float kTriggerChangeThreshold = 0.08F;

        float Magnitude(const dualpad::input::StickState& stick)
        {
            return std::sqrt(stick.x * stick.x + stick.y * stick.y);
        }

        float DirectionChangeDegrees(
            const dualpad::input::StickState& previous,
            const dualpad::input::StickState& current)
        {
            const auto previousMagnitude = Magnitude(previous);
            const auto currentMagnitude = Magnitude(current);
            if (previousMagnitude == 0.0F || currentMagnitude == 0.0F) {
                return 0.0F;
            }
            const auto cosine = std::clamp(
                (previous.x * current.x + previous.y * current.y) /
                    (previousMagnitude * currentMagnitude),
                -1.0F,
                1.0F);
            return std::acos(cosine) * 180.0F / std::numbers::pi_v<float>;
        }

        void AppendSourceActivity(
            ClassifiedGamepadReportDraft& report,
            SourceActivityKind kind,
            std::uint32_t controlCode,
            std::uint64_t sourceTimestampUs)
        {
            report.sourceActivities.push_back(MeaningfulSourceActivityDraft{
                .source = PhysicalInputSource::Gamepad,
                .kind = kind,
                .controlCode = controlCode,
                .producerTimestampUs = sourceTimestampUs
            });
        }

        void AppendTypedActivity(
            ClassifiedGamepadReportDraft& report,
            GamepadActivityReason reason,
            std::uint32_t controlCode,
            float magnitude,
            std::uint64_t sourceSequence,
            std::uint64_t sourceTimestampUs,
            SourceActivityKind sourceKind)
        {
            report.meaningfulActivities.push_back(GamepadActivityDraft{
                .reason = reason,
                .controlCode = controlCode,
                .magnitude = magnitude,
                .sourceSequence = sourceSequence,
                .sourceTimestampUs = sourceTimestampUs
            });
            AppendSourceActivity(report, sourceKind, controlCode, sourceTimestampUs);
        }

        void ClassifyStick(
            ClassifiedGamepadReportDraft& report,
            const dualpad::input::StickState& previous,
            const dualpad::input::StickState& current,
            GamepadActivityReason enteredReason,
            GamepadActivityReason changedReason,
            dualpad::input::PadAxisId control,
            std::uint64_t sourceSequence,
            std::uint64_t sourceTimestampUs)
        {
            const auto previousMagnitude = Magnitude(previous);
            const auto currentMagnitude = Magnitude(current);
            const auto controlCode = static_cast<std::uint32_t>(control);
            if (previousMagnitude < kStickEnterThreshold && currentMagnitude >= kStickEnterThreshold) {
                AppendTypedActivity(
                    report,
                    enteredReason,
                    controlCode,
                    currentMagnitude,
                    sourceSequence,
                    sourceTimestampUs,
                    SourceActivityKind::GamepadAnalogEnter);
                return;
            }
            if (previousMagnitude < kStickEnterThreshold || currentMagnitude < kStickEnterThreshold) {
                return;
            }

            const auto l1Change = std::fabs(current.x - previous.x) + std::fabs(current.y - previous.y);
            const auto directionChange = DirectionChangeDegrees(previous, current);
            if (l1Change >= kStickL1ChangeThreshold ||
                (currentMagnitude >= kStickEnterThreshold &&
                    directionChange >= kStickDirectionChangeDegrees)) {
                AppendTypedActivity(
                    report,
                    changedReason,
                    controlCode,
                    currentMagnitude,
                    sourceSequence,
                    sourceTimestampUs,
                    SourceActivityKind::GamepadAnalogChange);
            }
        }

        void ClassifyTrigger(
            ClassifiedGamepadReportDraft& report,
            float previous,
            float current,
            GamepadActivityReason enteredReason,
            GamepadActivityReason changedReason,
            dualpad::input::PadAxisId control,
            std::uint64_t sourceSequence,
            std::uint64_t sourceTimestampUs)
        {
            const auto controlCode = static_cast<std::uint32_t>(control);
            if (previous < kTriggerEnterThreshold && current >= kTriggerEnterThreshold) {
                AppendTypedActivity(
                    report,
                    enteredReason,
                    controlCode,
                    current,
                    sourceSequence,
                    sourceTimestampUs,
                    SourceActivityKind::GamepadAnalogEnter);
                return;
            }
            if (previous < kTriggerEnterThreshold || current < kTriggerEnterThreshold) {
                return;
            }
            if (std::fabs(current - previous) >= kTriggerChangeThreshold) {
                AppendTypedActivity(
                    report,
                    changedReason,
                    controlCode,
                    current,
                    sourceSequence,
                    sourceTimestampUs,
                    SourceActivityKind::GamepadAnalogChange);
            }
        }
    }

    ClassifiedGamepadReportDraft GamepadActivityClassifier::Classify(
        const dualpad::input::PadState& previous,
        const dualpad::input::PadState& current,
        std::uint64_t sourceSequence,
        std::uint64_t sourceTimestampUs) const
    {
        ClassifiedGamepadReportDraft report{
            .current = GamepadCurrentStateDraft{
                .sourceSequence = sourceSequence,
                .sourceTimestampUs = sourceTimestampUs,
                .state = current,
                .currentDownMask = current.buttons.digitalMask
            }
        };

        const auto pressedMask = current.buttons.digitalMask & ~previous.buttons.digitalMask;
        const auto releasedMask = previous.buttons.digitalMask & ~current.buttons.digitalMask;
        for (std::uint32_t index = 0; index < 32; ++index) {
            const auto controlCode = 1u << index;
            if ((pressedMask & controlCode) != 0) {
                report.orderedDigitalEdges.push_back(GamepadDigitalEdgeDraft{
                    .phase = GamepadDigitalEdgePhase::Press,
                    .controlCode = controlCode,
                    .sourceSequence = sourceSequence,
                    .sourceTimestampUs = sourceTimestampUs
                });
                const bool touchpad = controlCode == dualpad::input::protocol::buttons::kTouchpadClick;
                AppendTypedActivity(
                    report,
                    touchpad ? GamepadActivityReason::TouchpadActivity : GamepadActivityReason::DigitalPress,
                    controlCode,
                    1.0F,
                    sourceSequence,
                    sourceTimestampUs,
                    touchpad ? SourceActivityKind::GamepadTouchPress : SourceActivityKind::GamepadButtonPress);
            }
            if ((releasedMask & controlCode) != 0) {
                report.orderedDigitalEdges.push_back(GamepadDigitalEdgeDraft{
                    .phase = GamepadDigitalEdgePhase::Release,
                    .controlCode = controlCode,
                    .sourceSequence = sourceSequence,
                    .sourceTimestampUs = sourceTimestampUs
                });
            }
        }

        ClassifyStick(
            report,
            previous.leftStick,
            current.leftStick,
            GamepadActivityReason::LeftStickEntered,
            GamepadActivityReason::LeftStickChanged,
            dualpad::input::PadAxisId::LeftStickX,
            sourceSequence,
            sourceTimestampUs);
        ClassifyStick(
            report,
            previous.rightStick,
            current.rightStick,
            GamepadActivityReason::RightStickEntered,
            GamepadActivityReason::RightStickChanged,
            dualpad::input::PadAxisId::RightStickX,
            sourceSequence,
            sourceTimestampUs);
        ClassifyTrigger(
            report,
            previous.leftTrigger.normalized,
            current.leftTrigger.normalized,
            GamepadActivityReason::LeftTriggerEntered,
            GamepadActivityReason::LeftTriggerChanged,
            dualpad::input::PadAxisId::LeftTrigger,
            sourceSequence,
            sourceTimestampUs);
        ClassifyTrigger(
            report,
            previous.rightTrigger.normalized,
            current.rightTrigger.normalized,
            GamepadActivityReason::RightTriggerEntered,
            GamepadActivityReason::RightTriggerChanged,
            dualpad::input::PadAxisId::RightTrigger,
            sourceSequence,
            sourceTimestampUs);

        return report;
    }

    void GamepadActivityClassifier::Reset(InputResetReasonMask) noexcept
    {}
}
