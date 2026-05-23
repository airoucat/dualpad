#include "pch.h"

#include "input_v2/config/ManifestValidator.h"

#include "input/IniParseHelpers.h"
#include "input/mapping/PadEvent.h"
#include "input_v2/actions/ActionManifest.h"
#include "input_v2/config/AtomicConfigReloader.h"
#include "input_v2/config/LegacyIniImporter.h"
#include "input_v2/context/ContextCatalog.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace dualpad::input_v2::config
{
    namespace
    {
        using dualpad::input::IsSyntheticPadBitCode;

        std::string NormalizeSectionName(std::string_view name)
        {
            return dualpad::input::ini::Trim(std::string(name));
        }

        std::string NormalizeKey(std::string_view key)
        {
            return dualpad::input::ini::Trim(std::string(key));
        }

        std::uint32_t ButtonNameToCode(std::string_view name)
        {
            if (name == "Square") return 0x00000001;
            if (name == "Cross") return 0x00000002;
            if (name == "Circle") return 0x00000004;
            if (name == "Triangle") return 0x00000008;
            if (name == "L1") return 0x00000010;
            if (name == "R1") return 0x00000020;
            if (name == "L2Button") return 0x00000040;
            if (name == "R2Button") return 0x00000080;
            if (name == "Create") return 0x00000100;
            if (name == "Options") return 0x00000200;
            if (name == "L3") return 0x00000400;
            if (name == "R3") return 0x00000800;
            if (name == "PS") return 0x00001000;
            if (name == "Mute" || name == "Mic") return 0x00002000;
            if (name == "TouchpadClick") return 0x00004000;
            if (name == "DpadUp") return 0x00010000;
            if (name == "DpadDown") return 0x00020000;
            if (name == "DpadLeft") return 0x00040000;
            if (name == "DpadRight") return 0x00080000;
            if (name == "FnLeft") return 0x00100000;
            if (name == "FnRight") return 0x00200000;
            if (name == "BackLeft") return 0x00400000;
            if (name == "BackRight") return 0x00800000;
            return 0;
        }

        bool IsFaceButtonCode(std::uint32_t code)
        {
            switch (code) {
            case 0x00000001:
            case 0x00000002:
            case 0x00000004:
            case 0x00000008:
                return true;
            default:
                return false;
            }
        }

        bool IsFnButtonCode(std::uint32_t code)
        {
            return code == 0x00100000 || code == 0x00200000;
        }

        bool ContainsFnWithFace(const std::vector<std::uint32_t>& buttons)
        {
            bool hasFace = false;
            bool hasFn = false;
            for (const auto code : buttons) {
                hasFace = hasFace || IsFaceButtonCode(code);
                if (IsFnButtonCode(code)) {
                    hasFn = true;
                }
            }
            return hasFace && hasFn;
        }

        bool ParseButtonChord(std::string_view chord, std::vector<std::uint32_t>& outButtons)
        {
            outButtons.clear();
            std::size_t tokenStart = 0;
            while (tokenStart <= chord.size()) {
                const auto plusPos = chord.find('+', tokenStart);
                const auto tokenView = plusPos == std::string_view::npos ?
                    chord.substr(tokenStart) :
                    chord.substr(tokenStart, plusPos - tokenStart);

                const auto token = dualpad::input::ini::Trim(std::string(tokenView));
                if (token.empty()) {
                    return false;
                }

                const auto code = ButtonNameToCode(token);
                if (code == 0) {
                    return false;
                }
                if (std::find(outButtons.begin(), outButtons.end(), code) != outButtons.end()) {
                    return false;
                }
                outButtons.push_back(code);

                if (plusPos == std::string_view::npos) {
                    break;
                }
                tokenStart = plusPos + 1;
            }
            return !outButtons.empty();
        }

        struct TriggerKeyShape
        {
            std::string type;
            std::string body;
        };

        std::optional<TriggerKeyShape> SplitTriggerKey(std::string_view rawKey)
        {
            const auto key = dualpad::input::ini::Trim(std::string(rawKey));
            if (key.empty()) {
                return std::nullopt;
            }
            const auto pos = key.find(':');
            if (pos == std::string::npos) {
                return std::nullopt;
            }
            TriggerKeyShape shape{};
            shape.type = dualpad::input::ini::Trim(key.substr(0, pos));
            shape.body = dualpad::input::ini::Trim(key.substr(pos + 1));
            if (shape.type.empty() || shape.body.empty()) {
                return std::nullopt;
            }
            return shape;
        }

        ValidationResult Fail(std::string message)
        {
            ValidationResult r{};
            r.ok = false;
            r.message = std::move(message);
            return r;
        }

        ValidationResult Ok()
        {
            ValidationResult r{};
            r.ok = true;
            r.message = "ok";
            return r;
        }
    }

    ValidationResult ManifestValidator::ValidateImportedAst(const LegacyImportBundle& imported)
    {
        // Treat missing config files as non-fatal here; compile stage decides whether built-in defaults are allowed.
        // We still validate any parsed sections that exist.

        for (const auto& section : imported.bindings.sections) {
            const auto sectionName = NormalizeSectionName(section.name);
            if (sectionName.empty()) {
                return Fail("bindings ini contains an empty section name");
            }

            const auto sectionNameLower = dualpad::input::ini::ToLower(sectionName);

            std::unordered_set<std::string> seenKeys;
            bool seenInherit = false;

            for (const auto& kv : section.entries) {
                const auto key = NormalizeKey(kv.key);
                if (key.empty()) {
                    continue;
                }

                // Fail-closed: same normalized binding key must not appear more than once in a section.
                // This freezes the "duplicate binding key must fail" rule in Phase 1.
                if (!seenKeys.insert(key).second) {
                    return Fail(std::format(
                        "duplicate binding key '{}' in section [{}] at {}:{}",
                        key,
                        sectionName,
                        kv.span.path.string(),
                        kv.span.line));
                }

                // [Touchpad] is configuration, not a binding section.
                // Phase 1 freezes that Touchpad config compiles into the manifest, but imported-AST validation
                // must not treat Touchpad entries as trigger->action bindings.
                if (sectionNameLower == "touchpad") {
                    const auto k = dualpad::input::ini::ToLower(key);
                    if (k == "inherit") {
                        return Fail(std::format("Touchpad section must not contain Inherit key at {}:{}", kv.span.path.string(), kv.span.line));
                    }
                    if (k == "mode" || k == "edgethreshold" || k == "leftrightboundary" || k == "slidethreshold") {
                        if (NormalizeKey(kv.value).empty()) {
                            return Fail(std::format("empty Touchpad value for '{}' at {}:{}", key, kv.span.path.string(), kv.span.line));
                        }
                        continue;
                    }
                    return Fail(std::format("unknown Touchpad key '{}' at {}:{}", key, kv.span.path.string(), kv.span.line));
                }

                if (key == "Inherit") {
                    if (seenInherit) {
                        return Fail(std::format("duplicate Inherit key in section [{}]", sectionName));
                    }
                    seenInherit = true;
                    if (NormalizeKey(kv.value).empty()) {
                        return Fail(std::format("empty Inherit value in section [{}]", sectionName));
                    }
                    continue;
                }

                const auto actionId = dualpad::input::ini::Trim(kv.value);
                if (actionId.empty()) {
                    return Fail(std::format("empty action id for '{}' in section [{}]", key, sectionName));
                }
                if (!dualpad::input_v2::actions::ActionManifest::IsKnownActionId(actionId)) {
                    return Fail(std::format("unknown action id '{}' in section [{}]", actionId, sectionName));
                }

                // Frozen Layer/Combo chord semantics checks.
                const auto shape = SplitTriggerKey(key);
                if (!shape) {
                    return Fail(std::format("invalid trigger key '{}' in section [{}]", key, sectionName));
                }

                if (shape->type == "Combo" || shape->type == "Layer") {
                    std::vector<std::uint32_t> buttons;
                    if (!ParseButtonChord(shape->body, buttons)) {
                        return Fail(std::format("invalid {} chord '{}' in section [{}]", shape->type, shape->body, sectionName));
                    }
                    for (const auto code : buttons) {
                        if (!IsSyntheticPadBitCode(code)) {
                            return Fail(std::format("{} chord '{}' contains non-digital button in section [{}]", shape->type, shape->body, sectionName));
                        }
                    }
                    if (ContainsFnWithFace(buttons)) {
                        return Fail(std::format("{} chord '{}' mixes Fn and face buttons in section [{}]", shape->type, shape->body, sectionName));
                    }
                    if (shape->type == "Combo" && buttons.size() != 2) {
                        return Fail(std::format("Combo chord '{}' must have exactly 2 buttons in section [{}]", shape->body, sectionName));
                    }
                    if (shape->type == "Layer" && buttons.size() < 2) {
                        return Fail(std::format("Layer chord '{}' must have at least 2 buttons in section [{}]", shape->body, sectionName));
                    }
                }
            }
        }

        // Menu policy: duplicate keys in imported sections are intentionally allowed in INI,
        // but Phase 1 compile will fail-closed on conflicting duplicates at catalog compile time.
        return Ok();
    }

    ValidationResult ManifestValidator::ValidateCompiledBundle(
        const context::CompiledContextCatalog& catalog,
        const actions::CompiledActionManifest& manifest)
    {
        if (catalog.manifestEpoch != manifest.manifestEpoch) {
            return Fail(std::format("catalog epoch {} != manifest epoch {}", catalog.manifestEpoch, manifest.manifestEpoch));
        }
        if (manifest.legacyBindingProjection.manifestEpoch != manifest.manifestEpoch) {
            return Fail(std::format(
                "legacyBindingProjection epoch {} != manifest epoch {}",
                manifest.legacyBindingProjection.manifestEpoch,
                manifest.manifestEpoch));
        }
        if (manifest.actions.empty()) {
            return Fail("manifest actions table must not be empty");
        }
        if (manifest.outputDescriptors.empty()) {
            return Fail("manifest outputDescriptors table must not be empty");
        }
        if (manifest.policies.empty()) {
            return Fail("manifest policies table must not be empty");
        }

        std::unordered_set<std::string> outputDescriptorIds;
        for (const auto& descriptor : manifest.outputDescriptors) {
            if (descriptor.id.empty() || descriptor.kind.empty() || descriptor.target.empty()) {
                return Fail("manifest output descriptor entries must have id/kind/target");
            }
            if (!outputDescriptorIds.insert(descriptor.id).second) {
                return Fail(std::format("duplicate output descriptor id '{}'", descriptor.id));
            }
        }

        std::unordered_set<std::string> actionIds;
        for (const auto& action : manifest.actions) {
            if (action.id.empty() ||
                action.valueKind == actions::ActionValueKind::Unknown ||
                action.contract.empty() ||
                action.outputDescriptorId.empty() ||
                action.promptHintId.empty() ||
                action.domain == actions::ActionDomain::Unknown) {
                return Fail(std::format("action '{}' has incomplete metadata", action.id));
            }
            if (!outputDescriptorIds.contains(action.outputDescriptorId)) {
                return Fail(std::format(
                    "action '{}' references unknown output descriptor '{}'",
                    action.id,
                    action.outputDescriptorId));
            }
            if (!actionIds.insert(action.id).second) {
                return Fail(std::format("duplicate action id '{}'", action.id));
            }
        }

        std::unordered_set<std::string> baseSets;
        for (const auto& id : manifest.actionSets) {
            if (id.empty()) {
                return Fail("manifest contains empty base set id");
            }
            baseSets.insert(id);
        }

        // Phase 1 hard constraint: base sets are frozen to exactly GameplayBase and MenuBase.
        {
            constexpr const char* kGameplayBase = "GameplayBase";
            constexpr const char* kMenuBase = "MenuBase";
            if (baseSets.size() != 2 || !baseSets.contains(kGameplayBase) || !baseSets.contains(kMenuBase)) {
                return Fail("manifest base set registry must be exactly {GameplayBase, MenuBase} in Phase 1");
            }
        }

        std::unordered_set<std::string> layers;
        for (const auto& id : manifest.actionLayers) {
            if (id.empty()) {
                return Fail("manifest contains empty layer id");
            }
            layers.insert(id);
        }

        for (const auto& binding : manifest.bindings) {
            if (!actionIds.contains(binding.actionId)) {
                return Fail(std::format("binding references unknown action '{}'", binding.actionId));
            }
            if (!baseSets.contains(binding.baseSetId)) {
                return Fail(std::format(
                    "binding for action '{}' references unknown base set '{}'",
                    binding.actionId,
                    binding.baseSetId));
            }
            if (binding.layerId && !layers.contains(*binding.layerId)) {
                return Fail(std::format(
                    "binding for action '{}' references unknown layer '{}'",
                    binding.actionId,
                    *binding.layerId));
            }
            if (binding.controlPath.empty() || binding.interaction.empty() || binding.deviceFamily.empty()) {
                return Fail(std::format("binding for action '{}' has incomplete lowering metadata", binding.actionId));
            }
        }

        for (const auto& display : manifest.displayBindings) {
            if (!actionIds.contains(display.actionId)) {
                return Fail(std::format("display binding references unknown action '{}'", display.actionId));
            }
            if (!baseSets.contains(display.baseSetId)) {
                return Fail(std::format(
                    "display binding for action '{}' references unknown base set '{}'",
                    display.actionId,
                    display.baseSetId));
            }
            if (display.layerId && !layers.contains(*display.layerId)) {
                return Fail(std::format(
                    "display binding for action '{}' references unknown layer '{}'",
                    display.actionId,
                    *display.layerId));
            }
            if (display.controlPath.empty() || display.interaction.empty() || display.deviceFamily.empty()) {
                return Fail(std::format("display binding for action '{}' has incomplete metadata", display.actionId));
            }
        }

        for (const auto& entry : catalog.entries) {
            if (entry.presentationPolicyId.empty()) {
                return Fail(std::format(
                    "catalog entry '{}' missing presentationPolicyId",
                    entry.canonicalContextName));
            }

            if (entry.defaultActionSetId) {
                if (!baseSets.contains(*entry.defaultActionSetId)) {
                    return Fail(std::format(
                        "catalog entry '{}' defaultActionSetId '{}' not in manifest base set registry",
                        entry.canonicalContextName,
                        *entry.defaultActionSetId));
                }

                for (const auto& layerId : entry.defaultLayerIds) {
                    if (!layers.contains(layerId)) {
                        return Fail(std::format(
                            "catalog entry '{}' references unknown layer '{}'",
                            entry.canonicalContextName,
                            layerId));
                    }
                }

                // scopeAnchorIds must equal [baseSetId] + defaultLayerIds in order.
                if (entry.scopeAnchorIds.size() != 1 + entry.defaultLayerIds.size()) {
                    return Fail(std::format(
                        "catalog entry '{}' scopeAnchorIds size {} does not match base+layers size {}",
                        entry.canonicalContextName,
                        entry.scopeAnchorIds.size(),
                        1 + entry.defaultLayerIds.size()));
                }
                if (entry.scopeAnchorIds.empty() || entry.scopeAnchorIds[0] != *entry.defaultActionSetId) {
                    return Fail(std::format(
                        "catalog entry '{}' scopeAnchorIds[0] must equal defaultActionSetId",
                        entry.canonicalContextName));
                }
                for (std::size_t i = 0; i < entry.defaultLayerIds.size(); ++i) {
                    if (entry.scopeAnchorIds[i + 1] != entry.defaultLayerIds[i]) {
                        return Fail(std::format(
                            "catalog entry '{}' scopeAnchorIds mismatch at index {}",
                            entry.canonicalContextName,
                            i + 1));
                    }
                }
            } else {
                if (!entry.defaultLayerIds.empty() || !entry.scopeAnchorIds.empty()) {
                    return Fail(std::format(
                        "catalog entry '{}' has layers/scope anchors but no defaultActionSetId",
                        entry.canonicalContextName));
                }
            }
        }

        return Ok();
    }

    ValidationResult ManifestValidator::ValidateCompiledBundle(const CompiledConfigBundle& bundle)
    {
        return ValidateCompiledBundle(bundle.catalog, bundle.manifest);
    }
}
