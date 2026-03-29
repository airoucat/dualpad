#include "pch.h"
#include "input/mapping/PadEventGenerator.h"

#include "input/mapping/EventDebugLogger.h"

namespace logger = SKSE::log;

namespace dualpad::input
{
    void PadEventGenerator::Reset()
    {
        _comboEvaluator.Reset();
        _tapHoldEvaluator.Reset();
        _touchpadMapper.Reset();
    }

    TouchpadMapper& PadEventGenerator::GetTouchpadMapper()
    {
        return _touchpadMapper;
    }

    const TouchpadMapper& PadEventGenerator::GetTouchpadMapper() const
    {
        return _touchpadMapper;
    }

    void PadEventGenerator::Generate(
        const PadState& previous,
        const PadState& current,
        InputContext context,
        PadEventBuffer& outEvents)
    {
        outEvents.Clear();

        _comboEvaluator.Evaluate(previous, current, context, outEvents);
        _axisEvaluator.Evaluate(previous, current, outEvents);
        _tapHoldEvaluator.Evaluate(previous, current, outEvents);
        _layerEvaluator.Evaluate(previous, current, outEvents);
        _touchpadMapper.ProcessTouch(current, outEvents);

        if (outEvents.overflowed) {
            logger::warn(
                "[DualPad][Mapping] Dropped {} pad events because the per-frame buffer is full ({})",
                outEvents.droppedCount,
                kMaxPadEventsPerFrame);
        }

        LogPadEvents(outEvents);
    }
}
