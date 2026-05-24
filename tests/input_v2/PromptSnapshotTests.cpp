#include "pch.h"

#include "input_v2/context/ContextCatalog.h"
#include "input_v2/presentation/PresentationProjection.h"
#include "input_v2/prompt/PromptProjection.h"
#include "input_v2/prompt/PromptService.h"

#include <iostream>
#include <stdexcept>
#include <string_view>

namespace
{
    namespace actions = dualpad::input_v2::actions;
    namespace context = dualpad::input_v2::context;
    namespace presentation = dualpad::input_v2::presentation;
    namespace prompt = dualpad::input_v2::prompt;

    void Require(bool condition, std::string_view message)
    {
        if (!condition) {
            throw std::runtime_error(std::string(message));
        }
    }

    actions::CompiledActionGraph Graph()
    {
        actions::CompiledActionGraph graph{};
        graph.manifestEpoch = 42;
        graph.actions = {
            actions::ActionDefinition{ .id = "Menu.Accept", .valueKind = actions::ActionValueKind::Digital },
            actions::ActionDefinition{ .id = "Menu.Back", .valueKind = actions::ActionValueKind::Digital },
            actions::ActionDefinition{ .id = "Menu.Hidden", .valueKind = actions::ActionValueKind::Digital },
            actions::ActionDefinition{ .id = "Menu.KbmOnly", .valueKind = actions::ActionValueKind::Digital },
            actions::ActionDefinition{ .id = "Menu.NoDisplay", .valueKind = actions::ActionValueKind::Digital }
        };

        const auto addBinding = [&](
            actions::BindingId id,
            std::string actionId,
            std::string actionSetId,
            actions::DisplayBindingMode mode,
            std::string token,
            std::uint16_t priority,
            std::string deviceProfile = "DualSense",
            bool legacyRenderable = true) {
            graph.lookups.bindingIndexById[id] = graph.bindings.size();
            graph.lookups.bindingIdsByActionId[actionId].push_back(id);
            graph.lookups.bindingIdsByActionSetId[actionSetId].push_back(id);
            graph.bindings.push_back(actions::CompiledGraphBinding{
                .bindingId = id,
                .actionId = std::move(actionId),
                .actionSetId = std::move(actionSetId),
                .primaryDisplayBindingId = id,
                .legacyOrigin = "PromptSnapshotTest"
            });
            graph.displayBindings.push_back(actions::DisplayBindingRecord{
                .bindingId = id,
                .mode = mode,
                .token = std::move(token),
                .localizedLabel = graph.bindings.back().actionId,
                .priority = priority,
                .deviceProfile = std::move(deviceProfile),
                .legacyTokenRenderable = legacyRenderable
            });
        };

        addBinding(1, "Menu.Accept", "MenuBase", actions::DisplayBindingMode::Primary, "Cross", 10);
        addBinding(2, "Menu.Accept", "JournalLayer", actions::DisplayBindingMode::Primary, "Circle", 30);
        addBinding(3, "Menu.Accept", "JournalLayer", actions::DisplayBindingMode::Primary, "Square", 30);
        addBinding(4, "Menu.Back", "MenuBase", actions::DisplayBindingMode::Primary, "Triangle", 5);
        addBinding(5, "Menu.Hidden", "JournalLayer", actions::DisplayBindingMode::Hidden, "Hidden", 5);
        addBinding(6, "Menu.KbmOnly", "JournalLayer", actions::DisplayBindingMode::Primary, "Enter", 5, "KeyboardMouse");
        graph.lookups.bindingIndexById[7] = graph.bindings.size();
        graph.lookups.bindingIdsByActionId["Menu.NoDisplay"].push_back(7);
        graph.lookups.bindingIdsByActionSetId["JournalLayer"].push_back(7);
        graph.bindings.push_back(actions::CompiledGraphBinding{
            .bindingId = 7,
            .actionId = "Menu.NoDisplay",
            .actionSetId = "JournalLayer",
            .primaryDisplayBindingId = 7,
            .legacyOrigin = "PromptSnapshotTest"
        });
        return graph;
    }

