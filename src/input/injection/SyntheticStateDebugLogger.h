#pragma once

#include "input/injection/SyntheticPadFrame.h"

namespace dualpad::input
{
    bool IsSyntheticStateDebugLogEnabled();
    void LogSyntheticPadFrame(const SyntheticPadFrame& frame);
}
