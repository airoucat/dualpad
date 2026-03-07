#pragma once

#include "input/mapping/PadEvent.h"
#include "input/state/PadState.h"

namespace dualpad::input
{
    struct AxisEvaluatorConfig
    {
        float stickThreshold{ 0.015f };
        float triggerThreshold{ 0.02f };
    };

    class AxisEvaluator
    {
    public:
        void SetConfig(const AxisEvaluatorConfig& config);
        void Evaluate(const PadState& previous, const PadState& current, PadEventBuffer& outEvents) const;

    private:
        AxisEvaluatorConfig _config{};
    };
}
