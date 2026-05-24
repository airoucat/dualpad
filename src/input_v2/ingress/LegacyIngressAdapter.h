#pragma once

#include "input/injection/PadEventSnapshot.h"
#include "input_v2/ingress/IngressMarkers.h"
#include "input_v2/presentation/SourceEvidenceCollector.h"

#include <cstdint>
#include <vector>

namespace dualpad::input_v2::ingress
{
    std::vector<IngressEvent> ConvertLegacySnapshotToIngressEvents(
        const dualpad::input::PadEventSnapshot& snapshot,
        std::uint64_t lastObservedSequence);

    void PublishSourceEvidenceFrameToIngressHub(const presentation::SourceEvidenceFrame& frame);
}
