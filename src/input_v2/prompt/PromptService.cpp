#include "pch.h"

#include "input_v2/prompt/PromptService.h"

#include <algorithm>
#include <sstream>

namespace dualpad::input_v2::prompt
{
    namespace
    {
        bool IsKnownAction(const actions::CompiledActionGraph& graph, std::string_view actionId)
        {
            return std::find_if(graph.actions.begin(), graph.actions.end(), [&](const actions::ActionDefinition& action) {
                return action.id == actionId;
            }) != graph.actions.end();
        }

        bool IsPrefixOf(const std::vector<std::string>& requested, const std::vector<std::string>& current)
        {
            if (requested.size() > current.size()) {
                return false;
            }
            for (std::size_t i = 0; i < requested.size(); ++i) {
                if (requested[i] != current[i]) {
                    return false;
                }
            }
            return true;
        }

        bool IsFamilyCompatible(presentation::DeviceFamily family, std::string_view profile)
        {
            if (family == presentation::DeviceFamily::Gamepad) {
                return profile == "DualSense" || profile == "Gamepad" || profile == "XInput";
            }
            return profile == "KeyboardMouse" || profile == "Keyboard" || profile == "Mouse" || profile == "KBM";
        }

        const actions::DisplayBindingRecord* FindDisplayBinding(
            const actions::CompiledActionGraph& graph,
            actions::BindingId bindingId)
        {
            const auto it = std::find_if(
                graph.displayBindings.begin(),
                graph.displayBindings.end(),
                [&](const actions::DisplayBindingRecord& display) {
                    return display.bindingId == bindingId;
                });
            return it == graph.displayBindings.end() ? nullptr : &*it;
        }

        std::string ContextIdString(context::UiContextId id)
        {
            return std::to_string(static_cast<std::uint16_t>(id));
        }

        PromptDescriptor Failure(
            PromptQueryStatus status,
            const PublishedPromptScope& scope,
            std::optional<actions::ActionId> action = std::nullopt,
            std::optional<context::UiContextId> context = std::nullopt,
            PromptResolutionSource source = PromptResolutionSource::ExactScope)
        {
            return PromptDescriptor{
                .ok = false,
                .status = status,
                .action = std::move(action),
                .resolvedContext = context,
                .resolutionSource = source,
                .promptScopeRevision = scope.promptScopeRevision,
                .manifestEpoch = scope.manifestEpoch
            };
        }

        std::string CandidateSource(const actions::CompiledGraphBinding& binding)
        {
            std::ostringstream out;
            out << binding.actionSetId << ':' << binding.bindingId << ':' << binding.legacyOrigin;
            return out.str();
        }

        std::string GlyphAssetLookupPath(std::string_view platformId, std::string_view glyphId)
        {
            std::ostringstream out;
            out << "Interface/Exported/DualPad/Glyphs/" << platformId << '/' << glyphId << ".svg";
            return out.str();
        }

        PromptCandidate MakePromptCandidate(
            const actions::CompiledGraphBinding& binding,
            const actions::DisplayBindingRecord& display)
        {
            const auto glyphId = display.token;
            const auto platformId = display.deviceProfile;
            const auto fallbackText = display.localizedLabel.empty() ? display.token : display.localizedLabel;
            return PromptCandidate{
                .bindingId = display.bindingId,
                .source = CandidateSource(binding),
                .token = display.token,
                .localizedLabel = display.localizedLabel,
                .deviceProfile = display.deviceProfile,
                .glyphId = glyphId,
                .platformId = platformId,
                .buttonSemanticName = fallbackText,
                .fallbackText = fallbackText,
                .assetLookupPath = GlyphAssetLookupPath(platformId, glyphId),
                .missingIconBehavior = "fallback_text",
                .debugReason = "Ok",
                .priority = display.priority
            };
        }
    }

    PromptService::PromptService(
        const context::CompiledContextCatalog& catalog,
        const actions::CompiledActionGraph& graph,
        const PublishedPromptScope& scope) :
        _catalog(catalog),
        _graph(graph),
        _scope(scope)
    {}

