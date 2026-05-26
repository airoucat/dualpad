#include "pch.h"

#include "input_v2/ingress/LiveInputFactProducer.h"

#include "input_v2/ingress/LegacyIngressAdapter.h"

namespace dualpad::input_v2::ingress
{
    namespace
    {
        constexpr std::uint64_t kGamepadLeaseWindowTicks = 1'500'000;

        std::uint64_t TimestampUs(const dualpad::input::PadEventSnapshot& snapshot)
        {
            if (snapshot.sourceTimestampUs != 0) {
                return snapshot.sourceTimestampUs;
            }
            return snapshot.state.timestampUs;
        }

        actions::ControlSample DigitalSample(
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
    }

    LiveInputFactProducer& LiveInputFactProducer::GetSingleton()
    {
        static LiveInputFactProducer producer;
        return producer;
    }

    std::vector<actions::ControlSample> LiveInputFactProducer::BuildControlSamples(
        const dualpad::input::PadEventSnapshot& snapshot,
        bool synthesizeDigitalEdges)
    {
        std::vector<actions::ControlSample> samples;
        const auto timestampUs = TimestampUs(snapshot);
        const auto currentMask = snapshot.state.buttons.digitalMask;
        const auto pressedMask = synthesizeDigitalEdges ? (currentMask & ~_previousDownMask) : 0u;
        const auto releasedMask = synthesizeDigitalEdges ? (_previousDownMask & ~currentMask) : 0u;

        for (std::uint32_t index = 0; index < 32; ++index) {
            const auto bit = 1u << index;
            const bool down = (currentMask & bit) != 0;
            const bool pressed = (pressedMask & bit) != 0;
            const bool released = (releasedMask & bit) != 0;

            if (down) {
                if (pressed || _downAtUs[index] == 0) {
                    _downAtUs[index] = timestampUs;
                }
                samples.push_back(DigitalSample(bit, true, pressed, false, _downAtUs[index], timestampUs));
            } else if (released) {
                const auto downAtUs = _downAtUs[index] != 0 ? _downAtUs[index] : timestampUs;
                samples.push_back(DigitalSample(bit, false, false, true, downAtUs, timestampUs));
                _downAtUs[index] = 0;
            } else if ((_previousDownMask & bit) != 0 && !synthesizeDigitalEdges) {
                _downAtUs[index] = 0;
            }
        }

        samples.push_back(AxisSample(dualpad::input::PadAxisId::LeftStickX, snapshot.state.leftStick.x, timestampUs));
        samples.push_back(AxisSample(dualpad::input::PadAxisId::LeftStickY, snapshot.state.leftStick.y, timestampUs));
        samples.push_back(AxisSample(dualpad::input::PadAxisId::RightStickX, snapshot.state.rightStick.x, timestampUs));
        samples.push_back(AxisSample(dualpad::input::PadAxisId::RightStickY, snapshot.state.rightStick.y, timestampUs));
        samples.push_back(AxisSample(dualpad::input::PadAxisId::LeftTrigger, snapshot.state.leftTrigger.normalized, timestampUs));
        samples.push_back(AxisSample(dualpad::input::PadAxisId::RightTrigger, snapshot.state.rightTrigger.normalized, timestampUs));

        _previousDownMask = currentMask;
        return samples;
    }

    void LiveInputFactProducer::PublishGamepadSourceEvidence(
        const context::ResolvedContextSnapshot& contextSnapshot,
        std::uint64_t tick)
    {
        _sourceEvidenceCollector.RecordGamepadEvidence(true, tick, kGamepadLeaseWindowTicks);
        const auto publication = _deviceFamilyPublisher.Publish(
            presentation::DeviceFamily::Gamepad,
            presentation::DeviceFamilyEvidenceSource::RawInputIngress,
            tick);
        const auto frame = _sourceEvidenceCollector.CollectAfterDeviceFamilyIngress(
            publication,
            contextSnapshot,
            tick);
        PublishSourceEvidenceFrameToIngressHub(frame);
    }

    void LiveInputFactProducer::PublishKeyboardSourceEvidence(
        const context::ResolvedContextSnapshot& contextSnapshot,
        std::uint32_t scancode,
        std::uint64_t tick)
    {
        const bool syntheticSuppressed =
            _sourceEvidenceCollector.ConsumeSyntheticKeyboardScancode(scancode, tick);
        _sourceEvidenceCollector.RecordKeyboardEvidence(true, syntheticSuppressed, tick);
        if (syntheticSuppressed) {
            PublishCurrentSourceEvidence(contextSnapshot, tick);
            return;
        }

        PublishKeyboardMouseSourceEvidence(contextSnapshot, tick);
    }

    void LiveInputFactProducer::PublishMouseButtonSourceEvidence(
        const context::ResolvedContextSnapshot& contextSnapshot,
        std::uint64_t tick)
    {
        _sourceEvidenceCollector.RecordMouseButtonEvidence(true, tick);
        PublishKeyboardMouseSourceEvidence(contextSnapshot, tick);
    }

    void LiveInputFactProducer::PublishMouseMoveSourceEvidence(
        const context::ResolvedContextSnapshot& contextSnapshot,
        std::int32_t dx,
        std::int32_t dy,
        std::uint64_t tick)
    {
        if (dx == 0 && dy == 0) {
            return;
        }

        _sourceEvidenceCollector.RecordMouseMoveEvidence(dx, dy, tick);
        PublishKeyboardMouseSourceEvidence(contextSnapshot, tick);
    }

    void LiveInputFactProducer::MarkSyntheticKeyboardScancode(
        std::uint8_t scancode,
        std::uint8_t pendingEvents,
        std::uint64_t windowUs,
        std::uint64_t nowUs)
    {
        _sourceEvidenceCollector.MarkSyntheticKeyboardScancode(scancode, pendingEvents, windowUs, nowUs);
    }

    void LiveInputFactProducer::PublishKeyboardMouseSourceEvidence(
        const context::ResolvedContextSnapshot& contextSnapshot,
        std::uint64_t tick)
    {
        const auto publication = _deviceFamilyPublisher.Publish(
            presentation::DeviceFamily::KeyboardMouse,
            presentation::DeviceFamilyEvidenceSource::RawInputIngress,
            tick);
        const auto frame = _sourceEvidenceCollector.CollectAfterDeviceFamilyIngress(
            publication,
            contextSnapshot,
            tick);
        PublishSourceEvidenceFrameToIngressHub(frame);
    }

    void LiveInputFactProducer::PublishCurrentSourceEvidence(
        const context::ResolvedContextSnapshot& contextSnapshot,
        std::uint64_t tick)
    {
        const auto frame = _sourceEvidenceCollector.CollectAfterDeviceFamilyIngress(
            presentation::DeviceFamilyIngressPublication{
                .evidence = _deviceFamilyPublisher.GetPublished()
            },
            contextSnapshot,
            tick);
        PublishSourceEvidenceFrameToIngressHub(frame);
    }

    void LiveInputFactProducer::Reset()
    {
        _previousDownMask = 0;
        _downAtUs = {};
        _deviceFamilyPublisher.ResetForTests();
        _sourceEvidenceCollector.ResetForTests();
    }

    void LiveInputFactProducer::ResetForTests()
    {
        Reset();
    }
}
