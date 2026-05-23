#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace dualpad::input_v2::config
{
    struct SourceSpan
    {
        std::filesystem::path path;
        std::size_t line{ 0 };
    };

    struct ImportedKeyValue
    {
        std::string key;
        std::string value;
        SourceSpan span;
    };

    struct ImportedSection
    {
        std::string name;
        SourceSpan span;
        std::vector<ImportedKeyValue> entries;
    };

    struct LegacyBindingsAst
    {
        std::vector<ImportedSection> sections;
    };

    struct LegacyMenuPolicyAst
    {
        // Raw values preserved from INI. Compile-time validation happens in ContextCatalog/ManifestValidator.
        std::string unknownMenuPolicy{ "passthrough" };
        bool logUnknownMenuProbe{ true };
        bool logUnknownMenuDecision{ true };

        // menuName -> targetContextName
        std::vector<std::pair<std::string, std::string>> trackRules;
        // menuName -> enabled
        std::vector<std::pair<std::string, bool>> ignoreRules;
    };

    struct LegacyImportBundle
    {
        std::filesystem::path bindingsPath;
        std::filesystem::path menuPolicyPath;

        bool bindingsMissing{ false };
        bool menuPolicyMissing{ false };

        LegacyBindingsAst bindings;
        LegacyMenuPolicyAst menuPolicy;

        std::vector<std::string> warnings;
    };

    struct LegacyImportResult
    {
        bool ok{ false };
        std::string message;
        LegacyImportBundle bundle;
    };

    class LegacyIniImporter
    {
    public:
        // Imports both DualPadBindings.ini and DualPadMenuPolicy.ini into a single bundle.
        static LegacyImportResult Import(
            const std::filesystem::path& bindingsPath,
            const std::filesystem::path& menuPolicyPath);

        // Helper: project-relative default paths used by legacy configs.
        static std::filesystem::path DefaultBindingsPath();
        static std::filesystem::path DefaultMenuPolicyPath();
    };
}

