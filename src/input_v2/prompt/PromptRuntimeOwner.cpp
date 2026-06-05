#include "pch.h"

#include "input_v2/prompt/PromptRuntimeOwner.h"

#include "input_v2/actions/CompiledActionGraphPublisher.h"
#include "input_v2/config/AtomicConfigReloader.h"

#include <utility>

namespace dualpad::input_v2::prompt
{
    namespace
    {
        PromptDescriptor ScopeUnavailableDescriptor(
            const PromptQuery& query,
            const PublishedPromptScope& scope)
        {
            return PromptDescriptor{
                .ok = false,
                .status = PromptQueryStatus::ScopeUnavailable,
                .action = actions::ActionId(query.actionId),
                .promptScopeRevision = scope.promptScopeRevision,
                .manifestEpoch = scope.manifestEpoch
            };
        }
    }

    PromptRuntimeOwner& PromptRuntimeOwner::GetSingleton()
    {
        static PromptRuntimeOwner owner;
        return owner;
    }

    void PromptRuntimeOwner::PublishPresentationState(
        const presentation::PublishedPresentationState& presentation)
    {
        const auto bundle = config::AtomicConfigReloader::GetSingleton().GetActiveBundleSnapshot();
        const auto graphSnapshot = actions::CompiledActionGraphPublisher::GetRuntimeOwner().GetActiveSnapshot();
        PublishPresentationState(
            presentation,
            PromptRuntimeBaseline{
                .manifestEpoch = graphSnapshot.manifestEpoch,
                .configGeneration = bundle ? bundle->manifestEpoch : 0,
                .bundle = bundle,
                .graph = graphSnapshot
            });
    }

    void PromptRuntimeOwner::PublishPresentationState(
        const presentation::PublishedPresentationState& presentation,
        PromptRuntimeBaseline baseline)
    {
        std::scoped_lock lock(_mutex);
        _lastPresentation = presentation;
        _baseline = std::move(baseline);
        (void)RefreshScopeForManifestEpochLocked(_baseline->manifestEpoch);
    }

    PublishedPromptScope PromptRuntimeOwner::RefreshScopeForManifestEpochLocked(std::uint64_t manifestEpoch)
    {
        if (!_lastPresentation) {
            return _projection.GetPublishedPromptScope();
        }
        return _projection.BuildPromptScope(*_lastPresentation, manifestEpoch);
    }

    PromptDescriptor PromptRuntimeOwner::Resolve(const PromptQuery& query)
    {
        PromptRuntimeBaseline baseline{};
        PublishedPromptScope scope{};
        {
            std::scoped_lock lock(_mutex);
            if (!_baseline) {
                scope = _projection.GetPublishedPromptScope();
                return ScopeUnavailableDescriptor(query, scope);
            }
            baseline = *_baseline;
            scope = RefreshScopeForManifestEpochLocked(baseline.manifestEpoch);
        }

        const auto graph = baseline.graph.graph;
        if (!baseline.bundle || !graph ||
            baseline.bundle->manifestEpoch != baseline.manifestEpoch ||
            baseline.configGeneration != baseline.bundle->manifestEpoch ||
            baseline.graph.manifestEpoch != baseline.manifestEpoch ||
            graph->manifestEpoch != baseline.manifestEpoch ||
            scope.manifestEpoch != baseline.manifestEpoch) {
            return ScopeUnavailableDescriptor(query, scope);
        }

        PromptService service(baseline.bundle->catalog, *graph, scope);
        return service.Resolve(query);
    }

    PromptSnapshotRecord PromptRuntimeOwner::Snapshot(const PromptQuery& query)
    {
        return MakePromptSnapshotRecord(query, Resolve(query));
    }

    std::string PromptRuntimeOwner::ResolveLegacyGlyphToken(
        std::string_view actionId,
        std::string_view contextName)
    {
        const auto descriptor = Resolve(PromptQuery{
            .actionId = actionId,
            .selectorKind = PromptScopeSelectorKind::ExplicitContextName,
            .contextName = contextName
        });
        if (descriptor.ok && descriptor.primary) {
            return descriptor.primary->token;
        }
        return {};
    }

    PromptLegacyGlyphDescriptor PromptRuntimeOwner::ResolveLegacyGlyph(
        std::string_view actionId,
        std::string_view contextName)
    {
        const PromptQuery query{
            .actionId = actionId,
            .selectorKind = PromptScopeSelectorKind::ExplicitContextName,
            .contextName = contextName
        };
        return MakePromptLegacyGlyphDescriptor(query, Resolve(query));
    }

    PublishedPromptScope PromptRuntimeOwner::GetPublishedPromptScopeForTests()
    {
        std::scoped_lock lock(_mutex);
        return _projection.GetPublishedPromptScope();
    }

    void PromptRuntimeOwner::ResetForTests()
    {
        std::scoped_lock lock(_mutex);
        _lastPresentation.reset();
        _baseline.reset();
        _projection.ResetForTests();
    }
}
