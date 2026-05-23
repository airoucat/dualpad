#pragma once

#include <string>

namespace dualpad::input_v2::actions
{
    struct CompiledActionManifest;
}

namespace dualpad::input_v2::config
{
    struct LegacyImportBundle;

    struct ValidationResult
    {
        bool ok{ false };
        std::string message;
    };

    struct CompiledConfigBundle;
}

namespace dualpad::input_v2::context
{
    struct CompiledContextCatalog;
}

namespace dualpad::input_v2::config
{
    // Phase 1: fail-closed validation for imported INI AST and compiled bundle cross-checks.
    class ManifestValidator
    {
    public:
        static ValidationResult ValidateImportedAst(const LegacyImportBundle& imported);
        static ValidationResult ValidateCompiledBundle(
            const context::CompiledContextCatalog& catalog,
            const actions::CompiledActionManifest& manifest);

        static ValidationResult ValidateCompiledBundle(const CompiledConfigBundle& bundle);
    };
}

