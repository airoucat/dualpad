#include "pch.h"

#include "input/InputContext.h"
#include "input_v2/config/LegacyIniImporter.h"
#include "input_v2/context/ContextCatalog.h"

#include <filesystem>
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

    std::filesystem::path FindProjectRoot(std::filesystem::path from)
    {
        if (std::filesystem::is_regular_file(from)) {
            from = from.parent_path();
        }
        while (!from.empty()) {
            if (std::filesystem::is_regular_file(from / "xmake.lua")) {
                return from;
            }
            const auto parent = from.parent_path();
            if (parent == from) {
                break;
            }
            from = parent;
        }
        return std::filesystem::current_path();
    }
}

void RunContextCatalogTests()
{
    namespace cfg = dualpad::input_v2::config;
    namespace ctx = dualpad::input_v2::context;

    const auto root = FindProjectRoot(std::filesystem::current_path());
    const auto bindings = root / "tests" / "fixtures" / "input_v2" / "valid_bindings.ini";
    const auto policy = root / "tests" / "fixtures" / "input_v2" / "valid_menu_policy.ini";

    const auto imported = cfg::LegacyIniImporter::Import(bindings, policy);
    Require(imported.ok, "import fixtures should succeed");

    const auto compiled = ctx::ContextCatalog::Compile(imported.bundle.menuPolicy, 7);
    Require(compiled.ok, compiled.message);
    Require(compiled.catalog.manifestEpoch == 7, "catalog epoch should be set");

    {
        const auto alias = ctx::ContextCatalog::ResolveAlias(compiled.catalog, "Inventory Menu");
        Require(alias.has_value(), "Inventory Menu should resolve via aliasIndex");
        const auto legacy = ctx::ContextCatalog::ToLegacyInputContext(compiled.catalog, *alias);
        Require(legacy.has_value() && *legacy == dualpad::input::InputContext::InventoryMenu, "alias should map to legacy InventoryMenu");
    }

    {
        const auto menu = ctx::ContextCatalog::ResolveMenuName(compiled.catalog, "InventoryMenu");
        Require(menu.has_value(), "InventoryMenu should resolve via menuNameIndex");
        const auto legacy = ctx::ContextCatalog::ToLegacyInputContext(compiled.catalog, *menu);
        Require(legacy.has_value() && *legacy == dualpad::input::InputContext::InventoryMenu, "menu name should map to legacy InventoryMenu");
    }

    {
        // track rule from fixture: ThirdPartyWidget -> Menu
        auto it = compiled.catalog.menuPolicy.trackRules.find("ThirdPartyWidget");
        Require(it != compiled.catalog.menuPolicy.trackRules.end(), "fixture track rule should compile");
        const auto* entry = ctx::ContextCatalog::FindById(compiled.catalog, it->second);
        Require(entry && entry->legacyInputContext.has_value(), "track rule target must have legacy mapping");
        Require(*entry->legacyInputContext == dualpad::input::InputContext::Menu, "ThirdPartyWidget should target legacy Menu");
    }

    {
        cfg::LegacyMenuPolicyAst bad{};
        bad.trackRules.emplace_back("Widget", "NotAContext");
        const auto res = ctx::ContextCatalog::Compile(bad, 1);
        Require(!res.ok, "unknown track rule target should fail");
    }

    {
        cfg::LegacyMenuPolicyAst bad{};
        bad.trackRules.emplace_back("Widget", "Gameplay");
        const auto res = ctx::ContextCatalog::Compile(bad, 1);
        Require(!res.ok, "track rule targeting gameplay should fail");
    }
}

