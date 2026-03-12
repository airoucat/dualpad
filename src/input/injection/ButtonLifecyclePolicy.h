#pragma once

#include "input/Action.h"

#include <cstdint>
#include <string_view>

namespace dualpad::input
{
    // Legacy event-injector lifecycle helpers retained only for experimental
    // fallback paths. Poll-owned native digital commits now use the planner
    // backend lifecycle policy in src/input/backend/.
    enum class ButtonLifecycleMode : std::uint8_t
    {
        HoldWhileSourceDown,
        MinDownWindow
    };

    struct ButtonLifecyclePolicy
    {
        ButtonLifecycleMode mode{ ButtonLifecycleMode::HoldWhileSourceDown };
        std::uint32_t minDownUs{ 0 };
    };

    inline constexpr const char* ToString(ButtonLifecycleMode mode)
    {
        switch (mode) {
        case ButtonLifecycleMode::HoldWhileSourceDown:
            return "HoldWhileSourceDown";
        case ButtonLifecycleMode::MinDownWindow:
            return "MinDownWindow";
        default:
            return "Unknown";
        }
    }

    inline constexpr bool RequiresButtonLifecycleTracking(std::string_view actionId)
    {
        return actionId == actions::Jump ||
            actionId == actions::Sprint ||
            actionId == actions::Activate ||
            actionId == actions::Sneak ||
            actionId == actions::MenuScrollUp ||
            actionId == actions::MenuScrollDown ||
            actionId == actions::MenuPageUp ||
            actionId == actions::MenuPageDown;
    }

    inline constexpr ButtonLifecyclePolicy ResolveButtonLifecyclePolicy(std::string_view actionId)
    {
        if (actionId == actions::Jump) {
            return {
                .mode = ButtonLifecycleMode::MinDownWindow,
                .minDownUs = 70000
            };
        }

        if (actionId == actions::Activate) {
            return {
                .mode = ButtonLifecycleMode::MinDownWindow,
                .minDownUs = 40000
            };
        }

        return {};
    }
}
