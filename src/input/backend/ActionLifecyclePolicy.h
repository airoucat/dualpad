#pragma once

#include <cstdint>
#include <string_view>

namespace dualpad::input::backend
{
    // Backend-neutral lifecycle policy hints. The planner decides policy;
    // Poll-time commit only materializes visibility for one Poll.
    enum class ActionLifecyclePolicy : std::uint8_t
    {
        None = 0,
        DeferredPulse,
        MinDownWindowPulse,
        HoldOwner,
        ToggleOwner,
        RepeatOwner,
        AxisValue
    };

    inline constexpr std::string_view ToString(ActionLifecyclePolicy policy)
    {
        switch (policy) {
        case ActionLifecyclePolicy::DeferredPulse:
            return "DeferredPulse";
        case ActionLifecyclePolicy::MinDownWindowPulse:
            return "MinDownWindowPulse";
        case ActionLifecyclePolicy::HoldOwner:
            return "HoldOwner";
        case ActionLifecyclePolicy::ToggleOwner:
            return "ToggleOwner";
        case ActionLifecyclePolicy::RepeatOwner:
            return "RepeatOwner";
        case ActionLifecyclePolicy::AxisValue:
            return "AxisValue";
        case ActionLifecyclePolicy::None:
        default:
            return "None";
        }
    }
}
