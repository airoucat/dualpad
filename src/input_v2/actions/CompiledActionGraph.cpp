#include "pch.h"

#include "input_v2/actions/CompiledActionGraph.h"

#include "input/Trigger.h"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace dualpad::input_v2::actions
{
    namespace
    {
        struct LoweredLegacyBinding
        {
            std::vector<ControlPath> paths;
            InteractionSpec interaction;
            BindingMatchPolicy matchPolicy{ BindingMatchPolicy::ExactOnly };
            DisplayBindingMode defaultDisplayMode{ DisplayBindingMode::Primary };
            bool legacyTokenRenderable{ true };
        };

        struct DuplicateShapeOwner
        {
            std::string actionId;
            std::string legacyOrigin;
        };

        ControlPath DigitalPath(std::uint32_t code)
        {
            return ControlPath{
                .kind = ControlPathKind::DigitalButton,
                .code = code,
                .component = AxisComponent::None
            };
        }

        ControlPath AxisPath(std::uint32_t code)
        {
            return ControlPath{
                .kind = ControlPathKind::AnalogAxis1D,
                .code = code,
                .component = AxisComponent::None
            };
        }

        std::string JoinPathToken(const std::vector<ControlPath>& paths)
        {
            std::ostringstream out;
            for (std::size_t i = 0; i < paths.size(); ++i) {
                if (i > 0) {
                    out << '+';
                }
                out << ToDebugString(paths[i]);
            }
            return out.str();
        }

        bool LowerLegacyTrigger(
            const dualpad::input::Trigger& trigger,
            LoweredLegacyBinding& lowered,
            std::string& error)
        {
            lowered = {};
            using dualpad::input::TriggerType;

            switch (trigger.type) {
            case TriggerType::Button:
                lowered.paths = { DigitalPath(trigger.code) };
                lowered.interaction.kind = InteractionKind::Press;
                lowered.matchPolicy = BindingMatchPolicy::PreferExactThenSubset;
                return true;
            case TriggerType::Hold:
                lowered.paths = { DigitalPath(trigger.code) };
                lowered.interaction.kind = InteractionKind::Hold;
                lowered.interaction.holdThresholdUs = kLegacyHoldThresholdUs;
                lowered.matchPolicy = BindingMatchPolicy::PreferExactThenSubset;
                return true;
            case TriggerType::Tap:
                lowered.paths = { DigitalPath(trigger.code) };
                lowered.interaction.kind = InteractionKind::Tap;
                lowered.interaction.tapMaxUs = kLegacyTapThresholdUs;
                lowered.matchPolicy = BindingMatchPolicy::PreferExactThenSubset;
                return true;
            case TriggerType::Layer:
                lowered.paths.reserve(trigger.modifiers.size() + 1);
                for (const auto modifier : trigger.modifiers) {
                    lowered.paths.push_back(DigitalPath(modifier));
                    lowered.interaction.requiredPathIndices.push_back(
                        static_cast<std::uint16_t>(lowered.paths.size() - 1));
                }
                lowered.paths.push_back(DigitalPath(trigger.code));
                lowered.interaction.kind = InteractionKind::Press;
                lowered.interaction.primaryPathIndex = static_cast<std::uint16_t>(lowered.paths.size() - 1);
                lowered.matchPolicy = BindingMatchPolicy::ExactOnly;
                if (lowered.interaction.requiredPathIndices.empty()) {
                    error = "Layer binding must have at least one required path";
                    return false;
                }
                return true;
            case TriggerType::Combo:
                if (trigger.modifiers.size() != 1) {
                    error = "Combo binding must contain exactly two digital buttons";
                    return false;
                }
                lowered.paths = { DigitalPath(trigger.modifiers.front()), DigitalPath(trigger.code) };
                lowered.interaction.kind = InteractionKind::Chord;
                lowered.interaction.primaryPathIndex = 1;
                lowered.interaction.requiredPathIndices = { 0 };
                lowered.interaction.unordered = true;
                lowered.interaction.chordWindowUs = kLegacyComboWindowUs;
                lowered.matchPolicy = BindingMatchPolicy::ExactOnly;
                lowered.legacyTokenRenderable = false;
                return true;
            case TriggerType::Gesture:
                lowered.paths = {
                    ControlPath{
                        .kind = ControlPathKind::TouchGesture,
                        .code = trigger.code,
                        .component = AxisComponent::None
                    }
                };
                lowered.interaction.kind = InteractionKind::Gesture;
                lowered.defaultDisplayMode = DisplayBindingMode::Hidden;
                lowered.matchPolicy = BindingMatchPolicy::ExactOnly;
                return true;
            case TriggerType::Axis:
                lowered.paths = { AxisPath(trigger.code) };
                lowered.interaction.kind = InteractionKind::Value;
                lowered.defaultDisplayMode = DisplayBindingMode::Hidden;
                lowered.matchPolicy = BindingMatchPolicy::ExactOnly;
                return true;
            default:
                error = "Unknown legacy trigger type";
                return false;
            }
        }

        std::string BindingShapeKey(const CompiledGraphBinding& binding)
        {
            std::ostringstream out;
            out << binding.actionSetId << '|';

            auto shapePaths = binding.paths;
            if (binding.interaction.kind == InteractionKind::Chord && binding.interaction.unordered) {
                (std::sort)(shapePaths.begin(), shapePaths.end(), [](const auto& a, const auto& b) {
                    if (a.kind != b.kind) {
                        return static_cast<std::uint8_t>(a.kind) < static_cast<std::uint8_t>(b.kind);
                    }
                    if (a.code != b.code) {
                        return a.code < b.code;
                    }
                    return static_cast<std::uint8_t>(a.component) < static_cast<std::uint8_t>(b.component);
                });
            }

            for (const auto& path : shapePaths) {
                out << static_cast<int>(path.kind) << ':' << path.code << ':' << static_cast<int>(path.component) << ';';
            }
            out << '|' << static_cast<int>(binding.interaction.kind);
            for (const auto required : binding.interaction.requiredPathIndices) {
                out << ':' << required;
            }
            return out.str();
        }

        std::optional<dualpad::input_v2::actions::DisplayBinding> FindManifestDisplayBinding(
            const CompiledActionManifest& manifest,
            const CompiledBinding& binding)
        {
            const auto found = std::find_if(
                manifest.displayBindings.begin(),
                manifest.displayBindings.end(),
                [&](const DisplayBinding& display) {
                    return display.actionId == binding.actionId &&
                        display.baseSetId == binding.baseSetId &&
                        display.layerId == binding.layerId &&
                        display.deviceFamily == binding.deviceFamily;
                });
            if (found == manifest.displayBindings.end()) {
                return std::nullopt;
            }
            return *found;
        }
    }

    const CompiledGraphBinding* CompiledActionGraph::FindBinding(BindingId bindingId) const
    {
        const auto it = lookups.bindingIndexById.find(bindingId);
        if (it == lookups.bindingIndexById.end() || it->second >= bindings.size()) {
            return nullptr;
        }
        return &bindings[it->second];
    }

    std::vector<const CompiledGraphBinding*> CompiledActionGraph::BindingsForActionSet(
        const std::string& actionSetId,
        const std::vector<std::string>& layerIds) const
    {
        std::vector<const CompiledGraphBinding*> result;
        const auto append = [&](const std::string& setId) {
            const auto it = lookups.bindingIdsByActionSetId.find(setId);
            if (it == lookups.bindingIdsByActionSetId.end()) {
                return;
            }
            for (const auto bindingId : it->second) {
                if (const auto* binding = FindBinding(bindingId)) {
                    result.push_back(binding);
                }
            }
        };

        append(actionSetId);
        for (const auto& layerId : layerIds) {
            append(layerId);
        }
        return result;
    }

    ActionGraphCompileResult ActionGraphCompiler::Compile(const CompiledActionManifest& manifest)
    {
        ActionGraphCompileResult result{};
        result.graph.manifestEpoch = manifest.manifestEpoch;
        result.graph.actions = manifest.actions;

        std::unordered_set<std::string> knownActions;
        for (const auto& action : manifest.actions) {
            knownActions.insert(action.id);
        }

        std::map<std::string, DuplicateShapeOwner> seenBindingShapes;
        std::set<std::string> displayPriorityKeys;
        BindingId nextBindingId = 1;

        for (const auto& manifestBinding : manifest.bindings) {
            if (!knownActions.contains(manifestBinding.actionId)) {
                result.message = "Action graph compile failed: unknown action '" + manifestBinding.actionId + "'";
                return result;
            }

            LoweredLegacyBinding lowered{};
            std::string error;
            if (!LowerLegacyTrigger(manifestBinding.legacyTrigger, lowered, error)) {
                result.message = "Action graph compile failed for action '" + manifestBinding.actionId + "': " + error;
                return result;
            }
            if (lowered.paths.empty()) {
                result.message = "Action graph compile failed: lowered binding has no ControlPath";
                return result;
            }

            CompiledGraphBinding binding{};
            binding.actionId = manifestBinding.actionId;
            binding.actionSetId = manifestBinding.layerId.value_or(manifestBinding.baseSetId);
            binding.paths = lowered.paths;
            binding.interaction = lowered.interaction;
            binding.matchPolicy = lowered.matchPolicy;
            binding.primaryDisplayBindingId = binding.bindingId;
            binding.legacyOrigin = std::string(dualpad::input::ToString(manifestBinding.legacyTrigger.type));

            const auto shapeKey = BindingShapeKey(binding);
            const auto duplicateIt = seenBindingShapes.find(shapeKey);
            if (duplicateIt != seenBindingShapes.end()) {
                const auto isComboDuplicate =
                    duplicateIt->second.legacyOrigin == "Combo" || binding.legacyOrigin == "Combo";
                if (isComboDuplicate || duplicateIt->second.actionId != binding.actionId) {
                    result.message = "Action graph compile failed: duplicate binding in action set '" + binding.actionSetId + "'";
                    return result;
                }

                // PH4 allows idempotent duplicates created by legacy context aliases collapsing into
                // the same ActionSetStack. They are not runtime conflict candidates.
                continue;
            }
            seenBindingShapes.emplace(shapeKey, DuplicateShapeOwner{
                .actionId = binding.actionId,
                .legacyOrigin = binding.legacyOrigin
            });
            binding.bindingId = nextBindingId++;

            const auto manifestDisplay = FindManifestDisplayBinding(manifest, manifestBinding);
            DisplayBindingRecord display{};
            display.bindingId = binding.bindingId;
            display.mode = lowered.defaultDisplayMode;
            display.priority = static_cast<std::uint16_t>(result.graph.displayBindings.size());
            display.deviceProfile = manifestBinding.deviceFamily;
            display.legacyTokenRenderable = lowered.legacyTokenRenderable;
            display.token = JoinPathToken(binding.paths);
            display.localizedLabel = display.token;

            if (manifestDisplay.has_value()) {
                if (!manifestDisplay->controlPath.empty()) {
                    display.token = manifestDisplay->controlPath;
                    display.localizedLabel = manifestDisplay->controlPath;
                    display.mode = DisplayBindingMode::Primary;
                    display.legacyTokenRenderable = true;
                }
                if (manifestDisplay->interaction == "hidden") {
                    display.mode = DisplayBindingMode::Hidden;
                }
                if (manifestDisplay->interaction.rfind("priority:", 0) == 0) {
                    try {
                        display.priority = static_cast<std::uint16_t>(std::stoul(manifestDisplay->interaction.substr(9)));
                    } catch (const std::exception&) {
                        result.message = "Action graph compile failed: invalid display binding priority";
                        return result;
                    }
                }
            }

            const auto priorityKey =
                binding.actionSetId + '|' + binding.actionId + '|' + std::to_string(display.priority) + '|' + std::string(ToString(display.mode));
            if (displayPriorityKeys.contains(priorityKey)) {
                result.message = "Action graph compile failed: display binding priority conflict";
                return result;
            }
            displayPriorityKeys.insert(priorityKey);

            result.graph.lookups.bindingIndexById[binding.bindingId] = result.graph.bindings.size();
            result.graph.lookups.bindingIdsByActionId[binding.actionId].push_back(binding.bindingId);
            result.graph.lookups.bindingIdsByActionSetId[binding.actionSetId].push_back(binding.bindingId);
            result.graph.displayBindings.push_back(display);
            result.graph.bindings.push_back(std::move(binding));
        }

        result.ok = true;
        result.message = "ok";
        return result;
    }
}
