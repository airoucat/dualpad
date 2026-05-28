#include "pch.h"

#include "input_v2/prompt/PromptScope.h"

namespace dualpad::input_v2::prompt
{
    std::string_view ToString(PromptScopeState state)
    {
        switch (state) {
        case PromptScopeState::Ready:
            return "Ready";
        case PromptScopeState::Unavailable:
        default:
            return "Unavailable";
        }
    }

    std::string_view ToString(PromptScopeSelectorKind kind)
    {
        switch (kind) {
        case PromptScopeSelectorKind::CurrentPublished:
            return "CurrentPublished";
        case PromptScopeSelectorKind::ExplicitContextName:
        default:
            return "ExplicitContextName";
        }
    }
}
