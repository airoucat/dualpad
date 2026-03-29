#pragma once

#include "input/mapping/AxisEvaluator.h"
#include "input/mapping/ComboEvaluator.h"
#include "input/mapping/LayerEvaluator.h"
#include "input/mapping/TapHoldEvaluator.h"
#include "input/mapping/TouchpadMapper.h"
#include "input/InputContext.h"
#include "input/state/PadState.h"

namespace dualpad::input
{
    class PadEventGenerator
    {
    public:
        void Reset();

        TouchpadMapper& GetTouchpadMapper();
        const TouchpadMapper& GetTouchpadMapper() const;

        void Generate(
            const PadState& previous,
            const PadState& current,
            InputContext context,
            PadEventBuffer& outEvents);

    private:
        AxisEvaluator _axisEvaluator{};
        ComboEvaluator _comboEvaluator{};
        LayerEvaluator _layerEvaluator{};
        TapHoldEvaluator _tapHoldEvaluator{};
        TouchpadMapper _touchpadMapper{};
    };
}
