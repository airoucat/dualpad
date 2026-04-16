#pragma once

#include "input/InputContext.h"

#include <optional>
#include <string_view>

namespace dualpad::input
{
    // Accepts config/menu aliases and maps them to the canonical InputContext enum.
    std::optional<InputContext> ParseInputContextName(std::string_view name);

    // Menu-owned contexts participate in menu policy and glyph lookup.
    constexpr bool IsMenuOwnedContext(InputContext context)
    {
        const auto value = static_cast<std::uint16_t>(context);
        return (value >= 100 && value < 2000) || context == InputContext::Console;
    }

    constexpr bool IsGameplayDomainContext(InputContext context)
    {
        return !IsMenuOwnedContext(context);
    }
}
