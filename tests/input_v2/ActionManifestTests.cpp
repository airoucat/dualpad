#include "pch.h"

#include "input/Action.h"
#include "input_v2/compat/LegacyInputContextCompat.h"
#include "input_v2/actions/ActionManifest.h"
#include "input_v2/config/LegacyIniImporter.h"
#include "input_v2/context/ContextCatalog.h"

#include <filesystem>
#include <fstream>
#include <algorithm>
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
        Require(!compiledManifest.manifest.actions.empty(), "manifest should expose action registry");
        Require(!compiledManifest.manifest.actionSets.empty(), "manifest should expose action sets");
        Require(!compiledManifest.manifest.actionLayers.empty(), "manifest should expose action layers");
        Require(!compiledManifest.manifest.bindings.empty(), "manifest should expose compiled bindings");
        Require(!compiledManifest.manifest.displayBindings.empty(), "manifest should expose display bindings");
        Require(!compiledManifest.manifest.outputDescriptors.empty(), "manifest should expose output descriptors");
        Require(!compiledManifest.manifest.policies.empty(), "manifest should expose policies");
        Require(
            compiledManifest.manifest.touchpadConfig.mode == dualpad::input::TouchpadMode::Disabled,
            "top-level touchpad config should be compiled from [Touchpad]");
        Require(
            compiledManifest.manifest.legacyBindingProjection.touchpadConfig.mode ==
                compiledManifest.manifest.touchpadConfig.mode,
            "legacy projection touchpad config should be copied from compiled manifest");

        const auto actionIt = std::find_if(
            compiledManifest.manifest.actions.begin(),
            compiledManifest.manifest.actions.end(),
            [](const act::ActionDefinition& action) {
                return action.id == dualpad::input::actions::MenuConfirm;
            });
        Require(actionIt != compiledManifest.manifest.actions.end(), "Menu.Confirm action metadata should exist");
        Require(actionIt->valueKind == act::ActionValueKind::Digital, "Menu.Confirm should be digital");
        Require(actionIt->domain == act::ActionDomain::Menu, "Menu.Confirm should be a menu action");
        Require(!actionIt->contract.empty(), "action contract should be explicit");
        Require(!actionIt->outputDescriptorId.empty(), "action output descriptor id should be explicit");
        Require(!actionIt->promptHintId.empty(), "action prompt hint id should be explicit");

        const auto descriptorIt = std::find_if(
            compiledManifest.manifest.outputDescriptors.begin(),
            compiledManifest.manifest.outputDescriptors.end(),
            [&](const act::OutputDescriptor& descriptor) {
                return descriptor.id == actionIt->outputDescriptorId;
            });
        Require(descriptorIt != compiledManifest.manifest.outputDescriptors.end(), "action descriptor ref should resolve");

        bool foundConfirm = false;
        for (const auto& b : compiledManifest.manifest.legacyBindingProjection.bindings) {
            if (b.context == dualpad::input::InputContext::Menu &&
                b.actionId == dualpad::input::actions::MenuConfirm) {
                foundConfirm = true;
                break;
            }
        }
        Require(foundConfirm, "compiled projection should include Menu.Confirm binding in Menu context");

        bool foundCompiledDisplayBinding = false;
        for (const auto& display : compiledManifest.manifest.displayBindings) {
            if (display.actionId == dualpad::input::actions::MenuConfirm &&
                display.baseSetId == "MenuBase") {
                foundCompiledDisplayBinding = true;
                break;
            }
        }
        Require(foundCompiledDisplayBinding, "compiled manifest should include top-level display binding");
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
