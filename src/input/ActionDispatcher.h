#pragma once

#include "input/InputContext.h"
#include "input/backend/FrameActionPlan.h"
#include "input/mapping/PadEvent.h"

#include <cstdint>
#include <string_view>

namespace dualpad::input
{
    enum class ActionDispatchTarget : std::uint8_t
    {
        None = 0,
        NativeButtonCommit,
        KeyboardHelper,
        Plugin
    };

    struct ActionDispatchResult
    {
        bool handled{ false };
        ActionDispatchTarget target{ ActionDispatchTarget::None };
    };

    class ActionDispatcher
    {
    public:
        ActionDispatcher() = default;
        ActionDispatchResult DispatchPlannedAction(const backend::PlannedAction& action) const;
    };

    std::string_view ToString(ActionDispatchTarget target);
}
