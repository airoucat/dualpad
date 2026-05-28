#include "pch.h"

#include "input_v2/config/LegacyIniImporter.h"
#include "input_v2/config/ManifestValidator.h"
#include "input_v2/context/ContextCatalog.h"
#include "input_v2/actions/ActionManifest.h"

#include <filesystem>
#include <fstream>
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

    void WriteFile(const std::filesystem::path& path, std::string_view contents)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << contents;
    }
}

void RunManifestValidatorTests()
{
    namespace cfg = dualpad::input_v2::config;
    namespace ctx = dualpad::input_v2::context;
    namespace act = dualpad::input_v2::actions;

    {
        // unknown action -> load fail
        const auto temp = std::filesystem::temp_directory_path() / "dualpad-inputv2-unknown-action";
        std::filesystem::remove_all(temp);
        const auto bindings = temp / "DualPadBindings.ini";
        const auto policy = temp / "DualPadMenuPolicy.ini";
        WriteFile(bindings, "[Gameplay]\nButton:Cross=Game.NotARealAction\n");
        WriteFile(policy, "[Policy]\nunknown_menu_policy=track\n");

        const auto imported = cfg::LegacyIniImporter::Import(bindings, policy);
        Require(imported.ok, "import should succeed");
        const auto validated = cfg::ManifestValidator::ValidateImportedAst(imported.bundle);
        Require(!validated.ok, "unknown action must fail ValidateImportedAst");
    }

    {
        // duplicate binding key -> load fail
        const auto temp = std::filesystem::temp_directory_path() / "dualpad-inputv2-dup-key";
        std::filesystem::remove_all(temp);
        const auto bindings = temp / "DualPadBindings.ini";
        const auto policy = temp / "DualPadMenuPolicy.ini";
        WriteFile(
            bindings,
            "[Menu]\n"
            "Button:Cross=Menu.Confirm\n"
            "Button:Cross=Menu.Cancel\n");
        WriteFile(policy, "[Policy]\nunknown_menu_policy=track\n");

        const auto imported = cfg::LegacyIniImporter::Import(bindings, policy);
        Require(imported.ok, "import should succeed");
        const auto validated = cfg::ManifestValidator::ValidateImportedAst(imported.bundle);
        Require(!validated.ok, "duplicate binding key must fail ValidateImportedAst");
    }

    {
        // FN + face button chord -> load fail
        const auto temp = std::filesystem::temp_directory_path() / "dualpad-inputv2-fn-face";
        std::filesystem::remove_all(temp);
        const auto bindings = temp / "DualPadBindings.ini";
        const auto policy = temp / "DualPadMenuPolicy.ini";
        WriteFile(bindings, "[Menu]\nCombo:FnLeft+Cross=Menu.Confirm\n");
        WriteFile(policy, "[Policy]\nunknown_menu_policy=track\n");

        const auto imported = cfg::LegacyIniImporter::Import(bindings, policy);
        Require(imported.ok, "import should succeed");
        const auto validated = cfg::ManifestValidator::ValidateImportedAst(imported.bundle);
        Require(!validated.ok, "FN+face Combo must fail ValidateImportedAst");
    }

    {
        // Combo:* must be exactly two buttons
        const auto temp = std::filesystem::temp_directory_path() / "dualpad-inputv2-combo-3";
        std::filesystem::remove_all(temp);
        const auto bindings = temp / "DualPadBindings.ini";
        const auto policy = temp / "DualPadMenuPolicy.ini";
        WriteFile(bindings, "[Menu]\nCombo:Cross+Square+Circle=Menu.Confirm\n");
        WriteFile(policy, "[Policy]\nunknown_menu_policy=track\n");

        const auto imported = cfg::LegacyIniImporter::Import(bindings, policy);
        Require(imported.ok, "import should succeed");
        const auto validated = cfg::ManifestValidator::ValidateImportedAst(imported.bundle);
        Require(!validated.ok, "Combo with 3 buttons must fail ValidateImportedAst");
    }

    {
        // projection epoch mismatch -> load fail
        cfg::LegacyMenuPolicyAst menuPolicy{};
        const auto catalogRes = ctx::ContextCatalog::Compile(menuPolicy, 1);
        Require(catalogRes.ok, "catalog compile should succeed");

        act::CompiledActionManifest manifest{};
        manifest.manifestEpoch = 10;
        manifest.actionSets = { "GameplayBase", "MenuBase" };
        manifest.actionLayers = { "InventoryLayer" };
        manifest.legacyBindingProjection.manifestEpoch = 9;

        const auto validated = cfg::ManifestValidator::ValidateCompiledBundle(catalogRes.catalog, manifest);
        Require(!validated.ok, "projection epoch mismatch must fail ValidateCompiledBundle");
    }

    {
        // invalid base set anchor -> load fail
        cfg::LegacyMenuPolicyAst menuPolicy{};
        const auto catalogRes = ctx::ContextCatalog::Compile(menuPolicy, 1);
        Require(catalogRes.ok, "catalog compile should succeed");

        auto catalog = catalogRes.catalog;
        Require(!catalog.entries.empty(), "catalog should not be empty");
        catalog.entries[0].defaultActionSetId = std::string("BadBaseSet");
        catalog.entries[0].defaultLayerIds.clear();
        catalog.entries[0].scopeAnchorIds = { "BadBaseSet" };

        act::CompiledActionManifest manifest{};
        manifest.manifestEpoch = 1;
        manifest.actionSets = { "GameplayBase", "MenuBase" };
        manifest.actionLayers = { "InventoryLayer" };
        manifest.legacyBindingProjection.manifestEpoch = 1;

        const auto validated = cfg::ManifestValidator::ValidateCompiledBundle(catalog, manifest);
        Require(!validated.ok, "invalid base set anchor must fail ValidateCompiledBundle");
    }

    {
        // invalid layer reference -> load fail
        cfg::LegacyMenuPolicyAst menuPolicy{};
        const auto catalogRes = ctx::ContextCatalog::Compile(menuPolicy, 1);
        Require(catalogRes.ok, "catalog compile should succeed");

        auto catalog = catalogRes.catalog;
        Require(!catalog.entries.empty(), "catalog should not be empty");
        catalog.entries[0].defaultActionSetId = std::string("MenuBase");
        catalog.entries[0].defaultLayerIds = { "NoSuchLayer" };
        catalog.entries[0].scopeAnchorIds = { "MenuBase", "NoSuchLayer" };

        act::CompiledActionManifest manifest{};
        manifest.manifestEpoch = 1;
        manifest.actionSets = { "GameplayBase", "MenuBase" };
        manifest.actionLayers = { "InventoryLayer" };
        manifest.legacyBindingProjection.manifestEpoch = 1;

        const auto validated = cfg::ManifestValidator::ValidateCompiledBundle(catalog, manifest);
        Require(!validated.ok, "invalid layer reference must fail ValidateCompiledBundle");
    }

    {
        // invalid scopeAnchorIds -> load fail
        cfg::LegacyMenuPolicyAst menuPolicy{};
        const auto catalogRes = ctx::ContextCatalog::Compile(menuPolicy, 1);
        Require(catalogRes.ok, "catalog compile should succeed");

        auto catalog = catalogRes.catalog;
        Require(!catalog.entries.empty(), "catalog should not be empty");
        catalog.entries[0].defaultActionSetId = std::string("MenuBase");
        catalog.entries[0].defaultLayerIds = { "InventoryLayer" };
        catalog.entries[0].scopeAnchorIds = { "MenuBase" };

        act::CompiledActionManifest manifest{};
        manifest.manifestEpoch = 1;
        manifest.actionSets = { "GameplayBase", "MenuBase" };
        manifest.actionLayers = { "InventoryLayer" };
        manifest.legacyBindingProjection.manifestEpoch = 1;

        const auto validated = cfg::ManifestValidator::ValidateCompiledBundle(catalog, manifest);
        Require(!validated.ok, "invalid scopeAnchorIds must fail ValidateCompiledBundle");
    }
}
