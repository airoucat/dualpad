#pragma once

#include "input/mapping/PadEvent.h"
#include "input/state/PadState.h"

namespace dualpad::input
{
    class ComboEvaluator
    {
    public:
        void Reset();
        void Evaluate(const PadState& previous, const PadState& current, PadEventBuffer& outEvents);
    };
}
