#pragma once

#include "input_v2/context/ContextCatalog.h"

#include <string>
#include <vector>

namespace dualpad::input_v2::actions
{
    using ActionSetId = std::string;
    using ActionLayerId = std::string;
    using ScopeAnchorId = std::string;

    struct ActionSetStack
    {
        ActionSetId baseSetId;
        std::vector<ActionLayerId> layerIds;
        std::vector<ScopeAnchorId> scopeAnchorIds;

        friend bool operator==(const ActionSetStack&, const ActionSetStack&) = default;
    };

    class ActionSetResolver
    {
    public:
        static ActionSetStack Resolve(
            const context::CompiledContextCatalog& catalog,
            context::UiContextId uiContextId);
    };
}
