#include "pch.h"

#include "input_v2/ingress/IngressMarkers.h"

namespace dualpad::input_v2::ingress
{
    IngressEvent MakeSequenceGapEvent()
    {
        IngressEvent event{};
        event.kind = IngressKind::SequenceGap;
        event.source = IngressSource::Recovery;
        return event;
    }

    IngressEvent MakeQueueOverflowEvent()
    {
        IngressEvent event{};
        event.kind = IngressKind::QueueOverflow;
        event.source = IngressSource::Recovery;
        return event;
    }

    IngressEvent MakeExplicitResetEvent()
    {
        IngressEvent event{};
        event.kind = IngressKind::ExplicitReset;
        event.source = IngressSource::Recovery;
        return event;
    }
}
