#pragma once

#include "input/InputContext.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace dualpad::input_v2::config
{
    struct LegacyMenuPolicyAst;
}

namespace dualpad::input_v2::context
{
    // Canonical context identifier for Phase 1+. This is intentionally decoupled from legacy InputContext.
    enum class UiContextId : std::uint16_t
    {
        None = 0,

        // Menu contexts.
        UnknownTrackedMenu,
        Inventory,
        Magic,
        Map,
        Journal,
        Dialogue,
        Favorites,
        Tween,
        Container,
        Barter,
        Training,
        LevelUp,
        RaceSex,
        StatsMenu,
        SkillMenu,
        BookMenu,
        MessageBox,
        Quantity,
        Gift,
        Creations,
        Console,
        ItemMenu,
        DebugText,
        MapMenuContext,
        Stats,
        Cursor,
        Book,
        DebugOverlay,
        TFCMode,
        DebugMapMenu,
        Lockpicking,
        Favor,

        // Gameplay substates (represented as layers but still assigned stable IDs for catalog queries).
        Combat,
        Sneaking,
        Riding,
        Werewolf,
        VampireLord,
        Death,
        Bleedout,
        Ragdoll,
        KillMove,

        // Non-tracked overlays / passthrough.
        PassthroughOverlay,
    };

    struct UiContextIdHash
    {
        std::size_t operator()(UiContextId id) const noexcept
        {
            return std::hash<std::uint16_t>{}(static_cast<std::uint16_t>(id));
        }
    };

    enum class UnknownMenuPolicy : std::uint8_t
    {
        Passthrough = 0,
        Track
    };

    struct MenuPolicyMetadata
    {
        UnknownMenuPolicy unknownMenuPolicy{ UnknownMenuPolicy::Passthrough };
        bool logUnknownMenuProbe{ true };
        bool logUnknownMenuDecision{ true };

        // menuName -> UiContextId
        std::unordered_map<std::string, UiContextId> trackRules;
        std::unordered_set<std::string> ignoreRules;
    };

    struct CompiledContextEntry
    {
        UiContextId uiContextId{ UiContextId::None };
        std::string canonicalContextName;
        std::optional<dualpad::input::InputContext> legacyInputContext;

        // Base-set anchor and default layer stack for this context.
        std::optional<std::string> defaultActionSetId;
        std::vector<std::string> defaultLayerIds;
        std::vector<std::string> scopeAnchorIds;

        std::vector<std::string> aliases;
        std::vector<std::string> menuNames;
    };

    struct CompiledContextCatalog
    {
        std::uint64_t manifestEpoch{ 0 };

        MenuPolicyMetadata menuPolicy{};
        std::vector<CompiledContextEntry> entries;

        // Indices for compatibility facades.
        std::unordered_map<std::string, UiContextId> aliasIndex;
        std::unordered_map<std::string, UiContextId> menuNameIndex;
        std::unordered_map<UiContextId, std::size_t, UiContextIdHash> entryIndexById;
    };

    struct CatalogCompileResult
    {
        bool ok{ false };
        std::string message;
        CompiledContextCatalog catalog;
    };

    class ContextCatalog
    {
    public:
        // Returns a built-in catalog compiled from the Phase 1 seed and an empty menu policy.
        // This exists so compatibility facades can resolve names even before AtomicConfigReloader is initialized.
        static const CompiledContextCatalog& BuiltInCatalog();

        static CatalogCompileResult Compile(
            const dualpad::input_v2::config::LegacyMenuPolicyAst& importedPolicy,
            std::uint64_t manifestEpoch);

        // Query helpers.
        static const CompiledContextEntry* FindById(const CompiledContextCatalog& catalog, UiContextId id);
        static std::optional<UiContextId> ResolveAlias(const CompiledContextCatalog& catalog, std::string_view name);
        static std::optional<UiContextId> ResolveMenuName(const CompiledContextCatalog& catalog, std::string_view menuName);
        static std::optional<dualpad::input::InputContext> ToLegacyInputContext(const CompiledContextCatalog& catalog, UiContextId id);
    };
}
