#include "pch.h"

#include "input/InputContextNames.h"

#include "input_v2/context/ContextCatalog.h"

namespace dualpad::input
{
    std::optional<InputContext> ParseInputContextName(std::string_view name)
    {
        const auto& catalog = dualpad::input_v2::context::ContextCatalog::BuiltInCatalog();

        const auto ctxId = dualpad::input_v2::context::ContextCatalog::ResolveAlias(catalog, name);
        if (!ctxId) {
            return std::nullopt;
        }

        return dualpad::input_v2::context::ContextCatalog::ToLegacyInputContext(catalog, *ctxId);
    }
}
