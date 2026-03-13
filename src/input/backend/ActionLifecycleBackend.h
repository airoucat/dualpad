#pragma once

#include "input/InputContext.h"
#include "input/backend/ActionOutputContract.h"

#include <string_view>

namespace dualpad::input::backend
{
    // Shared digital-action lifecycle interface. Concrete backends can
    // materialize the same action contract as ButtonEvent, keyboard output,
    // plugin callbacks, or other future routes without changing the planner.
    class IActionLifecycleBackend
    {
    public:
        virtual ~IActionLifecycleBackend() = default;

        virtual void Reset() = 0;
        virtual bool IsRouteActive() const = 0;
        virtual bool CanHandleAction(std::string_view actionId) const = 0;
        virtual bool TriggerAction(
            std::string_view actionId,
            ActionOutputContract contract,
            InputContext context) = 0;
        virtual bool SubmitActionState(
            std::string_view actionId,
            ActionOutputContract contract,
            bool pressed,
            float heldSeconds,
            InputContext context) = 0;
    };
}
