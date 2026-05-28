#pragma once

#include "input_v2/actions/ActionSetResolver.h"
#include "input_v2/context/ContextCatalog.h"
#include "input_v2/presentation/SourceEvidenceCollector.h"

#include <cstdint>
#include <string_view>

namespace dualpad::input_v2::prompt
{
    enum class PromptScopeState : std::uint8_t
    {
        Ready = 0,
        Unavailable
    };

    enum class PromptScopeSelectorKind : std::uint8_t
    {
        CurrentPublished = 0,
        ExplicitContextName
    };

    struct PublishedPromptScope
    {
        PromptScopeState state{ PromptScopeState::Unavailable };
        presentation::DeviceFamily family{ presentation::DeviceFamily::KeyboardMouse };
        context::UiContextId uiContextId{ context::UiContextId::None };
        actions::ActionSetStack actionSetStack;
        std::uint32_t promptScopeRevision{ 0 };
        std::uint64_t manifestEpoch{ 0 };
    };

    struct PromptQuery
    {
        std::string_view actionId;
        PromptScopeSelectorKind selectorKind{ PromptScopeSelectorKind::CurrentPublished };
        std::string_view contextName;
    };

    std::string_view ToString(PromptScopeState state);
    std::string_view ToString(PromptScopeSelectorKind kind);
}
