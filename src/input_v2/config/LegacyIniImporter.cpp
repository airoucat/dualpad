#include "pch.h"

#include "input_v2/config/LegacyIniImporter.h"

#include "input/IniParseHelpers.h"

#include <fstream>

namespace dualpad::input_v2::config
{
    namespace
    {
        void AddWarning(std::vector<std::string>& warnings, std::string message)
        {
            warnings.push_back(std::move(message));
        }

        std::vector<ImportedSection> ParseIniFile(const std::filesystem::path& path, std::vector<std::string>& warnings)
        {
            std::ifstream in(path);
            if (!in.is_open()) {
                AddWarning(warnings, std::string("failed to open ini: ") + path.string());
                return {};
            }

            std::vector<ImportedSection> sections;
            ImportedSection* current = nullptr;

            std::string line;
            std::size_t lineNo = 0;
            while (std::getline(in, line)) {
                ++lineNo;

                if (lineNo == 1) {
                    dualpad::input::ini::StripUtf8Bom(line);
                }

                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }

                line = dualpad::input::ini::Trim(std::move(line));
                if (line.empty() || line[0] == ';' || line[0] == '#') {
                    continue;
                }

                if (line.front() == '[' && line.back() == ']') {
                    ImportedSection section{};
                    section.name = dualpad::input::ini::Trim(line.substr(1, line.size() - 2));
                    section.span = SourceSpan{ .path = path, .line = lineNo };
                    sections.push_back(std::move(section));
                    current = &sections.back();
                    continue;
                }

                const auto eqPos = line.find('=');
                if (eqPos == std::string::npos) {
                    AddWarning(warnings, std::string("ini line missing '=' at ") + path.string() + ":" + std::to_string(lineNo));
                    continue;
                }

                if (!current) {
                    AddWarning(warnings, std::string("ini entry without section at ") + path.string() + ":" + std::to_string(lineNo));
                    continue;
                }

                ImportedKeyValue kv{};
                kv.key = dualpad::input::ini::Trim(line.substr(0, eqPos));
                kv.value = dualpad::input::ini::Trim(line.substr(eqPos + 1));
                kv.span = SourceSpan{ .path = path, .line = lineNo };
                if (kv.key.empty()) {
                    continue;
                }
                current->entries.push_back(std::move(kv));
            }

            return sections;
        }

        void CompileMenuPolicyAst(
            const std::filesystem::path& path,
            const std::vector<ImportedSection>& sections,
            LegacyMenuPolicyAst& out)
        {
            (void)path;
            for (const auto& section : sections) {
                const auto sectionName = dualpad::input::ini::ToLower(section.name);
                if (sectionName == "policy") {
                    for (const auto& kv : section.entries) {
                        const auto key = dualpad::input::ini::ToLower(kv.key);
                        if (key == "unknown_menu_policy") {
                            out.unknownMenuPolicy = kv.value;
                        } else if (key == "log_unknown_menu_probe") {
                            out.logUnknownMenuProbe = dualpad::input::ini::ParseBool(kv.value, true);
                        } else if (key == "log_unknown_menu_decision") {
                            out.logUnknownMenuDecision = dualpad::input::ini::ParseBool(kv.value, true);
                        }
                    }
                    continue;
                }

                if (sectionName == "track") {
                    for (const auto& kv : section.entries) {
                        if (kv.key.empty() || kv.value.empty()) {
                            continue;
                        }
                        out.trackRules.emplace_back(kv.key, kv.value);
                    }
                    continue;
                }

                if (sectionName == "ignore") {
                    for (const auto& kv : section.entries) {
                        if (kv.key.empty()) {
                            continue;
                        }
                        const auto enabled = dualpad::input::ini::ParseBool(kv.value, false);
                        out.ignoreRules.emplace_back(kv.key, enabled);
                    }
                    continue;
                }
            }
        }
    }

    std::filesystem::path LegacyIniImporter::DefaultBindingsPath()
    {
        return "Data/SKSE/Plugins/DualPadBindings.ini";
    }

    std::filesystem::path LegacyIniImporter::DefaultMenuPolicyPath()
    {
        return "Data/SKSE/Plugins/DualPadMenuPolicy.ini";
    }

    LegacyImportResult LegacyIniImporter::Import(
        const std::filesystem::path& bindingsPath,
        const std::filesystem::path& menuPolicyPath)
    {
        LegacyImportResult result{};
        result.bundle.bindingsPath = bindingsPath.empty() ? DefaultBindingsPath() : bindingsPath;
        result.bundle.menuPolicyPath = menuPolicyPath.empty() ? DefaultMenuPolicyPath() : menuPolicyPath;

        if (!std::filesystem::exists(result.bundle.bindingsPath)) {
            result.bundle.bindingsMissing = true;
        } else {
            // Parse bindings ini into raw AST (no trigger/action lowering here).
            result.bundle.bindings.sections = ParseIniFile(result.bundle.bindingsPath, result.bundle.warnings);
        }

        if (!std::filesystem::exists(result.bundle.menuPolicyPath)) {
            result.bundle.menuPolicyMissing = true;
        } else {
            const auto sections = ParseIniFile(result.bundle.menuPolicyPath, result.bundle.warnings);
            CompileMenuPolicyAst(result.bundle.menuPolicyPath, sections, result.bundle.menuPolicy);
        }

        // Missing files are not an import failure; compile phase decides whether built-in defaults are allowed.
        result.ok = true;
        result.message = "ok";
        return result;
    }
}

