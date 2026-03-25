#pragma once

#include "input/mapping/PadEvent.h"
#include "input/state/PadState.h"

namespace dualpad::input
{
    class LayerEvaluator
    {
    public:
        void Evaluate(const PadState& previous, const PadState& current, PadEventBuffer& outEvents) const;
    };
}
