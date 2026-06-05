#pragma once

#include "input_v2/actions/CompiledActionGraphPublisher.h"
#include "input_v2/presentation/PresentationProjection.h"
#include "input_v2/prompt/PromptProjection.h"
#include "input_v2/prompt/PromptService.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

namespace dualpad::input_v2::config
{
    struct CompiledConfigBundle;
}

namespace dualpad::input_v2::prompt
{
    struct PromptRuntimeBaseline
    {
        std::uint64_t manifestEpoch{ 0 };
        std::uint64_t configGeneration{ 0 };
        std::shared_ptr<const config::CompiledConfigBundle> bundle;
        actions::PublishedActionGraphSnapshot graph;
    };

    class PromptRuntimeOwner
    {
    public:
        static PromptRuntimeOwner& GetSingleton();

        void PublishPresentationState(const presentation::PublishedPresentationState& presentation);
        void PublishPresentationState(
            const presentation::PublishedPresentationState& presentation,
            PromptRuntimeBaseline baseline);

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

        [[nodiscard]] PublishedPromptScope RefreshScopeForManifestEpochLocked(std::uint64_t manifestEpoch);

        mutable std::mutex _mutex;
        PromptProjection _projection;
        std::optional<presentation::PublishedPresentationState> _lastPresentation;
        std::optional<PromptRuntimeBaseline> _baseline;
    };
}
