#include "pch.h"

#include "input_v2/compat/LegacyInputContextCompat.h"
#include "input_v2/context/ContextRefreshTick.h"
#include "input_v2/context/ContextResolver.h"
#include "input_v2/menu/MenuInstanceRegistry.h"

#include <iostream>
#include <stdexcept>
#include <string_view>

namespace
{
    void Require(bool condition, std::string_view message)
    {
        if (!condition) {
            throw std::runtime_error(std::string(message));
        }
    }

    dualpad::input_v2::menu::ObservedMenuNode Node(
        std::uintptr_t ptr,
        std::string name,
        std::int32_t depth,
        std::uintptr_t delegate = 0,
        std::uintptr_t movie = 0,
        std::uint32_t order = 0)
    {
        return dualpad::input_v2::menu::ObservedMenuNode{
            .menuPtr = ptr,
            .menuName = std::move(name),
            .menuFlagsValue = 1u << 3,
            .inputContextValue = 0,
            .depthPriority = depth,
            .delegatePtr = delegate,
            .moviePtr = movie,
            .observationOrder = order
        };
    }
}

void RunContextResolverTests()
{
    namespace actions = dualpad::input_v2::actions;
    namespace ctx = dualpad::input_v2::context;
    namespace menu = dualpad::input_v2::menu;
    using dualpad::input::InputContext;

    const auto& catalog = ctx::ContextCatalog::BuiltInCatalog();

    {
        menu::MenuInstanceRegistry registry;
        auto stack = registry.ReconcileAndPublish(
            menu::ObservedMenuSnapshot{
                .completeness = menu::ObserverCompleteness::Complete,
                .nodes = { Node(0x1000, "InventoryMenu", 5) }
            },
            catalog);
        Require(stack.menuStackRevision == 1, "stable pointer first publish should advance menuStackRevision");
        const auto firstId = stack.trackedMenus.front().instanceId;

        stack = registry.ReconcileAndPublish(
            menu::ObservedMenuSnapshot{
                .completeness = menu::ObserverCompleteness::Complete,
                .nodes = { Node(0x1000, "InventoryMenu", 5) }
            },
            catalog);
        Require(stack.menuStackRevision == 1, "unchanged stable pointer should not advance menuStackRevision");
        Require(stack.trackedMenus.front().instanceId == firstId, "stable pointer must retain MenuInstanceId");
        Require(stack.trackedMenus.front().lastSeenRevision == 1, "lastSeenRevision bookkeeping is not part of published revision semantics");
    }

    {
        menu::MenuInstanceRegistry registry;
        const auto stack = registry.ReconcileAndPublish(
            menu::ObservedMenuSnapshot{
                .completeness = menu::ObserverCompleteness::Complete,
                .nodes = {
                    Node(0x2000, "InventoryMenu", 4, 0x11, 0x21, 0),
                    Node(0x3000, "InventoryMenu", 6, 0x12, 0x22, 1)
                }
            },
            catalog);
        Require(stack.trackedMenus.size() == 2, "same-name duplicate menus must coexist");
        Require(stack.trackedMenus[0].instanceId != stack.trackedMenus[1].instanceId, "same-name duplicate menus need distinct ids");
        Require(stack.trackedMenus[0].depthPriority == 6, "top menu sorting should prefer higher depthPriority");
    }

    {
        menu::MenuInstanceRegistry registry;
        auto stack = registry.ReconcileAndPublish(
            menu::ObservedMenuSnapshot{
                .completeness = menu::ObserverCompleteness::Complete,
                .nodes = {
                    Node(0x4000, "InventoryMenu", 5),
                    Node(0x5000, "JournalMenu", 6)
                }
            },
            catalog);
        Require(stack.trackedMenus.size() == 2, "setup should track two menus");

        stack = registry.ReconcileAndPublish(
            menu::ObservedMenuSnapshot{
                .completeness = menu::ObserverCompleteness::Partial,
                .nodes = { Node(0x4000, "InventoryMenu", 5) }
            },
            catalog);
        Require(stack.trackedMenus.size() == 2, "partial snapshot must not close absent menus");
        Require(stack.menuStackRevision == 2, "partial snapshot may publish observed updates only");

        stack = registry.ReconcileAndPublish(
            menu::ObservedMenuSnapshot{
                .completeness = menu::ObserverCompleteness::Complete,
                .nodes = { Node(0x4000, "InventoryMenu", 5) }
            },
            catalog);
        Require(stack.trackedMenus.size() == 1, "complete snapshot may close absent menus");
    }

    {
        menu::MenuInstanceRegistry registry;
        ctx::ContextResolver resolver;
        const auto stack = registry.ReconcileAndPublish(
            menu::ObservedMenuSnapshot{
                .completeness = menu::ObserverCompleteness::Complete,
                .nodes = { Node(0x6000, "JournalMenu", 7) }
            },
            catalog);
        const auto resolved = resolver.ResolveAndPublish(stack, ctx::GameplaySubstate::None, catalog);
        Require(resolved.uiContextId == ctx::UiContextId::Journal, "JournalMenu should resolve from compiled catalog");
        Require(resolved.legacyInputContext == InputContext::JournalMenu, "legacy mirror should come from catalog mapping");
        Require(resolved.actionSetStack.baseSetId == "MenuBase", "menu action set base should be MenuBase");
        Require(resolved.actionSetStack.layerIds == std::vector<std::string>{ "JournalLayer" }, "Journal layer should come from catalog");
        Require(resolved.actionSetStack.scopeAnchorIds == std::vector<std::string>({ "MenuBase", "JournalLayer" }), "scope anchors should come from catalog");
        Require(resolved.presentationPolicyId == "JournalMenu", "presentationPolicyId must publish with resolved UiContextId");
        Require(resolved.menuStackRevision == stack.menuStackRevision, "resolver must forward registry menuStackRevision");
        Require(resolved.contextRevision == 1, "first resolved context should publish contextRevision 1");

        const auto again = resolver.ResolveAndPublish(stack, ctx::GameplaySubstate::None, catalog);
        Require(again.contextRevision == 1, "unchanged context must not advance contextRevision");
    }

    {
        menu::MenuInstanceRegistry registry;
        ctx::ContextResolver resolver;
        auto customCatalog = catalog;
        auto* journal = const_cast<ctx::CompiledContextEntry*>(
            ctx::ContextCatalog::FindById(customCatalog, ctx::UiContextId::Journal));
        Require(journal != nullptr, "custom catalog should contain Journal entry");
        journal->presentationPolicyId = "JournalPolicyFromCatalog";

        const auto stack = registry.ReconcileAndPublish(
            menu::ObservedMenuSnapshot{
                .completeness = menu::ObserverCompleteness::Complete,
                .nodes = { Node(0x6100, "JournalMenu", 7) }
            },
            customCatalog);
        const auto resolved = resolver.ResolveAndPublish(stack, ctx::GameplaySubstate::None, customCatalog);
        Require(
            resolved.presentationPolicyId == "JournalPolicyFromCatalog",
            "presentationPolicyId must be forwarded from CompiledContextEntry, not derived from canonicalContextName");
    }

    {
        menu::MenuInstanceRegistry registry;
        ctx::ContextResolver resolver;
        const auto stack = registry.ReconcileAndPublish(
            menu::ObservedMenuSnapshot{
                .completeness = menu::ObserverCompleteness::Complete,
                .nodes = { Node(0, "FavoritesMenu", 7) }
            },
            catalog);
        const auto resolved = resolver.ResolveAndPublish(stack, ctx::GameplaySubstate::None, catalog);
        Require(resolved.uiContextId == ctx::UiContextId::UnknownTrackedMenu, "degraded identity must fail closed to UnknownTrackedMenu");
        Require(resolved.legacyInputContext == InputContext::Menu, "unknown tracked menu should mirror legacy Menu");
        Require(resolved.actionSetStack.baseSetId == "MenuBase", "unknown tracked base set should be MenuBase");
        Require(resolved.actionSetStack.layerIds == std::vector<std::string>{ "UnknownTrackedMenuLayer" }, "unknown tracked layer must not guess specific menu layer");
        Require(resolved.presentationPolicyId == "Menu", "unknown tracked presentation policy must come from catalog sentinel");
    }

    {
        const auto passthrough = actions::ActionSetResolver::Resolve(catalog, ctx::UiContextId::PassthroughOverlay);
        Require(passthrough.baseSetId.empty(), "passthrough overlay must not claim MenuBase");
        Require(passthrough.layerIds.empty(), "passthrough overlay must not inherit UnknownTrackedMenuLayer");
        Require(passthrough.scopeAnchorIds.empty(), "passthrough overlay must not create scope anchors");

        const auto unknown = actions::ActionSetResolver::Resolve(catalog, ctx::UiContextId::UnknownTrackedMenu);
        Require(unknown.baseSetId == "MenuBase", "unknown tracked menu should claim MenuBase");
        Require(
            unknown.layerIds == std::vector<std::string>{ "UnknownTrackedMenuLayer" },
            "unknown tracked menu should use UnknownTrackedMenuLayer");
    }

    {
        const auto gameplay = actions::ActionSetResolver::Resolve(catalog, ctx::UiContextId::None);
        Require(gameplay.baseSetId == "GameplayBase", "gameplay base set should come from catalog");
        Require(gameplay.layerIds.empty(), "plain gameplay should not add menu layers");

        const auto sneak = actions::ActionSetResolver::Resolve(catalog, ctx::UiContextId::Sneaking);
        Require(sneak.baseSetId == "GameplayBase", "sneaking should remain in GameplayBase");
        Require(sneak.layerIds == std::vector<std::string>{ "SneakLayer" }, "sneaking layer should come from catalog");
    }

    {
        auto& tick = ctx::ContextRefreshTick::GetSingleton();
        tick.ResetForTests();

        const auto frame = tick.BeginFrame();
        tick.MarkCombatEvent(true);
        const auto combat = tick.RefreshObservedForTests(
            frame,
            menu::ObservedMenuSnapshot{
                .completeness = menu::ObserverCompleteness::Complete,
                .nodes = {}
            },
            InputContext::Gameplay,
            catalog);
        Require(combat.uiContextId == ctx::UiContextId::Combat, "combat event fact must publish through ContextResolver");
        Require(combat.legacyInputContext == InputContext::Combat, "combat fact must mirror resolved combat context");

        const auto skipped = tick.RefreshObservedForTests(
            frame,
            menu::ObservedMenuSnapshot{
                .completeness = menu::ObserverCompleteness::Complete,
                .nodes = { Node(0x7000, "InventoryMenu", 10) }
            },
            InputContext::Gameplay,
            catalog);
        Require(skipped.uiContextId == ctx::UiContextId::Combat, "same frame must not run a second context refresh");

        tick.MarkCombatEvent(false);
        const auto gameplay = tick.RefreshObservedForTests(
            tick.BeginFrame(),
            menu::ObservedMenuSnapshot{
                .completeness = menu::ObserverCompleteness::Complete,
                .nodes = {}
            },
            InputContext::Gameplay,
            catalog);
        Require(gameplay.uiContextId == ctx::UiContextId::None, "combat clear must publish gameplay through ContextResolver");
        Require(gameplay.legacyInputContext == InputContext::Gameplay, "gameplay mirror should return via resolved context");
    }

    {
        ctx::ShadowCompareRecord expected{
            .topMenuInstanceId = 1,
            .identityQuality = menu::MenuIdentityQuality::StablePointer,
            .menuStackRevision = 2,
            .uiContextId = ctx::UiContextId::Inventory,
            .actionSetStack = actions::ActionSetStack{
                .baseSetId = "MenuBase",
                .layerIds = { "InventoryLayer" },
                .scopeAnchorIds = { "MenuBase", "InventoryLayer" }
            },
            .presentationPolicyId = "InventoryMenu",
            .legacyInputContext = InputContext::InventoryMenu,
            .legacyContextEpoch = 2,
            .contextRevision = 3
        };
        auto actual = expected;
        Require(ctx::ContextResolver::CompareShadowRecords(expected, actual).passes, "identical shadow compare records should pass");

        actual.presentationPolicyId = "Menu";
        const auto diff = ctx::ContextResolver::CompareShadowRecords(expected, actual);
        Require(!diff.passes, "presentationPolicyId diff must fail shadow compare");
        Require(diff.diffs.size() == 1 && diff.diffs.front() == "presentationPolicyId", "shadow compare must report presentationPolicyId diff");
    }
}

int main()
{
    try {
        RunContextResolverTests();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
