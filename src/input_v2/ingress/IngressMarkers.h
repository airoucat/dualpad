#pragma once

#include "input_v2/actions/LegacyInteractionInputAdapter.h"
#include "input/injection/PadEventSnapshot.h"
#include "input_v2/ingress/IngressBoundaryKey.h"
#include "input_v2/presentation/SourceEvidenceCollector.h"

#include <cstdint>
#include <optional>
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
        std::optional<dualpad::input::PadEventSnapshot> legacySnapshot;
        std::uint64_t firstSequence{ 0 };
        std::uint64_t sequence{ 0 };
        bool overflowed{ false };
        bool coalesced{ false };
        bool crossContextMismatch{ false };
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

    struct QueueOverflowPayload
    {
        bool hasManifest{ false };
        ManifestEpochChangedPayload manifest;
        bool hasUi{ false };
        UiSnapshotPayload ui;
        bool hasDeviceFamily{ false };
        DeviceFamilyChangedPayload deviceFamily;
        bool hasSourceEvidence{ false };
        presentation::SourceEvidenceSnapshot sourceEvidence;
        bool droppedControlSamples{ false };
        bool droppedPulseLedger{ false };
        bool droppedLegacySnapshot{ false };
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
        QueueOverflowPayload overflow;
    };

    IngressEvent MakeSequenceGapEvent();
    IngressEvent MakeQueueOverflowEvent();
    IngressEvent MakeExplicitResetEvent();
}
