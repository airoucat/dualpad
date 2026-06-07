#pragma once

#include "input_v2/actions/CompiledActionGraph.h"
#include "input_v2/context/ContextCatalog.h"
#include "input_v2/prompt/PromptSnapshotRecord.h"

namespace dualpad::input_v2::prompt
{
    struct PromptLegacyGlyphDescriptor
    {
        bool ok{ false };
        std::string buttonArtToken;
        std::string semanticId;
        std::string contextName;
        std::string failureReason;
        std::string resolvedContextId;
        std::string resolvedActionSetId;
        std::string resolutionSource;
        std::string fallback;
        std::string deviceProfile;
        std::string glyphId;
        std::string platformId;
        std::string buttonSemanticName;
        std::string fallbackText;
        std::string assetLookupPath;
        std::string missingIconBehavior;
        std::string debugReason;
        std::uint64_t manifestEpoch{ 0 };
        std::uint32_t promptScopeRevision{ 0 };
    };

    [[nodiscard]] PromptLegacyGlyphDescriptor MakePromptLegacyGlyphDescriptor(
        const PromptQuery& query,
        const PromptDescriptor& descriptor);

    class PromptService
    {
    public:
        PromptService(
            const context::CompiledContextCatalog& catalog,
            const actions::CompiledActionGraph& graph,
            const PublishedPromptScope& scope);

        [[nodiscard]] PromptDescriptor Resolve(const PromptQuery& query) const;
        [[nodiscard]] PromptSnapshotRecord Snapshot(const PromptQuery& query) const;

        [[nodiscard]] std::string ResolveLegacyGlyphToken(std::string_view actionId, std::string_view contextName) const;
        [[nodiscard]] PromptLegacyGlyphDescriptor ResolveLegacyGlyph(
            std::string_view actionId,
            std::string_view contextName) const;

    private:
        const context::CompiledContextCatalog& _catalog;
        const actions::CompiledActionGraph& _graph;
        const PublishedPromptScope& _scope;
    };
}
