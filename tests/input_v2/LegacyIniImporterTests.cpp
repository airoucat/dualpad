#include "pch.h"

#include "input_v2/config/LegacyIniImporter.h"

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

    bool Contains(std::string_view text, std::string_view needle)
    {
        return text.find(needle) != std::string_view::npos;
    }
}

void RunLegacyIniImporterTests()
{
    namespace cfg = dualpad::input_v2::config;

    const auto root = FindProjectRoot(std::filesystem::current_path());
    const auto bindings = root / "tests" / "fixtures" / "input_v2" / "valid_bindings.ini";
    const auto policy = root / "tests" / "fixtures" / "input_v2" / "valid_menu_policy.ini";

    {
        const auto res = cfg::LegacyIniImporter::Import(bindings, policy);
        Require(res.ok, "LegacyIniImporter::Import(valid fixtures) should succeed");
        Require(!res.bundle.bindingsMissing, "valid_bindings.ini should not be missing");
        Require(!res.bundle.menuPolicyMissing, "valid_menu_policy.ini should not be missing");
        Require(!res.bundle.bindings.sections.empty(), "bindings AST should not be empty");
        Require(!res.bundle.menuPolicy.trackRules.empty(), "menu policy trackRules should not be empty");
    }

    {
        const auto temp = std::filesystem::temp_directory_path() / "dualpad-inputv2-import-missing";
        std::filesystem::remove_all(temp);
        const auto missingBindings = temp / "DualPadBindings.ini";
        const auto missingPolicy = temp / "DualPadMenuPolicy.ini";

        const auto res = cfg::LegacyIniImporter::Import(missingBindings, missingPolicy);
        Require(res.ok, "LegacyIniImporter::Import(missing) should still be ok");
        Require(res.bundle.bindingsMissing, "bindingsMissing should be true");
        Require(res.bundle.menuPolicyMissing, "menuPolicyMissing should be true");
    }

    {
        const auto temp = std::filesystem::temp_directory_path() / "dualpad-inputv2-import-entry-before-section";
        std::filesystem::remove_all(temp);
        const auto badBindings = temp / "DualPadBindings.ini";
        WriteFile(badBindings, "Button:Cross=Game.Jump\n");

        const auto res = cfg::LegacyIniImporter::Import(badBindings, policy);
        Require(!res.ok, "existing bindings file with entry before section must fail import");
        Require(
            Contains(res.message, "entry without section"),
            "entry-before-section failure should be visible in import message");
    }

    {
        const auto temp = std::filesystem::temp_directory_path() / "dualpad-inputv2-import-malformed-line";
        std::filesystem::remove_all(temp);
        const auto badBindings = temp / "DualPadBindings.ini";
        WriteFile(badBindings, "[Gameplay]\nButton:Cross Game.Jump\n");

        const auto res = cfg::LegacyIniImporter::Import(badBindings, policy);
        Require(!res.ok, "existing bindings file with malformed line must fail import");
        Require(
            Contains(res.message, "missing '='"),
            "malformed-line failure should be visible in import message");
    }

    {
        const auto temp = std::filesystem::temp_directory_path() / "dualpad-inputv2-import-bindings-directory";
        std::filesystem::remove_all(temp);
        const auto bindingsDirectory = temp / "DualPadBindings.ini";
        std::filesystem::create_directories(bindingsDirectory);

        const auto res = cfg::LegacyIniImporter::Import(bindingsDirectory, policy);
        Require(!res.ok, "existing bindings path that is a directory must fail import");
        Require(
            Contains(res.message, "failed to open ini"),
            "unreadable bindings failure should be visible in import message");
    }
}

