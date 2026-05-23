#include "pch.h"

#include "input/Action.h"
#include "input/InputContext.h"
#include "input_v2/actions/ActionManifest.h"
#include "input_v2/config/LegacyIniImporter.h"
#include "input_v2/context/ContextCatalog.h"

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

    void WriteFile(const std::filesystem::path& path, std::string_view contents)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << contents;
    }
}

void RunActionManifestTests()
{
    namespace cfg = dualpad::input_v2::config;
    namespace ctx = dualpad::input_v2::context;
    namespace act = dualpad::input_v2::actions;

    const auto root = FindProjectRoot(std::filesystem::current_path());
    const auto bindings = root / "tests" / "fixtures" / "input_v2" / "valid_bindings.ini";
    const auto policy = root / "tests" / "fixtures" / "input_v2" / "valid_menu_policy.ini";

    {
        const auto imported = cfg::LegacyIniImporter::Import(bindings, policy);
        Require(imported.ok, "import fixtures should succeed");

        const auto compiledCatalog = ctx::ContextCatalog::Compile(imported.bundle.menuPolicy, 5);
        Require(compiledCatalog.ok, compiledCatalog.message);

        const auto compiledManifest = act::ActionManifest::Compile(compiledCatalog.catalog, imported.bundle.bindings, 5);
        Require(compiledManifest.ok, compiledManifest.message);
        Require(compiledManifest.manifest.manifestEpoch == 5, "manifest epoch should be set");
        Require(
            compiledManifest.manifest.legacyBindingProjection.manifestEpoch == 5,
            "legacyBindingProjection epoch should match");

        bool foundConfirm = false;
        for (const auto& b : compiledManifest.manifest.legacyBindingProjection.bindings) {
            if (b.context == dualpad::input::InputContext::Menu &&
                b.actionId == dualpad::input::actions::MenuConfirm) {
                foundConfirm = true;
                break;
            }
        }
        Require(foundConfirm, "compiled projection should include Menu.Confirm binding in Menu context");
    }

    {
        // Ambiguous visible display binding must fail.
        const auto temp = std::filesystem::temp_directory_path() / "dualpad-inputv2-ambiguous-display";
        std::filesystem::remove_all(temp);
        const auto tempBindings = temp / "DualPadBindings.ini";
        const auto tempPolicy = temp / "DualPadMenuPolicy.ini";

        WriteFile(
            tempBindings,
            R"ini(
[Touchpad]
Mode=Disabled

[Menu]
Button:Cross=Menu.Confirm
Button:Square=Menu.Confirm
Button:Circle=Menu.Cancel
)ini");
        WriteFile(tempPolicy, "[Policy]\nunknown_menu_policy=track\n");

        const auto imported = cfg::LegacyIniImporter::Import(tempBindings, tempPolicy);
        Require(imported.ok, "import ambiguous bindings should succeed");

        const auto compiledCatalog = ctx::ContextCatalog::Compile(imported.bundle.menuPolicy, 1);
        Require(compiledCatalog.ok, compiledCatalog.message);

        const auto compiledManifest = act::ActionManifest::Compile(compiledCatalog.catalog, imported.bundle.bindings, 1);
        Require(!compiledManifest.ok, "ambiguous display bindings should fail compilation");
    }
}

