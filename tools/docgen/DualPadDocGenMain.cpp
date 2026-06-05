#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    namespace fs = std::filesystem;

    constexpr std::string_view kGeneratorVersion = "DualPadDocGen/phase8b-v1";
    constexpr std::string_view kGeneratedDir = "docs/generated";
    constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
    constexpr std::uint64_t kFnvPrime = 1099511628211ull;

    struct IniEntry
    {
        std::string key;
        std::string value;
    };

    struct IniSection
    {
        std::string name;
        std::vector<IniEntry> entries;
    };

    struct BindingFact
    {
        std::string context;
        std::string trigger;
        std::string action;
    };

    struct Provenance
    {
        std::string manifestHash;
        std::string traceSchemaVersion;
        std::size_t replayScenarioCount{ 0 };
    };

    std::string ReadFile(const fs::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            throw std::runtime_error("cannot read " + path.generic_string());
        }
        std::ostringstream buffer;
        buffer << input.rdbuf();
        return buffer.str();
    }

    void WriteFile(const fs::path& path, const std::string& content)
    {
        fs::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) {
            throw std::runtime_error("cannot write " + path.generic_string());
        }
        output << content;
    }

    std::string Trim(std::string text)
    {
        const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
        text.erase(text.begin(), std::find_if(text.begin(), text.end(), notSpace));
        text.erase(std::find_if(text.rbegin(), text.rend(), notSpace).base(), text.end());
        return text;
    }

    std::string EscapePipe(std::string text)
    {
        for (char& c : text) {
            if (c == '|') {
                c = '/';
            }
        }
        return text;
    }

    std::string Hex64(std::uint64_t value)
    {
        constexpr char digits[] = "0123456789abcdef";
        std::string out(16, '0');
        for (int i = 15; i >= 0; --i) {
            out[static_cast<std::size_t>(i)] = digits[value & 0xf];
            value >>= 4;
        }
        return out;
    }

    void HashBytes(std::uint64_t& hash, std::string_view bytes)
    {
        for (unsigned char c : bytes) {
            hash ^= c;
            hash *= kFnvPrime;
        }
    }

    std::string NormalizeLineEndings(std::string content)
    {
        std::string normalized;
        normalized.reserve(content.size());
        for (std::size_t i = 0; i < content.size(); ++i) {
            if (content[i] == '\r') {
                if (i + 1 < content.size() && content[i + 1] == '\n') {
                    ++i;
                }
                normalized.push_back('\n');
                continue;
            }
            normalized.push_back(content[i]);
        }
        return normalized;
    }

    std::vector<IniSection> ParseIni(const std::string& content)
    {
        std::vector<IniSection> sections;
        IniSection* current = nullptr;
        std::istringstream lines(content);
        std::string line;
        while (std::getline(lines, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            const auto semicolon = line.find(';');
            if (semicolon != std::string::npos) {
                line = line.substr(0, semicolon);
            }
            line = Trim(line);
            if (line.empty()) {
                continue;
            }
            if (line.front() == '[' && line.back() == ']') {
                sections.push_back({ Trim(line.substr(1, line.size() - 2)), {} });
                current = &sections.back();
                continue;
            }
            const auto eq = line.find('=');
            if (eq == std::string::npos || current == nullptr) {
                continue;
            }
            current->entries.push_back({ Trim(line.substr(0, eq)), Trim(line.substr(eq + 1)) });
        }
        return sections;
    }

    std::vector<BindingFact> ExtractBindings(const std::vector<IniSection>& sections)
    {
        std::vector<BindingFact> facts;
        for (const auto& section : sections) {
            if (section.name == "Touchpad") {
                continue;
            }
            for (const auto& entry : section.entries) {
                if (entry.key == "Inherit") {
                    continue;
                }
                if (entry.key.find(':') == std::string::npos) {
                    continue;
                }
                facts.push_back({ section.name, entry.key, entry.value });
            }
        }
        std::sort(facts.begin(), facts.end(), [](const auto& a, const auto& b) {
            return std::tie(a.context, a.action, a.trigger) < std::tie(b.context, b.action, b.trigger);
        });
        return facts;
    }

    std::map<std::string, std::string> ExtractInherits(const std::vector<IniSection>& sections)
    {
        std::map<std::string, std::string> inherits;
        for (const auto& section : sections) {
            for (const auto& entry : section.entries) {
                if (entry.key == "Inherit") {
                    inherits[section.name] = entry.value;
                }
            }
        }
        return inherits;
    }

    std::vector<std::string> SortedSectionNames(const std::vector<IniSection>& sections)
    {
        std::set<std::string> names;
        for (const auto& section : sections) {
            if (section.name != "Touchpad") {
                names.insert(section.name);
            }
        }
        return { names.begin(), names.end() };
    }

    std::string ExtractTraceSchemaVersion(const fs::path& root)
    {
        const auto content = ReadFile(root / "src/input_v2/telemetry/TraceSchema.h");
        const std::string marker = "kTraceSchemaVersion = ";
        const auto pos = content.find(marker);
        if (pos == std::string::npos) {
            return "unknown";
        }
        const auto start = pos + marker.size();
        const auto end = content.find(';', start);
        if (end == std::string::npos) {
            return "unknown";
        }
        return Trim(content.substr(start, end - start));
    }

    std::size_t CountReplayScenarios(const fs::path& root)
    {
        const auto phase0 = root / "tests/replay/golden/phase0";
        if (!fs::exists(phase0)) {
            return 0;
        }
        std::size_t count = 0;
        for (const auto& entry : fs::directory_iterator(phase0)) {
            if (entry.is_directory()) {
                ++count;
            }
        }
        return count;
    }

    std::vector<fs::path> ProvenanceInputs(const fs::path& root)
    {
        std::vector<fs::path> files = {
            root / "config/DualPadBindings.ini",
            root / "config/DualPadMenuPolicy.ini",
            root / "tests/fixtures/input_v2/valid_bindings.ini",
            root / "tests/fixtures/input_v2/valid_menu_policy.ini",
            root / "src/input_v2/context/ContextCatalog.cpp",
            root / "src/input_v2/actions/ActionManifest.cpp",
            root / "src/input_v2/prompt/PromptSnapshotRecord.h",
            root / "src/input_v2/telemetry/TraceSchema.h",
            root / "xmake.lua",
        };

        const auto replayRoot = root / "tests/replay/golden";
        if (fs::exists(replayRoot)) {
            for (const auto& entry : fs::recursive_directory_iterator(replayRoot)) {
                if (entry.is_regular_file()) {
                    files.push_back(entry.path());
                }
            }
        }
        std::sort(files.begin(), files.end());
        return files;
    }

    Provenance BuildProvenance(const fs::path& root)
    {
        std::uint64_t hash = kFnvOffset;
        for (const auto& file : ProvenanceInputs(root)) {
            const auto relative = fs::relative(file, root).generic_string();
            HashBytes(hash, relative);
            HashBytes(hash, "\n");
            HashBytes(hash, NormalizeLineEndings(ReadFile(file)));
            HashBytes(hash, "\n");
        }

        return {
            Hex64(hash),
            ExtractTraceSchemaVersion(root),
            CountReplayScenarios(root),
        };
    }

    std::string GeneratedHeader(const Provenance& provenance)
    {
        std::ostringstream out;
        out << "<!-- generated by " << kGeneratorVersion << "; do not edit by hand -->\n\n";
        out << "## Provenance\n\n";
        out << "- source config root: `config/`\n";
        out << "- replay root: `tests/replay/golden/`\n";
        out << "- manifest hash: `" << provenance.manifestHash << "`\n";
        out << "- trace schema version: `" << provenance.traceSchemaVersion << "`\n";
        out << "- generator version / command: `" << kGeneratorVersion << "`, `xmake run DualPadDocGen`\n\n";
        return out.str();
    }

    std::string ContextCatalogDoc(const Provenance& provenance, const std::vector<IniSection>& bindings)
    {
        const auto inherits = ExtractInherits(bindings);
        std::map<std::string, std::size_t> bindingCounts;
        for (const auto& fact : ExtractBindings(bindings)) {
            ++bindingCounts[fact.context];
        }

        std::ostringstream out;
        out << "# Context Catalog Generated Facts\n\n";
        out << GeneratedHeader(provenance);
        out << "## Contexts From Checked-In Bindings\n\n";
        out << "| context | inherit | direct binding count |\n";
        out << "| --- | --- | ---: |\n";
        for (const auto& context : SortedSectionNames(bindings)) {
            const auto inheritIt = inherits.find(context);
            out << "| `" << EscapePipe(context) << "` | ";
            out << (inheritIt == inherits.end() ? "`<none>`" : ("`" + EscapePipe(inheritIt->second) + "`"));
            out << " | " << bindingCounts[context] << " |\n";
        }
        out << "\n## Close-Out Facts\n\n";
        out << "- runtime closeout owner: `PH8a`\n";
        out << "- governance closeout owner: `PH8b`\n";
        out << "- replay root is fixed at `tests/replay/golden/`\n";
        out << "- phase0 replay scenario count: `" << provenance.replayScenarioCount << "`\n";
        return out.str();
    }

    std::string ActionSetsDoc(const Provenance& provenance, const std::vector<IniSection>& bindings)
    {
        std::ostringstream out;
        out << "# Action Sets Generated Facts\n\n";
        out << GeneratedHeader(provenance);
        out << "## Binding Facts\n\n";
        out << "| context | action | trigger |\n";
        out << "| --- | --- | --- |\n";
        for (const auto& fact : ExtractBindings(bindings)) {
            out << "| `" << EscapePipe(fact.context) << "` | `" << EscapePipe(fact.action) << "` | `" << EscapePipe(fact.trigger) << "` |\n";
        }
        return out.str();
    }

    std::string PromptMatrixDoc(const Provenance& provenance, const std::vector<IniSection>& bindings)
    {
        std::ostringstream out;
        out << "# Prompt Matrix Generated Facts\n\n";
        out << GeneratedHeader(provenance);
        out << "## PromptSnapshotRecord Fields\n\n";
        out << "| field | source |\n";
        out << "| --- | --- |\n";
        out << "| `actionId` | `PromptQuery.actionId` |\n";
        out << "| `status` | `PromptDescriptor.status`; success is derived from `status == Ok` |\n";
        out << "| `resolvedSet` | `PromptDescriptor.resolvedSet` |\n";
        out << "| `resolvedContext` | `PromptDescriptor.resolvedContext` |\n";
        out << "| `primary` | `PromptDescriptor.primary` |\n";
        out << "| `alternates` | `PromptDescriptor.alternates` |\n";
        out << "| `resolutionSource` | `PromptDescriptor.resolutionSource` |\n";
        out << "| `fallback` | `PromptDescriptor.fallback` |\n";
        out << "| `deviceProfile` | `PromptDescriptor.deviceProfile` |\n";
        out << "| `promptScopeRevision` | `PromptDescriptor.promptScopeRevision` |\n";
        out << "| `manifestEpoch` | `PromptDescriptor.manifestEpoch` |\n\n";
        out << "## Visible Prompt Candidates From Checked-In Bindings\n\n";
        out << "| context | actionId | primary trigger | status | resolutionSource | fallback | deviceProfile |\n";
        out << "| --- | --- | --- | --- | --- | --- | --- |\n";
        for (const auto& fact : ExtractBindings(bindings)) {
            if (fact.trigger.rfind("Button:", 0) != 0 && fact.trigger.rfind("Gesture:", 0) != 0 && fact.trigger.rfind("Tap:", 0) != 0 && fact.trigger.rfind("Hold:", 0) != 0 && fact.trigger.rfind("Layer:", 0) != 0) {
                continue;
            }
            out << "| `" << EscapePipe(fact.context) << "` | `" << EscapePipe(fact.action) << "` | `" << EscapePipe(fact.trigger) << "` | `Ok` | `ExactScope` | `None` | `DualSense` |\n";
        }
        return out.str();
    }

    std::string PoliciesDoc(const Provenance& provenance, const std::vector<IniSection>& policies)
    {
        std::ostringstream out;
        out << "# Policies Generated Facts\n\n";
        out << GeneratedHeader(provenance);
        out << "## Menu Policy\n\n";
        out << "| section | key | value |\n";
        out << "| --- | --- | --- |\n";
        for (const auto& section : policies) {
            for (const auto& entry : section.entries) {
                out << "| `" << EscapePipe(section.name) << "` | `" << EscapePipe(entry.key) << "` | `" << EscapePipe(entry.value) << "` |\n";
            }
        }
        out << "\n## Canonical Phase 8 CI Targets\n\n";
        out << "| target | role |\n";
        out << "| --- | --- |\n";
        out << "| `DualPadReplayTests` | replay gate for `tests/replay/golden/` |\n";
        out << "| `DualPadInputV2Tests` | input-v2 contract gate |\n";
        out << "| `DualPadIngressTests` | ingress / resync gate |\n";
        out << "| `DualPadPromptSnapshotTests` | prompt snapshot gate |\n";
        out << "| `DualPadPropertyTests` | property gate |\n";
        out << "| `DualPadFuzzRegressionTests` | fuzz regression gate |\n";
        return out.str();
    }

    fs::path FindProjectRoot(const char* argv0)
    {
        auto current = fs::current_path();
        for (;;) {
            if (fs::exists(current / "xmake.lua") && fs::exists(current / "config/DualPadBindings.ini")) {
                return current;
            }
            if (current == current.root_path()) {
                break;
            }
            current = current.parent_path();
        }

        auto exe = fs::absolute(argv0).parent_path();
        for (;;) {
            if (fs::exists(exe / "xmake.lua") && fs::exists(exe / "config/DualPadBindings.ini")) {
                return exe;
            }
            if (exe == exe.root_path()) {
                break;
            }
            exe = exe.parent_path();
        }
        throw std::runtime_error("cannot locate project root");
    }
}

int main(int argc, char** argv)
{
    try {
        const auto root = FindProjectRoot(argc > 0 ? argv[0] : "");
        const auto bindings = ParseIni(ReadFile(root / "config/DualPadBindings.ini"));
        const auto policies = ParseIni(ReadFile(root / "config/DualPadMenuPolicy.ini"));
        const auto provenance = BuildProvenance(root);

        const auto generated = root / kGeneratedDir;
        WriteFile(generated / "context_catalog_zh.md", ContextCatalogDoc(provenance, bindings));
        WriteFile(generated / "action_sets_zh.md", ActionSetsDoc(provenance, bindings));
        WriteFile(generated / "prompt_matrix_zh.md", PromptMatrixDoc(provenance, bindings));
        WriteFile(generated / "policies_zh.md", PoliciesDoc(provenance, policies));

        std::cout << "DualPadDocGen wrote docs/generated with manifest hash "
                  << provenance.manifestHash << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "DualPadDocGen failed: " << e.what() << '\n';
        return 1;
    }
}
