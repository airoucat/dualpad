#pragma once

#include "input/InputContext.h"
#include "input/mapping/PadEvent.h"

#include <optional>
#include <string>

namespace dualpad::input
{
    struct ResolvedBinding
    {
        Trigger trigger;
        std::string actionId;
    };

    class BindingResolver
    {
    public:
        std::optional<ResolvedBinding> Resolve(const PadEvent& event, InputContext context) const;
    };
}
