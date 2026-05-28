#include "pch.h"

#include "input_v2/context/ContextCatalog.h"

#include "input/IniParseHelpers.h"
#include "input_v2/config/LegacyIniImporter.h"

#include <format>

namespace dualpad::input_v2::context
{
    namespace
    {
        using Legacy = dualpad::input::InputContext;

        struct ContextSeed
        {
            UiContextId id;
            const char* canonicalName;
            std::optional<Legacy> legacyInputContext;
            std::optional<const char*> defaultActionSetId;
            std::vector<std::string> defaultLayerIds;
            std::vector<std::string> scopeAnchorIds;
            std::vector<std::string> aliases;
            std::vector<std::string> menuNames;
            std::optional<const char*> presentationPolicyId;
        };

        std::vector<ContextSeed> BuildSeed()
        {
            // Base set ids are frozen in Phase 1.
            constexpr const char* kGameplayBase = "GameplayBase";
            constexpr const char* kMenuBase = "MenuBase";

            auto entry = [](ContextSeed seed) { return seed; };

            // NOTE: The Phase 1 plan requires we move all legacy context aliases/menu names into this seed,
            //       so compatibility aliases do not maintain
            //       a second hand-written table.
            auto seed = std::vector<ContextSeed>{
                entry({ UiContextId::None, "Gameplay", Legacy::Gameplay, kGameplayBase, {}, { kGameplayBase },
                    { "Gameplay" },
                    {} }),

                // Tracked menu base (generic Menu). This is the canonical compatibility surface used by legacy config,
                // but bindings inside [Menu] are compiled as base bindings (layerId=nullopt) in Phase 1.
                entry({ UiContextId::UnknownTrackedMenu, "Menu", Legacy::Menu, kMenuBase, { "UnknownTrackedMenuLayer" }, { kMenuBase, "UnknownTrackedMenuLayer" },
                    {
                        "Menu",
                        "Main Menu",
                        "Loading Menu",
                        "Credits Menu",
                        "Crafting Menu",
                        "TitleSequence Menu",
                        "Sleep/Wait Menu",
                        "Kinect Menu",
                        "SafeZoneMenu",
                        "StreamingInstallMenu",
                    },
                    {
                        "Main Menu",
                        "Loading Menu",
                        "Credits Menu",
                        "Crafting Menu",
                        "TitleSequence Menu",
                        "Sleep/Wait Menu",
                        "Kinect Menu",
                        "SafeZoneMenu",
                        "StreamingInstallMenu",
                    } }),

                entry({ UiContextId::Inventory, "InventoryMenu", Legacy::InventoryMenu, kMenuBase, { "InventoryLayer" }, { kMenuBase, "InventoryLayer" },
                    { "InventoryMenu", "Inventory Menu" },
                    { "InventoryMenu" } }),
                entry({ UiContextId::Magic, "MagicMenu", Legacy::MagicMenu, kMenuBase, { "MagicLayer" }, { kMenuBase, "MagicLayer" },
                    { "MagicMenu", "Magic Menu" },
                    { "MagicMenu" } }),
                entry({ UiContextId::Map, "MapMenu", Legacy::MapMenu, kMenuBase, { "MapLayer" }, { kMenuBase, "MapLayer" },
                    { "MapMenu", "Map Menu" },
                    { "MapMenu" } }),
                entry({ UiContextId::Journal, "JournalMenu", Legacy::JournalMenu, kMenuBase, { "JournalLayer" }, { kMenuBase, "JournalLayer" },
                    { "JournalMenu", "Journal Menu" },
                    { "JournalMenu" } }),
                entry({ UiContextId::Dialogue, "DialogueMenu", Legacy::DialogueMenu, kMenuBase, { "DialogueLayer" }, { kMenuBase, "DialogueLayer" },
                    { "DialogueMenu", "Dialogue Menu" },
                    { "DialogueMenu" } }),
                entry({ UiContextId::Favorites, "FavoritesMenu", Legacy::FavoritesMenu, kMenuBase, { "FavoritesLayer" }, { kMenuBase, "FavoritesLayer" },
                    { "FavoritesMenu", "Favorites Menu" },
                    { "FavoritesMenu" } }),
                entry({ UiContextId::Tween, "TweenMenu", Legacy::TweenMenu, kMenuBase, { "TweenLayer" }, { kMenuBase, "TweenLayer" },
                    { "TweenMenu", "Tween Menu" },
                    { "TweenMenu" } }),
                entry({ UiContextId::Container, "ContainerMenu", Legacy::ContainerMenu, kMenuBase, { "ContainerLayer" }, { kMenuBase, "ContainerLayer" },
                    { "ContainerMenu", "Container Menu" },
                    { "ContainerMenu" } }),
                entry({ UiContextId::Barter, "BarterMenu", Legacy::BarterMenu, kMenuBase, { "BarterLayer" }, { kMenuBase, "BarterLayer" },
                    { "BarterMenu", "Barter Menu" },
                    { "BarterMenu" } }),
                entry({ UiContextId::Training, "TrainingMenu", Legacy::TrainingMenu, kMenuBase, { "TrainingLayer" }, { kMenuBase, "TrainingLayer" },
                    { "TrainingMenu", "Training Menu" },
                    { "Training Menu" } }),
                entry({ UiContextId::LevelUp, "LevelUpMenu", Legacy::LevelUpMenu, kMenuBase, { "LevelUpLayer" }, { kMenuBase, "LevelUpLayer" },
                    { "LevelUpMenu", "LevelUp Menu" },
                    { "LevelUp Menu" } }),
                entry({ UiContextId::RaceSex, "RaceSexMenu", Legacy::RaceSexMenu, kMenuBase, { "RaceSexLayer" }, { kMenuBase, "RaceSexLayer" },
                    { "RaceSexMenu", "RaceSex Menu" },
                    { "RaceSex Menu" } }),
                entry({ UiContextId::StatsMenu, "StatsMenu", Legacy::StatsMenu, kMenuBase, { "StatsMenuLayer" }, { kMenuBase, "StatsMenuLayer" },
                    { "StatsMenu", "Stats Menu" },
                    { "StatsMenu", "Stats Menu" } }),
                entry({ UiContextId::SkillMenu, "SkillMenu", Legacy::SkillMenu, kMenuBase, { "SkillMenuLayer" }, { kMenuBase, "SkillMenuLayer" },
                    { "SkillMenu", "Skill Menu" },
                    { "SkillMenu" } }),
                entry({ UiContextId::BookMenu, "BookMenu", Legacy::BookMenu, kMenuBase, { "BookLayer" }, { kMenuBase, "BookLayer" },
                    { "BookMenu", "Book Menu" },
                    { "BookMenu", "Book Menu" } }),
                entry({ UiContextId::MessageBox, "MessageBoxMenu", Legacy::MessageBoxMenu, kMenuBase, { "MessageBoxLayer" }, { kMenuBase, "MessageBoxLayer" },
                    { "MessageBoxMenu", "MessageBox Menu" },
                    { "MessageBoxMenu" } }),
                entry({ UiContextId::Quantity, "QuantityMenu", Legacy::QuantityMenu, kMenuBase, { "QuantityLayer" }, { kMenuBase, "QuantityLayer" },
                    { "QuantityMenu", "Quantity Menu" },
                    { "QuantityMenu" } }),
                entry({ UiContextId::Gift, "GiftMenu", Legacy::GiftMenu, kMenuBase, { "GiftLayer" }, { kMenuBase, "GiftLayer" },
                    { "GiftMenu", "Gift Menu" },
                    { "GiftMenu" } }),
                entry({ UiContextId::Creations, "CreationsMenu", Legacy::CreationsMenu, kMenuBase, { "CreationsLayer" }, { kMenuBase, "CreationsLayer" },
                    {
                        "CreationsMenu",
                        "Creations Menu",
                        "CreationClubMenu",
                        "Creation Club Menu",
                        "Mod Manager Menu",
                    },
                    {
                        "Creations Menu",
                        "Creation Club Menu",
                        "Mod Manager Menu",
                    } }),

                entry({ UiContextId::Console, "Console", Legacy::Console, kMenuBase, { "ConsoleLayer" }, { kMenuBase, "ConsoleLayer" },
                    { "Console", "Console Native UI Menu" },
                    { "Console", "Console Native UI Menu" } }),
                entry({ UiContextId::ItemMenu, "ItemMenu", Legacy::ItemMenu, kMenuBase, { "ItemMenuLayer" }, { kMenuBase, "ItemMenuLayer" },
                    { "ItemMenu", "Item Menu" },
                    { "ItemMenu", "Item Menu" } }),
                entry({ UiContextId::DebugText, "DebugText", Legacy::DebugText, kMenuBase, { "DebugTextLayer" }, { kMenuBase, "DebugTextLayer" },
                    { "DebugText", "Debug Text Menu" },
                    { "Debug Text Menu" } }),
                entry({ UiContextId::MapMenuContext, "MapMenuContext", Legacy::MapMenuContext, kMenuBase, { "MapLayer" }, { kMenuBase, "MapLayer" },
                    { "MapMenuContext" },
                    {} }),
                entry({ UiContextId::Stats, "Stats", Legacy::Stats, kMenuBase, { "StatsLayer" }, { kMenuBase, "StatsLayer" },
                    { "Stats" },
                    {} }),
                entry({ UiContextId::Cursor, "Cursor", Legacy::Cursor, kMenuBase, { "CursorLayer" }, { kMenuBase, "CursorLayer" },
                    { "Cursor", "Cursor Menu", "CursorMenu" },
                    { "Cursor Menu", "CursorMenu" } }),
                entry({ UiContextId::Book, "Book", Legacy::Book, kMenuBase, { "BookLayer" }, { kMenuBase, "BookLayer" },
                    { "Book" },
                    {} }),
                entry({ UiContextId::DebugOverlay, "DebugOverlay", Legacy::DebugOverlay, kMenuBase, { "DebugOverlayLayer" }, { kMenuBase, "DebugOverlayLayer" },
                    { "DebugOverlay" },
                    {} }),
                entry({ UiContextId::TFCMode, "TFCMode", Legacy::TFCMode, kMenuBase, { "TFCModeLayer" }, { kMenuBase, "TFCModeLayer" },
                    { "TFCMode" },
                    {} }),
                entry({ UiContextId::DebugMapMenu, "DebugMapMenu", Legacy::DebugMapMenu, kMenuBase, { "DebugMapLayer" }, { kMenuBase, "DebugMapLayer" },
                    { "DebugMapMenu" },
                    {} }),
                entry({ UiContextId::Lockpicking, "Lockpicking", Legacy::Lockpicking, kMenuBase, { "LockpickingLayer" }, { kMenuBase, "LockpickingLayer" },
                    { "Lockpicking", "LockpickingMenu", "Lockpicking Menu" },
                    { "Lockpicking Menu", "LockpickingMenu" } }),
                entry({ UiContextId::Favor, "Favor", Legacy::Favor, kMenuBase, { "FavorLayer" }, { kMenuBase, "FavorLayer" },
                    { "Favor" },
                    {} }),

                // Gameplay substates expressed as layers.
                entry({ UiContextId::Combat, "Combat", Legacy::Combat, kGameplayBase, { "CombatLayer" }, { kGameplayBase, "CombatLayer" }, { "Combat" }, {} }),
                entry({ UiContextId::Sneaking, "Sneaking", Legacy::Sneaking, kGameplayBase, { "SneakLayer" }, { kGameplayBase, "SneakLayer" }, { "Sneaking" }, {} }),
                entry({ UiContextId::Riding, "Riding", Legacy::Riding, kGameplayBase, { "RidingLayer" }, { kGameplayBase, "RidingLayer" }, { "Riding" }, {} }),
                entry({ UiContextId::Werewolf, "Werewolf", Legacy::Werewolf, kGameplayBase, { "WerewolfLayer" }, { kGameplayBase, "WerewolfLayer" }, { "Werewolf" }, {} }),
                entry({ UiContextId::VampireLord, "VampireLord", Legacy::VampireLord, kGameplayBase, { "VampireLordLayer" }, { kGameplayBase, "VampireLordLayer" }, { "VampireLord" }, {} }),
                entry({ UiContextId::Death, "Death", Legacy::Death, kGameplayBase, { "DeathLayer" }, { kGameplayBase, "DeathLayer" }, { "Death" }, {} }),
                entry({ UiContextId::Bleedout, "Bleedout", Legacy::Bleedout, kGameplayBase, { "BleedoutLayer" }, { kGameplayBase, "BleedoutLayer" }, { "Bleedout" }, {} }),
                entry({ UiContextId::Ragdoll, "Ragdoll", Legacy::Ragdoll, kGameplayBase, { "RagdollLayer" }, { kGameplayBase, "RagdollLayer" }, { "Ragdoll" }, {} }),
                entry({ UiContextId::KillMove, "KillMove", Legacy::KillMove, kGameplayBase, { "KillMoveLayer" }, { kGameplayBase, "KillMoveLayer" }, { "KillMove" }, {} }),

                // Passthrough overlays are a declared non-owner in Phase 1.
                entry({ UiContextId::PassthroughOverlay, "PassthroughOverlay", std::nullopt, std::nullopt, {}, {},
                    { "PassthroughOverlay" }, {} }),
            };

            for (auto& s : seed) {
                if (!s.presentationPolicyId) {
                    s.presentationPolicyId = s.canonicalName;
                }
            }
            return seed;
        }

