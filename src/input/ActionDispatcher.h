#pragma once

#include "input/InputContext.h"
#include "input/backend/FrameActionPlan.h"
#include "input/mapping/PadEvent.h"

#include <cstdint>
#include <string_view>

namespace dualpad::input
{
    class CompatibilityInputInjector;

    enum class ActionDispatchTarget : std::uint8_t
    {
        None = 0,
        ButtonEvent,
        KeyboardNative,
        CompatibilityPulse,
        CompatibilityState,
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
        explicit ActionDispatcher(CompatibilityInputInjector& compatibilityInjector);

        void DispatchDirectPadEvent(const PadEvent& event) const;
        ActionDispatchResult DispatchPlannedAction(const backend::PlannedAction& action) const;

    private:
        CompatibilityInputInjector& _compatibilityInjector;
    };

    std::string_view ToString(ActionDispatchTarget target);
}
