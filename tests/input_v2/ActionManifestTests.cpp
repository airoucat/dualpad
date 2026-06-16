#include "pch.h"

#include "input/Action.h"
#include "input/PadProfile.h"
#include "input/backend/NativeActionDescriptor.h"
#include "input_v2/compat/LegacyInputContextCompat.h"
#include "input_v2/actions/ActionManifest.h"
#include "input_v2/config/LegacyIniImporter.h"
#include "input_v2/context/ContextCatalog.h"

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <stdexcept>
#include <optional>
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
    const auto repoBindings = root / "config" / "DualPadBindings.ini";
    const auto repoPolicy = root / "config" / "DualPadMenuPolicy.ini";
    const auto& bits = dualpad::input::GetPadBits(dualpad::input::GetActivePadProfile());

    const auto findProjectedBinding = [](
        const act::CompiledActionManifest& manifest,
        dualpad::input::InputContext context,
        std::uint32_t buttonCode) -> std::optional<act::ProjectedLegacyBinding> {
        const auto found = std::find_if(
            manifest.legacyBindingProjection.bindings.begin(),
            manifest.legacyBindingProjection.bindings.end(),
            [&](const act::ProjectedLegacyBinding& binding) {
                return binding.context == context &&
                    binding.trigger.type == dualpad::input::TriggerType::Button &&
                    binding.trigger.code == buttonCode;
            });
        if (found == manifest.legacyBindingProjection.bindings.end()) {
            return std::nullopt;
        }
        return *found;
    };

    const auto compileFromText = [&](std::string_view text) {
        const auto temp = std::filesystem::temp_directory_path() / "dualpad-action-manifest-hotfix";
        std::filesystem::remove_all(temp);
        const auto tempBindings = temp / "DualPadBindings.ini";
        const auto tempPolicy = temp / "DualPadMenuPolicy.ini";
        WriteFile(tempBindings, text);
        WriteFile(tempPolicy, "[Policy]\nunknown_menu_policy=track\n");

        const auto imported = cfg::LegacyIniImporter::Import(tempBindings, tempPolicy);
        Require(imported.ok, imported.message);
        const auto compiledCatalog = ctx::ContextCatalog::Compile(imported.bundle.menuPolicy, 1);
        Require(compiledCatalog.ok, compiledCatalog.message);
        return act::ActionManifest::Compile(compiledCatalog.catalog, imported.bundle.bindings, 1);
    };

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

        const auto menuStickIt = std::find_if(
            compiledManifest.manifest.actions.begin(),
            compiledManifest.manifest.actions.end(),
            [](const act::ActionDefinition& action) {
                return action.id == "Menu.LeftStick";
            });
        Require(menuStickIt != compiledManifest.manifest.actions.end(), "Menu.LeftStick action metadata should exist");
        Require(
            menuStickIt->valueKind == act::ActionValueKind::Axis2D,
            "Menu.LeftStick must be a two-dimensional native axis action");

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

        const auto cross = findProjectedBinding(
            compiledManifest.manifest,
            dualpad::input::InputContext::Menu,
            bits.cross);
        Require(cross.has_value(), "MenuCross_ResolvesToCancel_FromCheckedInBindings missing Cross binding");
        Require(
            cross->actionId == dualpad::input::actions::MenuCancel,
            "MenuCross_ResolvesToCancel_FromCheckedInBindings");
        Require(cross->bindingSource == "config", "Menu Cross checked-in binding source must be config");

        const auto triangle = findProjectedBinding(
            compiledManifest.manifest,
            dualpad::input::InputContext::Menu,
            bits.triangle);
        Require(triangle.has_value(), "MenuTriangle_ResolvesToConfirm_FromCheckedInBindings missing Triangle binding");
        Require(
            triangle->actionId == dualpad::input::actions::MenuConfirm,
            "MenuTriangle_ResolvesToConfirm_FromCheckedInBindings");
        Require(triangle->bindingSource == "config", "Menu Triangle checked-in binding source must be config");

        const auto circle = findProjectedBinding(
            compiledManifest.manifest,
            dualpad::input::InputContext::Menu,
            bits.circle);
        Require(circle.has_value(), "MenuCircle_ResolvesToDownloadAll_FromCheckedInBindings missing Circle binding");
        Require(
            circle->actionId == dualpad::input::actions::MenuDownloadAll,
            "MenuCircle_ResolvesToDownloadAll_FromCheckedInBindings");
        Require(circle->bindingSource == "config", "Menu Circle checked-in binding source must be config");
    }

    {
        const auto imported = cfg::LegacyIniImporter::Import(repoBindings, repoPolicy);
        Require(imported.ok, imported.message);

        const auto compiledCatalog = ctx::ContextCatalog::Compile(imported.bundle.menuPolicy, 7);
        Require(compiledCatalog.ok, compiledCatalog.message);

        const auto compiledManifest = act::ActionManifest::Compile(compiledCatalog.catalog, imported.bundle.bindings, 7);
        Require(compiledManifest.ok, compiledManifest.message);

        const auto triangle = findProjectedBinding(
            compiledManifest.manifest,
            dualpad::input::InputContext::FavoritesMenu,
            bits.triangle);
        Require(triangle.has_value(), "FavoritesMenu checked-in Triangle binding missing");
        Require(
            triangle->actionId == dualpad::input::actions::FavoritesToggleFocus,
            "FavoritesMenu Triangle must resolve to Favorites.ToggleFocus from checked-in config");
        Require(triangle->bindingSource == "config", "FavoritesMenu Triangle checked-in source must be config");

        const auto l1 = findProjectedBinding(
            compiledManifest.manifest,
            dualpad::input::InputContext::FavoritesMenu,
            bits.l1);
        Require(l1.has_value(), "FavoritesMenu checked-in L1 binding missing");
        Require(
            l1->actionId == dualpad::input::actions::FavoritesGroupConfirm,
            "FavoritesMenu L1 must resolve to Favorites.GroupConfirm from checked-in config");

        Require(
            dualpad::input::backend::FindNativeActionDescriptor(dualpad::input::actions::FavoritesToggleFocus) == nullptr,
            "Favorites.ToggleFocus must not emit native A/Accept without the page broker");
    }

    {
        const auto compiledManifest = compileFromText(R"ini(
[Touchpad]
Mode=Disabled
)ini");
        Require(compiledManifest.ok, compiledManifest.message);

        const auto cross = findProjectedBinding(compiledManifest.manifest, dualpad::input::InputContext::Menu, bits.cross);
        const auto triangle = findProjectedBinding(compiledManifest.manifest, dualpad::input::InputContext::Menu, bits.triangle);
        const auto circle = findProjectedBinding(compiledManifest.manifest, dualpad::input::InputContext::Menu, bits.circle);
        Require(cross.has_value() && triangle.has_value() && circle.has_value(), "fallback menu bindings must exist");
        Require(cross->actionId == dualpad::input::actions::MenuCancel, "FallbackMenuBindingsMatchCheckedInDefaults Cross");
        Require(triangle->actionId == dualpad::input::actions::MenuConfirm, "FallbackMenuBindingsMatchCheckedInDefaults Triangle");
        Require(circle->actionId == dualpad::input::actions::MenuDownloadAll, "FallbackMenuBindingsMatchCheckedInDefaults Circle");
        Require(cross->bindingSource == "fallback", "FallbackMenuBindingsMatchCheckedInDefaults Cross source");
        Require(triangle->bindingSource == "fallback", "FallbackMenuBindingsMatchCheckedInDefaults Triangle source");
        Require(circle->bindingSource == "fallback", "FallbackMenuBindingsMatchCheckedInDefaults Circle source");

        const auto favoritesTriangle = findProjectedBinding(
            compiledManifest.manifest,
            dualpad::input::InputContext::FavoritesMenu,
            bits.triangle);
        Require(favoritesTriangle.has_value(), "Fallback FavoritesMenu Triangle binding must exist");
        Require(
            favoritesTriangle->actionId == dualpad::input::actions::FavoritesToggleFocus,
            "Fallback FavoritesMenu Triangle must not inherit Menu.Confirm");
        Require(favoritesTriangle->bindingSource == "fallback", "Fallback FavoritesMenu Triangle source");
    }

    {
        const auto compiledManifest = compileFromText(R"ini(
[Touchpad]
Mode=Disabled

[Menu]
Button:Cross=Menu.Confirm
Button:Triangle=Menu.Cancel
Button:Circle=Menu.DownloadAll
)ini");
        Require(compiledManifest.ok, compiledManifest.message);

        const auto cross = findProjectedBinding(compiledManifest.manifest, dualpad::input::InputContext::Menu, bits.cross);
        Require(cross.has_value(), "ImportedBindingsOverrideFallback missing Cross");
        Require(cross->actionId == dualpad::input::actions::MenuConfirm, "ImportedBindingsOverrideFallback");
        Require(cross->bindingSource == "config", "Imported override must retain config source");
    }

    {
        const auto compiledManifest = compileFromText(R"ini(
[Touchpad]
Mode=Disabled

[Menu]
Button:Cross=Menu.Cancel
)ini");
        Require(compiledManifest.ok, compiledManifest.message);

        const auto cross = findProjectedBinding(compiledManifest.manifest, dualpad::input::InputContext::Menu, bits.cross);
        const auto triangle = findProjectedBinding(compiledManifest.manifest, dualpad::input::InputContext::Menu, bits.triangle);
        const auto circle = findProjectedBinding(compiledManifest.manifest, dualpad::input::InputContext::Menu, bits.circle);
        Require(cross.has_value() && triangle.has_value() && circle.has_value(), "FallbackOnlyFillsMissingTriggers bindings");
        Require(cross->actionId == dualpad::input::actions::MenuCancel, "FallbackOnlyFillsMissingTriggers Cross action");
        Require(cross->bindingSource == "config", "FallbackOnlyFillsMissingTriggers Cross source");
        Require(triangle->actionId == dualpad::input::actions::MenuConfirm, "FallbackOnlyFillsMissingTriggers Triangle action");
        Require(triangle->bindingSource == "fallback", "FallbackOnlyFillsMissingTriggers Triangle source");
        Require(circle->actionId == dualpad::input::actions::MenuDownloadAll, "FallbackOnlyFillsMissingTriggers Circle action");
        Require(circle->bindingSource == "fallback", "FallbackOnlyFillsMissingTriggers Circle source");
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