        std::string NormalizeKey(std::string_view text)
        {
            // Catalog keys are treated as case-sensitive stable identifiers, but we still trim.
            return dualpad::input::ini::Trim(std::string(text));
        }

        UnknownMenuPolicy CompileUnknownMenuPolicy(std::string_view rawValue)
        {
            const auto normalized = dualpad::input::ini::ToLower(dualpad::input::ini::Trim(std::string(rawValue)));
            if (normalized == "track") {
                return UnknownMenuPolicy::Track;
            }
            return UnknownMenuPolicy::Passthrough;
        }
    }

    const CompiledContextCatalog& ContextCatalog::BuiltInCatalog()
    {
        static const CompiledContextCatalog builtIn = []() {
            dualpad::input_v2::config::LegacyMenuPolicyAst policy{};
            const auto res = Compile(policy, 0);
            if (!res.ok) {
                // This should never happen unless the built-in seed is invalid.
                CompiledContextCatalog empty{};
                return empty;
            }
            return res.catalog;
        }();
        return builtIn;
    }

    CatalogCompileResult ContextCatalog::Compile(
        const dualpad::input_v2::config::LegacyMenuPolicyAst& importedPolicy,
        std::uint64_t manifestEpoch)
    {
        CatalogCompileResult result{};
        result.catalog.manifestEpoch = manifestEpoch;

        const auto seed = BuildSeed();
        result.catalog.entries.reserve(seed.size());

        std::unordered_set<UiContextId, UiContextIdHash> idsSeen;
        std::unordered_set<std::string> canonicalNamesSeen;

        for (const auto& s : seed) {
            if (idsSeen.contains(s.id)) {
                result.ok = false;
                result.message = std::format("duplicate UiContextId in seed: {}", static_cast<std::uint16_t>(s.id));
                return result;
            }
            idsSeen.insert(s.id);

            const auto canonical = NormalizeKey(s.canonicalName);
            if (canonical.empty()) {
                result.ok = false;
                result.message = "empty canonicalContextName in seed";
                return result;
            }
            if (canonicalNamesSeen.contains(canonical)) {
                result.ok = false;
                result.message = std::format("duplicate canonicalContextName in seed: {}", canonical);
                return result;
            }
            canonicalNamesSeen.insert(canonical);

            CompiledContextEntry entry{};
            entry.uiContextId = s.id;
            entry.canonicalContextName = canonical;
            entry.presentationPolicyId = NormalizeKey(*s.presentationPolicyId);
            if (entry.presentationPolicyId.empty()) {
                result.ok = false;
                result.message = std::format("empty presentationPolicyId in seed: {}", canonical);
                return result;
            }
            entry.legacyInputContext = s.legacyInputContext;
            if (s.defaultActionSetId) {
                entry.defaultActionSetId = std::string(*s.defaultActionSetId);
            }
            entry.defaultLayerIds = s.defaultLayerIds;
            entry.scopeAnchorIds = s.scopeAnchorIds;
            entry.aliases = s.aliases;
            entry.menuNames = s.menuNames;

            const auto index = result.catalog.entries.size();
            result.catalog.entryIndexById.emplace(s.id, index);
            result.catalog.entries.push_back(std::move(entry));
        }

        // Build alias and menu-name indices with fail-closed duplicate detection.
        for (const auto& entry : result.catalog.entries) {
            for (const auto& aliasRaw : entry.aliases) {
                const auto alias = NormalizeKey(aliasRaw);
                if (alias.empty()) {
                    continue;
                }

                auto [it, inserted] = result.catalog.aliasIndex.emplace(alias, entry.uiContextId);
                if (!inserted && it->second != entry.uiContextId) {
                    result.ok = false;
                    result.message = std::format("duplicate context alias '{}' maps to multiple UiContextId", alias);
                    return result;
                }
            }

            // Canonical name is always an alias.
            auto [canonIt, canonInserted] =
                result.catalog.aliasIndex.emplace(entry.canonicalContextName, entry.uiContextId);
            if (!canonInserted && canonIt->second != entry.uiContextId) {
                result.ok = false;
                result.message = std::format(
                    "canonicalContextName '{}' maps to multiple UiContextId",
                    entry.canonicalContextName);
                return result;
            }

            for (const auto& menuNameRaw : entry.menuNames) {
                const auto menuName = NormalizeKey(menuNameRaw);
                if (menuName.empty()) {
                    continue;
                }

                auto [it, inserted] = result.catalog.menuNameIndex.emplace(menuName, entry.uiContextId);
                if (!inserted && it->second != entry.uiContextId) {
                    result.ok = false;
                    result.message = std::format("duplicate menu name '{}' maps to multiple UiContextId", menuName);
                    return result;
                }
            }
        }

        // Compile menu policy metadata. Track rules target UiContextId by canonical/alias name.
        result.catalog.menuPolicy.unknownMenuPolicy = CompileUnknownMenuPolicy(importedPolicy.unknownMenuPolicy);
        result.catalog.menuPolicy.logUnknownMenuProbe = importedPolicy.logUnknownMenuProbe;
        result.catalog.menuPolicy.logUnknownMenuDecision = importedPolicy.logUnknownMenuDecision;
        for (const auto& [menuNameRaw, targetContextNameRaw] : importedPolicy.trackRules) {
            const auto menuName = NormalizeKey(menuNameRaw);
            const auto targetName = NormalizeKey(targetContextNameRaw);
            if (menuName.empty() || targetName.empty()) {
                continue;
            }

            const auto resolvedTarget = ResolveAlias(result.catalog, targetName);
            if (!resolvedTarget) {
                result.ok = false;
                result.message = std::format("track rule '{}' targets unknown context '{}'", menuName, targetName);
                return result;
            }

            if (*resolvedTarget == UiContextId::None) {
                result.ok = false;
                result.message = std::format("track rule '{}' cannot target gameplay context '{}'", menuName, targetName);
                return result;
            }

            auto [it, inserted] = result.catalog.menuPolicy.trackRules.emplace(menuName, *resolvedTarget);
            if (!inserted && it->second != *resolvedTarget) {
                result.ok = false;
                result.message = std::format("duplicate track rule '{}' maps to multiple targets", menuName);
                return result;
            }
        }

        for (const auto& [menuNameRaw, enabled] : importedPolicy.ignoreRules) {
            if (!enabled) {
                continue;
            }
            const auto menuName = NormalizeKey(menuNameRaw);
            if (!menuName.empty()) {
                result.catalog.menuPolicy.ignoreRules.insert(menuName);
            }
        }

        result.ok = true;
        return result;
    }

