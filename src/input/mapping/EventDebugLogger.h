#pragma once

#include "input/mapping/PadEvent.h"

namespace dualpad::input
{
    bool IsMappingDebugLogEnabled();
    void LogPadEvent(const PadEvent& event);
    void LogPadEvents(const PadEventBuffer& events);
}
