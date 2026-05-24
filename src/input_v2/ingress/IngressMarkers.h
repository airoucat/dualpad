#pragma once

#include "input_v2/actions/LegacyInteractionInputAdapter.h"
#include "input_v2/ingress/IngressBoundaryKey.h"
#include "input_v2/presentation/SourceEvidenceCollector.h"

#include <cstdint>
#include <vector>

namespace dualpad::input_v2::ingress
{
    enum class IngressSource : std::uint8_t
    {
        Unknown = 0,
        LegacyDispatcher,
        UiObserver,
        ManifestPublisher,
        DeviceFamilyPublisher,
        Recovery
    };

    enum class IngressKind : std::uint8_t
    {
        PadSnapshot = 0,
        UiSnapshot,
        HostFacts,
        SourceEvidence,
        ManifestEpochChanged,
        DeviceFamilyChanged,
        ExplicitReset,
        QueueOverflow,
        SequenceGap
    };

    struct PadSnapshotPayload
    {
        std::vector<actions::ControlSample> samples;
    };

    struct UiSnapshotPayload
    {
        std::uint32_t contextRevision{ 0 };
        std::uint32_t menuStackRevision{ 0 };
    };

    struct HostFactsPayload
    {
        std::uint64_t tick{ 0 };
    };

    struct ManifestEpochChangedPayload
    {
        std::uint32_t manifestEpoch{ 0 };
    };

    struct DeviceFamilyChangedPayload
    {
        presentation::DeviceFamily family{ presentation::DeviceFamily::KeyboardMouse };
        std::uint32_t deviceFamilyRevision{ 0 };
    };

    struct IngressEvent
    {
        std::uint64_t seq{ 0 };
        std::uint64_t monotonicUs{ 0 };
        IngressSource source{ IngressSource::Unknown };
        IngressKind kind{ IngressKind::PadSnapshot };
        PadSnapshotPayload pad;
        UiSnapshotPayload ui;
        HostFactsPayload host;
        presentation::SourceEvidenceSnapshot sourceEvidence;
        ManifestEpochChangedPayload manifest;
        DeviceFamilyChangedPayload deviceFamily;
    };

    IngressEvent MakeSequenceGapEvent();
    IngressEvent MakeQueueOverflowEvent();
    IngressEvent MakeExplicitResetEvent();
}