    const CompiledContextEntry* ContextCatalog::FindById(const CompiledContextCatalog& catalog, UiContextId id)
    {
        const auto it = catalog.entryIndexById.find(id);
        if (it == catalog.entryIndexById.end()) {
            return nullptr;
        }
        const auto idx = it->second;
        if (idx >= catalog.entries.size()) {
            return nullptr;
        }
        return &catalog.entries[idx];
    }

    std::optional<UiContextId> ContextCatalog::ResolveAlias(const CompiledContextCatalog& catalog, std::string_view name)
    {
        const auto key = NormalizeKey(name);
        if (key.empty()) {
            return std::nullopt;
        }

        const auto it = catalog.aliasIndex.find(key);
        if (it == catalog.aliasIndex.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    std::optional<UiContextId> ContextCatalog::ResolveMenuName(const CompiledContextCatalog& catalog, std::string_view menuName)
    {
        const auto key = NormalizeKey(menuName);
        if (key.empty()) {
            return std::nullopt;
        }

        const auto it = catalog.menuNameIndex.find(key);
        if (it == catalog.menuNameIndex.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    std::optional<dualpad::input::InputContext> ContextCatalog::ToLegacyInputContext(const CompiledContextCatalog& catalog, UiContextId id)
    {
        const auto* entry = FindById(catalog, id);
        if (!entry) {
            return std::nullopt;
        }
        return entry->legacyInputContext;
    }
}