    presentation::PublishedPresentationState JournalPresentation()
    {
        presentation::PublishedPresentationState state{};
        state.family = presentation::DeviceFamily::Gamepad;
        state.uiContextId = context::UiContextId::Journal;
        state.actionSetStack = actions::ActionSetStack{
            .baseSetId = "MenuBase",
            .layerIds = { "JournalLayer" },
            .scopeAnchorIds = { "MenuBase", "JournalLayer" }
        };
        state.epoch = 7;
        return state;
    }

    prompt::PublishedPromptScope JournalScope()
    {
        prompt::PromptProjection projection;
        return projection.BuildPromptScope(JournalPresentation(), 42);
    }

    prompt::PromptDescriptor Resolve(
        const actions::CompiledActionGraph& graph,
        const prompt::PublishedPromptScope& scope,
        std::string_view actionId,
        std::string_view contextName)
    {
        prompt::PromptService service(context::ContextCatalog::BuiltInCatalog(), graph, scope);
        return service.Resolve(prompt::PromptQuery{
            .actionId = actionId,
            .selectorKind = prompt::PromptScopeSelectorKind::ExplicitContextName,
            .contextName = contextName
        });
    }

    void RunPromptProjectionTests()
    {
        prompt::PromptProjection projection;
        const auto unavailable = projection.BuildPromptScope(presentation::PublishedPresentationState{}, 42);
        Require(unavailable.state == prompt::PromptScopeState::Unavailable, "missing presentation truth must publish Unavailable");

        const auto first = projection.BuildPromptScope(JournalPresentation(), 42);
        Require(first.state == prompt::PromptScopeState::Ready, "ready presentation must publish Ready prompt scope");
        Require(first.promptScopeRevision == unavailable.promptScopeRevision + 1, "ready publish must advance promptScopeRevision");

        auto changed = JournalPresentation();
        changed.family = presentation::DeviceFamily::KeyboardMouse;
        changed.epoch = 8;
        const auto familyChanged = projection.BuildPromptScope(changed, 42);
        Require(familyChanged.promptScopeRevision == first.promptScopeRevision + 1, "family change must advance promptScopeRevision");

        const auto manifestChanged = projection.BuildPromptScope(changed, 43);
        Require(
            manifestChanged.promptScopeRevision == familyChanged.promptScopeRevision + 1,
            "manifestEpoch-only change must advance promptScopeRevision");
    }

    void RunPromptServiceSuccessTests()
    {
        const auto graph = Graph();
        const auto scope = JournalScope();
        prompt::PromptService service(context::ContextCatalog::BuiltInCatalog(), graph, scope);

        const auto exact = service.Resolve(prompt::PromptQuery{
            .actionId = "Menu.Accept",
            .selectorKind = prompt::PromptScopeSelectorKind::ExplicitContextName,
            .contextName = "JournalMenu"
        });
        Require(exact.ok, "exact context prompt query must succeed");
        Require(exact.primary && exact.primary->token == "Circle", "priority and bindingId tie-break must pick Circle");
        Require(exact.alternates.size() == 1 && exact.alternates[0].token == "Square", "alternate candidate order must be deterministic");
        Require(exact.resolutionSource == prompt::PromptResolutionSource::ExactScope, "exact scope must report ExactScope");
        Require(exact.fallback == prompt::PromptFallbackKind::None, "exact layer candidate must not report fallback");
        Require(exact.deviceProfile && *exact.deviceProfile == exact.primary->deviceProfile, "top-level deviceProfile must mirror primary");

        const auto ancestor = service.Resolve(prompt::PromptQuery{
            .actionId = "Menu.Accept",
            .selectorKind = prompt::PromptScopeSelectorKind::ExplicitContextName,
            .contextName = "Menu"
        });
        Require(ancestor.ok, "generic Menu ancestor scope must succeed without runtime Menu retry");
        Require(ancestor.primary && ancestor.primary->token == "Cross", "generic Menu query must resolve MenuBase token");
        Require(ancestor.resolutionSource == prompt::PromptResolutionSource::AncestorScope, "generic Menu query must report AncestorScope");

        const auto fallback = service.Resolve(prompt::PromptQuery{
            .actionId = "Menu.Back",
            .selectorKind = prompt::PromptScopeSelectorKind::ExplicitContextName,
            .contextName = "JournalMenu"
        });
        Require(fallback.ok, "specific scope may fall back along its own scopeAnchorIds chain");
        Require(fallback.primary && fallback.primary->token == "Triangle", "ancestor fallback must use MenuBase binding");
        Require(fallback.fallback == prompt::PromptFallbackKind::AncestorScope, "ancestor candidate hit must report fallback");

        const auto snapshot = service.Snapshot(prompt::PromptQuery{
            .actionId = "Menu.Accept",
            .selectorKind = prompt::PromptScopeSelectorKind::ExplicitContextName,
            .contextName = "JournalMenu"
        });
        Require(snapshot.status == prompt::PromptQueryStatus::Ok, "snapshot success is derived from status");
        Require(snapshot.promptScopeRevision == exact.promptScopeRevision, "snapshot must carry promptScopeRevision");
        Require(snapshot.manifestEpoch == 42, "snapshot must carry manifestEpoch");

        Require(
            service.ResolveLegacyGlyphToken("Menu.Accept", "JournalMenu") == "Circle",
            "legacy token API wrapper must return primary token on success");
        const auto legacy = service.ResolveLegacyGlyph("Menu.Accept", "JournalMenu");
        Require(legacy.ok && legacy.buttonArtToken == "Circle", "legacy descriptor wrapper must preserve ok/buttonArtToken shape");
        Require(!legacy.failureReason.empty(), "legacy descriptor may include diagnostic failureReason/status field");
    }

