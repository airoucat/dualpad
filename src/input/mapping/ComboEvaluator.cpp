#include "pch.h"
#include "input/mapping/ComboEvaluator.h"

namespace dualpad::input
{
    void ComboEvaluator::Reset()
    {
    }

    void ComboEvaluator::Evaluate(const PadState& previous, const PadState& current, PadEventBuffer& outEvents)
    {
        (void)previous;
        (void)current;
        (void)outEvents;
    }
}
