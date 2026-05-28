#pragma once

#include "input_v2/presentation/PresentationProjection.h"
#include "input_v2/prompt/PromptProjection.h"
#include "input_v2/prompt/PromptService.h"

#include <mutex>
#include <optional>

namespace dualpad::input_v2::prompt
{
    class PromptRuntimeOwner
    {
    public:
        static PromptRuntimeOwner& GetSingleton();

        void PublishPresentationState(const presentation::PublishedPresentationState& presentation);

        [[nodiscard]] PromptDescriptor Resolve(const PromptQuery& query);
        [[nodiscard]] PromptSnapshotRecord Snapshot(const PromptQuery& query);
        [[nodiscard]] std::string ResolveLegacyGlyphToken(
            std::string_view actionId,
            std::string_view contextName);
        [[nodiscard]] PromptLegacyGlyphDescriptor ResolveLegacyGlyph(
            std::string_view actionId,
            std::string_view contextName);

        [[nodiscard]] PublishedPromptScope GetPublishedPromptScopeForTests();
        void ResetForTests();

    private:
        PromptRuntimeOwner() = default;

        [[nodiscard]] std::uint64_t ActiveManifestEpoch() const;
        [[nodiscard]] PublishedPromptScope RefreshScopeFromActiveManifestLocked();

        mutable std::mutex _mutex;
        PromptProjection _projection;
        std::optional<presentation::PublishedPresentationState> _lastPresentation;
    };
}