    void RunPromptServiceFailClosedTests()
    {
        const auto graph = Graph();
        const auto scope = JournalScope();

        auto missingGraph = graph;
        missingGraph.manifestEpoch = 0;
        Require(
            Resolve(missingGraph, scope, "Menu.Accept", "JournalMenu").status == prompt::PromptQueryStatus::ScopeUnavailable,
            "missing graph must fail closed as ScopeUnavailable");

        Require(
            Resolve(graph, scope, "Menu.Missing", "JournalMenu").status == prompt::PromptQueryStatus::UnknownAction,
            "unknown action must fail closed as UnknownAction");
        Require(
            Resolve(graph, scope, "Menu.Accept", "NotAContext").status == prompt::PromptQueryStatus::UnknownContext,
            "invalid context must fail closed as UnknownContext");
        Require(
            Resolve(graph, scope, "Menu.Accept", "InventoryMenu").status == prompt::PromptQueryStatus::ContextOutOfScope,
            "valid inactive context must fail closed as ContextOutOfScope");
        Require(
            Resolve(graph, scope, "Menu.KbmOnly", "JournalMenu").status == prompt::PromptQueryStatus::DeviceFamilyMismatch,
            "device mismatch must have priority over hidden/no-visible states");
        Require(
            Resolve(graph, scope, "Menu.Hidden", "JournalMenu").status == prompt::PromptQueryStatus::HiddenOnly,
            "family-compatible hidden-only binding must return HiddenOnly");
        Require(
            Resolve(graph, scope, "Menu.NoDisplay", "JournalMenu").status == prompt::PromptQueryStatus::NoVisibleBinding,
            "binding without display binding must return NoVisibleBinding");

        prompt::PromptService service(context::ContextCatalog::BuiltInCatalog(), graph, scope);
        Require(
            service.ResolveLegacyGlyphToken("Menu.Missing", "JournalMenu").empty(),
            "legacy token API wrapper must return empty string on failure");
        const auto legacyFailure = service.ResolveLegacyGlyph("Menu.Accept", "InventoryMenu");
        Require(!legacyFailure.ok, "legacy descriptor wrapper must preserve ok=false on failure");
        Require(legacyFailure.buttonArtToken.empty(), "legacy descriptor wrapper must return empty buttonArtToken on failure");
        Require(legacyFailure.failureReason == "ContextOutOfScope", "legacy descriptor wrapper must expose failureReason=status");
    }
}

int main()
{
    try {
        RunPromptProjectionTests();
        RunPromptServiceSuccessTests();
        RunPromptServiceFailClosedTests();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
