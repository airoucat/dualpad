#include "pch.h"

#include "input_v2/ingress/LegacyIngressAdapter.h"

#include "input_v2/ingress/IngressHub.h"

#include <bit>

namespace dualpad::input_v2::ingress
{
    namespace
    {
        actions::ControlSample DigitalSample(
            std::uint32_t code,
            bool down,
            bool pressed,
            bool released,
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
                .downAtUs = down ? timestampUs : 0,
                .timestampUs = timestampUs
            };
        }

        actions::ControlSample AxisSample(
            dualpad::input::PadAxisId axis,
            float value,
            std::uint64_t timestampUs)
        {
            return actions::ControlSample{
                .path = actions::ControlPath{
                    .kind = actions::ControlPathKind::AnalogAxis1D,
                    .code = static_cast<std::uint32_t>(axis)
                },
                .down = value != 0.0f,
                .scalar = value,
                .timestampUs = timestampUs
            };
        }

        std::uint64_t FirstSequenceOrSequence(const dualpad::input::PadEventSnapshot& snapshot)
        {
            return snapshot.firstSequence != 0 ? snapshot.firstSequence : snapshot.sequence;
        }

        void AppendStateSamples(
            const dualpad::input::PadEventSnapshot& snapshot,
            std::vector<actions::ControlSample>& samples)
        {
            const auto timestampUs = snapshot.sourceTimestampUs != 0 ?
                snapshot.sourceTimestampUs :
                snapshot.state.timestampUs;

            for (std::uint32_t bit = 1; bit != 0; bit <<= 1) {
                if ((snapshot.state.buttons.digitalMask & bit) != 0) {
                    samples.push_back(DigitalSample(bit, true, false, false, timestampUs));
                }
                if (bit == 0x80000000u) {
                    break;
                }
            }

            samples.push_back(AxisSample(dualpad::input::PadAxisId::LeftStickX, snapshot.state.leftStick.x, timestampUs));
            samples.push_back(AxisSample(dualpad::input::PadAxisId::LeftStickY, snapshot.state.leftStick.y, timestampUs));
            samples.push_back(AxisSample(dualpad::input::PadAxisId::RightStickX, snapshot.state.rightStick.x, timestampUs));
            samples.push_back(AxisSample(dualpad::input::PadAxisId::RightStickY, snapshot.state.rightStick.y, timestampUs));
            samples.push_back(AxisSample(dualpad::input::PadAxisId::LeftTrigger, snapshot.state.leftTrigger.normalized, timestampUs));
            samples.push_back(AxisSample(dualpad::input::PadAxisId::RightTrigger, snapshot.state.rightTrigger.normalized, timestampUs));
        }

        void AppendEventSamples(
            const dualpad::input::PadEventSnapshot& snapshot,
            std::vector<actions::ControlSample>& samples)
        {
            for (std::size_t index = 0; index < snapshot.events.count; ++index) {
                const auto& event = snapshot.events[index];
                switch (event.type) {
                case dualpad::input::PadEventType::ButtonPress:
                case dualpad::input::PadEventType::Hold:
                case dualpad::input::PadEventType::Tap:
                case dualpad::input::PadEventType::TouchpadPress:
                    samples.push_back(DigitalSample(event.code, true, true, false, event.timestampUs));
                    break;
                case dualpad::input::PadEventType::ButtonRelease:
                case dualpad::input::PadEventType::TouchpadRelease:
                    samples.push_back(DigitalSample(event.code, false, false, true, event.timestampUs));
                    break;
                case dualpad::input::PadEventType::AxisChange:
                    samples.push_back(AxisSample(event.axis, event.value, event.timestampUs));
                    break;
                default:
                    break;
                }
            }
        }
    }

    std::vector<IngressEvent> ConvertLegacySnapshotToIngressEvents(
        const dualpad::input::PadEventSnapshot& snapshot,
        std::uint64_t lastObservedSequence)
    {
        std::vector<IngressEvent> events;
        const auto firstSequence = FirstSequenceOrSequence(snapshot);
        if (lastObservedSequence != 0 &&
            firstSequence != 0 &&
            firstSequence != lastObservedSequence + 1) {
            auto gap = MakeSequenceGapEvent();
            gap.monotonicUs = snapshot.sourceTimestampUs;
            events.push_back(gap);
        }

        if (snapshot.type == dualpad::input::PadEventSnapshotType::Reset) {
            auto reset = MakeExplicitResetEvent();
            reset.monotonicUs = snapshot.sourceTimestampUs;
            events.push_back(reset);
            return events;
        }

        if (snapshot.overflowed || snapshot.events.overflowed) {
            auto overflow = MakeQueueOverflowEvent();
            overflow.monotonicUs = snapshot.sourceTimestampUs;
            events.push_back(overflow);
        }

        if (snapshot.coalesced || snapshot.crossContextMismatch) {
            auto reset = MakeExplicitResetEvent();
            reset.monotonicUs = snapshot.sourceTimestampUs;
            events.push_back(reset);
        }

        IngressEvent ui{};
        ui.kind = IngressKind::UiSnapshot;
        ui.source = IngressSource::LegacyDispatcher;
        ui.monotonicUs = snapshot.sourceTimestampUs;
        ui.ui = UiSnapshotPayload{
            .contextRevision = snapshot.contextEpoch,
            .menuStackRevision = snapshot.contextEpoch
        };
        events.push_back(ui);

        IngressEvent pad{};
        pad.kind = IngressKind::PadSnapshot;
        pad.source = IngressSource::LegacyDispatcher;
        pad.monotonicUs = snapshot.sourceTimestampUs;
        pad.pad.legacySnapshot = snapshot;
        pad.pad.firstSequence = firstSequence;
        pad.pad.sequence = snapshot.sequence;
        pad.pad.overflowed = snapshot.overflowed || snapshot.events.overflowed;
        pad.pad.coalesced = snapshot.coalesced;
        pad.pad.crossContextMismatch = snapshot.crossContextMismatch;
        AppendStateSamples(snapshot, pad.pad.samples);
        AppendEventSamples(snapshot, pad.pad.samples);
        events.push_back(std::move(pad));
        return events;
    }

    void PublishSourceEvidenceFrameToIngressHub(const presentation::SourceEvidenceFrame& frame)
    {
        auto& hub = IngressHub::GetSingleton();
        for (const auto& record : frame.records) {
            if (record.kind == presentation::SourceEvidenceRecordKind::DeviceFamilyChanged) {
                IngressEvent marker{};
                marker.kind = IngressKind::DeviceFamilyChanged;
                marker.source = IngressSource::DeviceFamilyPublisher;
                marker.monotonicUs = record.deviceFamilyChanged.publishedTick;
                marker.deviceFamily = DeviceFamilyChangedPayload{
                    .family = record.deviceFamilyChanged.family,
                    .deviceFamilyRevision = record.deviceFamilyChanged.newRevision
                };
                (void)hub.PushEvent(std::move(marker));
            } else {
                IngressEvent evidence{};
                evidence.kind = IngressKind::SourceEvidence;
                evidence.source = IngressSource::DeviceFamilyPublisher;
                evidence.monotonicUs = record.sourceEvidence.collectedTick;
                evidence.sourceEvidence = record.sourceEvidence;
                (void)hub.PushEvent(std::move(evidence));
            }
        }
    }
}
