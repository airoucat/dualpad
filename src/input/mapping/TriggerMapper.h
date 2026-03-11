#pragma once

#include "input/mapping/PadEvent.h"

#include <optional>

namespace dualpad::input
{
    class TriggerMapper
    {
    public:
        static std::optional<Trigger> TryMapEvent(const PadEvent& event);
    };
}
