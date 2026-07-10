#pragma once

#include "input/injection/PadEventSnapshot.h"
#include "input_v2/ingress/IngressMarkers.h"
#include "input_v2/presentation/SourceEvidenceCollector.h"

#include <cstdint>
#include <vector>

namespace dualpad::input_v2::ingress
{
    struct LegacyIngressConversionOptions
    {
        bool includeContinuousSamples{ true };
        bool retainLegacySnapshot{ true };
        bool includeUiSnapshot{ true };
        bool resetLiveProducer{ true };
    };

    std::vector<IngressEvent> ConvertLegacySnapshotToIngressEvents(
        const dualpad::input::PadEventSnapshot& snapshot,
        std::uint64_t lastObservedSequence,
        LegacyIngressConversionOptions options = {});

    void PublishSourceEvidenceFrameToIngressHub(const presentation::SourceEvidenceFrame& frame);
}