    PromptLegacyGlyphDescriptor MakePromptLegacyGlyphDescriptor(
        const PromptQuery& query,
        const PromptDescriptor& descriptor)
    {
        PromptLegacyGlyphDescriptor legacy{};
        legacy.ok = descriptor.ok;
        legacy.semanticId = std::string(query.actionId);
        legacy.contextName = std::string(query.contextName);
        legacy.failureReason = std::string(ToString(descriptor.status));
        legacy.resolutionSource = std::string(ToString(descriptor.resolutionSource));
        legacy.fallback = std::string(ToString(descriptor.fallback));
        legacy.manifestEpoch = descriptor.manifestEpoch;
        legacy.promptScopeRevision = descriptor.promptScopeRevision;
        if (descriptor.primary) {
            legacy.buttonArtToken = descriptor.primary->token;
            legacy.glyphId = descriptor.primary->glyphId;
            legacy.platformId = descriptor.primary->platformId;
            legacy.buttonSemanticName = descriptor.primary->buttonSemanticName;
            legacy.fallbackText = descriptor.primary->fallbackText;
            legacy.assetLookupPath = descriptor.primary->assetLookupPath;
            legacy.missingIconBehavior = descriptor.primary->missingIconBehavior;
            legacy.debugReason = descriptor.primary->debugReason;
        }
        if (descriptor.resolvedContext) {
            legacy.resolvedContextId = ContextIdString(*descriptor.resolvedContext);
        }
        if (descriptor.resolvedSet) {
            legacy.resolvedActionSetId = *descriptor.resolvedSet;
        }
        if (descriptor.deviceProfile) {
            legacy.deviceProfile = *descriptor.deviceProfile;
        }
        if (!descriptor.ok) {
            legacy.buttonArtToken.clear();
            legacy.glyphId.clear();
            legacy.platformId.clear();
            legacy.buttonSemanticName.clear();
            legacy.fallbackText.clear();
            legacy.assetLookupPath.clear();
            legacy.missingIconBehavior = "fail_closed_empty_token";
            legacy.debugReason = std::string(ToString(descriptor.status));
        }
        return legacy;
    }

