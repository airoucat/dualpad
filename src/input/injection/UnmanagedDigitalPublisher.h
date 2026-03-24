#pragma once

#include "input/injection/SyntheticPadFrame.h"

#include <cstdint>

namespace dualpad::input
{
    void PublishUnmanagedDigitalState(const SyntheticPadFrame& frame, std::uint32_t handledButtons);
}
