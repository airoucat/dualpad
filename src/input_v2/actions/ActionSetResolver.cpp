#include "pch.h"
#include "input_v2/actions/ActionSetResolver.h"

namespace dualpad::input_v2::actions
{
    ActionSetStack ActionSetResolver::Resolve(
        const context::CompiledContextCatalog& catalog,
        context::UiContextId uiContextId)
    {
        const auto* entry = context::ContextCatalog::FindById(catalog, uiContextId);
        if (entry && entry->defaultActionSetId) {
            return ActionSetStack{
                .baseSetId = *entry->defaultActionSetId,
                .layerIds = entry->defaultLayerIds,
                .scopeAnchorIds = entry->scopeAnchorIds
            };
        }

        return ActionSetStack{
            .baseSetId = "MenuBase",
            .layerIds = { "UnknownTrackedMenuLayer" },
            .scopeAnchorIds = { "MenuBase", "UnknownTrackedMenuLayer" }
        };
    }
}
