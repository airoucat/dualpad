#pragma once

#include "input_v2/gameplay/RecoveryPlan.h"

#include <cstdint>

namespace dualpad::input_v2::ingress
{
    struct AssembledFactFrame;

    enum class TransitionReason : std::uint8_t
    {
        ManifestEpochChanged = 0,
        BoundaryKeyChanged,
        SequenceGap,
        QueueOverflow,
        ExplicitReset
    };

    gameplay::GameplayRecoveryInput ToGameplayRecoveryInput(const AssembledFactFrame& frame);
}
