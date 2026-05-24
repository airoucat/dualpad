#include "pch.h"

#include "input_v2/prompt/PromptRuntimeOwner.h"

#include "input_v2/actions/CompiledActionGraphPublisher.h"
#include "input_v2/config/AtomicConfigReloader.h"

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
        std::scoped_lock lock(_mutex);
        _lastPresentation = presentation;
        (void)RefreshScopeFromActiveManifestLocked();
    }

    std::uint64_t PromptRuntimeOwner::ActiveManifestEpoch() const
    {
        const auto graph = actions::CompiledActionGraphPublisher::GetRuntimeOwner().GetActiveGraph();
        if (!graph) {
            return 0;
        }
        return graph->manifestEpoch;
    }

    PublishedPromptScope PromptRuntimeOwner::RefreshScopeFromActiveManifestLocked()
    {
        if (!_lastPresentation) {
            return _projection.GetPublishedPromptScope();
        }
        return _projection.BuildPromptScope(*_lastPresentation, ActiveManifestEpoch());
    }

    PromptDescriptor PromptRuntimeOwner::Resolve(const PromptQuery& query)
    {
        const auto bundle = config::AtomicConfigReloader::GetSingleton().GetActiveBundleSnapshot();
        const auto graph = actions::CompiledActionGraphPublisher::GetRuntimeOwner().GetActiveGraph();

        PublishedPromptScope scope{};
        {
            std::scoped_lock lock(_mutex);
            scope = RefreshScopeFromActiveManifestLocked();
        }

        if (!bundle || !graph) {
            return ScopeUnavailableDescriptor(query, scope);
        }

        PromptService service(bundle->catalog, *graph, scope);
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
        return RefreshScopeFromActiveManifestLocked();
    }

    void PromptRuntimeOwner::ResetForTests()
    {
        std::scoped_lock lock(_mutex);
        _lastPresentation.reset();
        _projection.ResetForTests();
    }
}