    PromptDescriptor PromptService::Resolve(const PromptQuery& query) const
    {
        if (_scope.state != PromptScopeState::Ready || !_graph.manifestEpoch || _graph.manifestEpoch != _scope.manifestEpoch) {
            return Failure(PromptQueryStatus::ScopeUnavailable, _scope);
        }

        if (!IsKnownAction(_graph, query.actionId)) {
            return Failure(PromptQueryStatus::UnknownAction, _scope);
        }

        std::vector<std::string> requestedScopeAnchorIds;
        PromptResolutionSource resolutionSource{ PromptResolutionSource::ExactScope };
        context::UiContextId resolvedContext = _scope.uiContextId;

        if (query.selectorKind == PromptScopeSelectorKind::CurrentPublished) {
            requestedScopeAnchorIds = _scope.actionSetStack.scopeAnchorIds;
        } else {
            const auto resolved = context::ContextCatalog::ResolveAlias(_catalog, query.contextName);
            if (!resolved) {
                return Failure(
                    PromptQueryStatus::UnknownContext,
                    _scope,
                    actions::ActionId(query.actionId));
            }

            resolvedContext = *resolved;
            const auto* entry = context::ContextCatalog::FindById(_catalog, resolvedContext);
            if (!entry) {
                return Failure(
                    PromptQueryStatus::UnknownContext,
                    _scope,
                    actions::ActionId(query.actionId));
            }

            if (query.contextName == "Menu") {
                requestedScopeAnchorIds = { "MenuBase" };
            } else {
                requestedScopeAnchorIds = entry->scopeAnchorIds;
            }

            if (requestedScopeAnchorIds.empty()) {
                return Failure(
                    PromptQueryStatus::ContextOutOfScope,
                    _scope,
                    actions::ActionId(query.actionId),
                    resolvedContext);
            }

            if (requestedScopeAnchorIds == _scope.actionSetStack.scopeAnchorIds) {
                resolutionSource = PromptResolutionSource::ExactScope;
            } else if (
                requestedScopeAnchorIds.size() < _scope.actionSetStack.scopeAnchorIds.size() &&
                IsPrefixOf(requestedScopeAnchorIds, _scope.actionSetStack.scopeAnchorIds)) {
                resolutionSource = PromptResolutionSource::AncestorScope;
            } else {
                return Failure(
                    PromptQueryStatus::ContextOutOfScope,
                    _scope,
                    actions::ActionId(query.actionId),
                    resolvedContext);
            }
        }

        bool sawDisplayBinding = false;
        bool sawFamilyCompatible = false;
        bool sawHiddenOnly = false;
        std::vector<PromptCandidate> candidates;
        std::optional<actions::ActionSetId> matchedSet;

        for (auto anchorIt = requestedScopeAnchorIds.rbegin(); anchorIt != requestedScopeAnchorIds.rend(); ++anchorIt) {
            bool sawDisplayAtAnchor = false;
            bool sawFamilyAtAnchor = false;
            bool sawHiddenAtAnchor = false;
            std::vector<PromptCandidate> anchorCandidates;

            const auto bindingIt = _graph.lookups.bindingIdsByActionSetId.find(*anchorIt);
            if (bindingIt == _graph.lookups.bindingIdsByActionSetId.end()) {
                continue;
            }

            for (const auto bindingId : bindingIt->second) {
                const auto* binding = _graph.FindBinding(bindingId);
                if (!binding || binding->actionId != query.actionId) {
                    continue;
                }

                const auto* display = FindDisplayBinding(_graph, bindingId);
                if (!display) {
                    continue;
                }

                sawDisplayBinding = true;
                sawDisplayAtAnchor = true;

                if (!IsFamilyCompatible(_scope.family, display->deviceProfile)) {
                    continue;
                }
                sawFamilyCompatible = true;
                sawFamilyAtAnchor = true;

                if (display->mode == actions::DisplayBindingMode::Hidden || !display->legacyTokenRenderable) {
                    sawHiddenOnly = true;
                    sawHiddenAtAnchor = true;
                    continue;
                }

                anchorCandidates.push_back(MakePromptCandidate(*binding, *display));
            }

            if (!anchorCandidates.empty()) {
                matchedSet = *anchorIt;
                candidates = std::move(anchorCandidates);
                break;
            }

            if (sawDisplayAtAnchor && !sawFamilyAtAnchor) {
                continue;
            }
            if (sawFamilyAtAnchor && sawHiddenAtAnchor) {
                continue;
            }
        }

        if (candidates.empty()) {
            if (sawDisplayBinding && !sawFamilyCompatible) {
                return Failure(
                    PromptQueryStatus::DeviceFamilyMismatch,
                    _scope,
                    actions::ActionId(query.actionId),
                    resolvedContext,
                    resolutionSource);
            }
            if (sawFamilyCompatible && sawHiddenOnly) {
                return Failure(
                    PromptQueryStatus::HiddenOnly,
                    _scope,
                    actions::ActionId(query.actionId),
                    resolvedContext,
                    resolutionSource);
            }
            return Failure(
                PromptQueryStatus::NoVisibleBinding,
                _scope,
                actions::ActionId(query.actionId),
                resolvedContext,
                resolutionSource);
        }

        (std::sort)(candidates.begin(), candidates.end(), [](const PromptCandidate& lhs, const PromptCandidate& rhs) {
            if (lhs.priority != rhs.priority) {
                return lhs.priority > rhs.priority;
            }
            return lhs.bindingId < rhs.bindingId;
        });

        PromptDescriptor descriptor{};
        descriptor.ok = true;
        descriptor.status = PromptQueryStatus::Ok;
        descriptor.action = actions::ActionId(query.actionId);
        descriptor.resolvedSet = matchedSet;
        descriptor.resolvedContext = resolvedContext;
        descriptor.primary = candidates.front();
        descriptor.alternates.assign(candidates.begin() + 1, candidates.end());
        descriptor.resolutionSource = resolutionSource;
        descriptor.fallback =
            matchedSet.has_value() && *matchedSet != requestedScopeAnchorIds.back()
                ? PromptFallbackKind::AncestorScope
                : PromptFallbackKind::None;
        descriptor.deviceProfile = descriptor.primary->deviceProfile;
        descriptor.promptScopeRevision = _scope.promptScopeRevision;
        descriptor.manifestEpoch = _scope.manifestEpoch;
        return descriptor;
    }

    PromptSnapshotRecord PromptService::Snapshot(const PromptQuery& query) const
    {
        return MakePromptSnapshotRecord(query, Resolve(query));
    }

    std::string PromptService::ResolveLegacyGlyphToken(std::string_view actionId, std::string_view contextName) const
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

    PromptLegacyGlyphDescriptor PromptService::ResolveLegacyGlyph(
        std::string_view actionId,
        std::string_view contextName) const
    {
        const PromptQuery query{
            .actionId = actionId,
            .selectorKind = PromptScopeSelectorKind::ExplicitContextName,
            .contextName = contextName
        };
        return MakePromptLegacyGlyphDescriptor(query, Resolve(query));
    }
}
