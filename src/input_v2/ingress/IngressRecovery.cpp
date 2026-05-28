#include "pch.h"

#include "input_v2/ingress/FrameAssembler.h"
#include "input_v2/ingress/IngressRecovery.h"

namespace dualpad::input_v2::ingress
{
    gameplay::GameplayRecoveryInput ToGameplayRecoveryInput(const AssembledFactFrame& frame)
    {
        gameplay::GameplayRecoveryInput input{};
        if (frame.kind != AssembledFrameKind::Transition) {
            return input;
        }

        input.softResyncRequested = frame.transition.requestSoftResync;
        input.hardResetRequested = frame.transition.requestHardResync;
        input.sequenceGapObserved = frame.transition.reason == TransitionReason::SequenceGap;
        input.explicitResetRequested = frame.transition.reason == TransitionReason::ExplicitReset ||
            frame.transition.reason == TransitionReason::ManifestEpochChanged ||
            frame.transition.reason == TransitionReason::QueueOverflow;
        return input;
    }
}
