#include "pch.h"

#include "input/glyph/GlyphResolutionCompat.h"

#include "input_v2/prompt/PromptRuntimeOwner.h"

namespace dualpad::input::glyph
{
    namespace
    {
        GlyphResolutionStatus ToGlyphStatus(input_v2::prompt::PromptQueryStatus status)
        {
            using input_v2::prompt::PromptQueryStatus;
            switch (status) {
            case PromptQueryStatus::Ok:
                return GlyphResolutionStatus::Resolved;
            case PromptQueryStatus::HiddenOnly:
            case PromptQueryStatus::DeviceFamilyMismatch:
                return GlyphResolutionStatus::UnsupportedTriggerForToken;
            case PromptQueryStatus::ScopeUnavailable:
            case PromptQueryStatus::UnknownAction:
            case PromptQueryStatus::UnknownContext:
            case PromptQueryStatus::ContextOutOfScope:
            case PromptQueryStatus::NoVisibleBinding:
            default:
                return GlyphResolutionStatus::NoBinding;
            }
        }
    }

    GlyphResolutionCompatResult ResolveActionGlyphCompat(
        std::string_view actionId,
        std::string_view contextName)
    {
        const input_v2::prompt::PromptQuery query{
            .actionId = actionId,
            .selectorKind = input_v2::prompt::PromptScopeSelectorKind::ExplicitContextName,
            .contextName = contextName
        };
        const auto descriptor = input_v2::prompt::PromptRuntimeOwner::GetSingleton().Resolve(query);

        GlyphResolutionCompatResult result{};
        result.ok = descriptor.ok;
        result.semanticId = std::string(actionId);
        result.requestedContextName = std::string(contextName);
        result.resolvedContextName = std::string(contextName);
        result.status = ToGlyphStatus(descriptor.status);
        result.fallbackKind = GlyphFallbackKind::None;
        result.fallbackReason = GlyphFallbackReason::None;

        if (descriptor.primary) {
            result.token = descriptor.primary->token;
            result.candidateCount = descriptor.alternates.size() + 1;
        }
        result.reverseLookupAmbiguous = descriptor.alternates.size() > 0;
        return result;
    }

    const char* ToString(GlyphResolutionStatus status)
    {
        switch (status) {
        case GlyphResolutionStatus::Resolved:
            return "resolved";
        case GlyphResolutionStatus::NoBinding:
            return "no_binding";
        case GlyphResolutionStatus::UnsupportedTriggerForToken:
        default:
            return "unsupported_trigger_for_token";
        }
    }

    const char* ToString(GlyphFallbackKind fallbackKind)
    {
        switch (fallbackKind) {
        case GlyphFallbackKind::None:
            return "none";
        case GlyphFallbackKind::ContextParseFallbackToMenu:
            return "context_parse_fallback_to_menu";
        case GlyphFallbackKind::ContextRetryToMenu:
        default:
            return "context_retry_to_menu";
        }
    }

    const char* ToString(GlyphFallbackReason fallbackReason)
    {
        switch (fallbackReason) {
        case GlyphFallbackReason::None:
            return "none";
        case GlyphFallbackReason::ContextParseFailure:
            return "context_parse_failure";
        case GlyphFallbackReason::NoBinding:
            return "no_binding";
        case GlyphFallbackReason::UnsupportedTriggerForToken:
        default:
            return "unsupported_trigger_for_token";
        }
    }
}
