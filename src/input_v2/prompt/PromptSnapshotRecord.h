#pragma once

#include "input_v2/actions/CompiledActionGraph.h"
#include "input_v2/context/ContextCatalog.h"
#include "input_v2/prompt/PromptScope.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dualpad::input_v2::prompt
{
    enum class PromptQueryStatus : std::uint8_t
    {
        Ok = 0,
        ScopeUnavailable,
        UnknownAction,
        UnknownContext,
        ContextOutOfScope,
        NoVisibleBinding,
        HiddenOnly,
        DeviceFamilyMismatch
    };

    enum class PromptResolutionSource : std::uint8_t
    {
        ExactScope = 0,
        AncestorScope
    };

    enum class PromptFallbackKind : std::uint8_t
    {
        None = 0,
        AncestorScope
    };

    struct PromptCandidate
    {
        actions::BindingId bindingId{ 0 };
        std::string source;
        std::string token;
        std::string localizedLabel;
        std::string deviceProfile;
        std::string glyphId;
        std::string platformId;
        std::string buttonSemanticName;
        std::string fallbackText;
        std::string assetLookupPath;
        std::string missingIconBehavior;
        std::string debugReason;
        std::uint16_t priority{ 0 };
    };

    struct PromptDescriptor
    {
        bool ok{ false };
        PromptQueryStatus status{ PromptQueryStatus::ScopeUnavailable };
        std::optional<actions::ActionId> action;
        std::optional<actions::ActionSetId> resolvedSet;
        std::optional<context::UiContextId> resolvedContext;
        std::optional<PromptCandidate> primary;
        std::vector<PromptCandidate> alternates;
        PromptResolutionSource resolutionSource{ PromptResolutionSource::ExactScope };
        PromptFallbackKind fallback{ PromptFallbackKind::None };
        std::optional<std::string> deviceProfile;
        std::uint32_t promptScopeRevision{ 0 };
        std::uint64_t manifestEpoch{ 0 };
    };

    struct PromptSnapshotRecord
    {
        std::string actionId;
        PromptQueryStatus status{ PromptQueryStatus::ScopeUnavailable };
        std::optional<actions::ActionSetId> resolvedSet;
        std::optional<context::UiContextId> resolvedContext;
        std::optional<PromptCandidate> primary;
        std::vector<PromptCandidate> alternates;
        PromptResolutionSource resolutionSource{ PromptResolutionSource::ExactScope };
        PromptFallbackKind fallback{ PromptFallbackKind::None };
        std::optional<std::string> deviceProfile;
        std::uint32_t promptScopeRevision{ 0 };
        std::uint64_t manifestEpoch{ 0 };
    };

    PromptSnapshotRecord MakePromptSnapshotRecord(const PromptQuery& query, const PromptDescriptor& descriptor);

    std::string_view ToString(PromptQueryStatus status);
    std::string_view ToString(PromptResolutionSource source);
    std::string_view ToString(PromptFallbackKind fallback);
}
