#pragma once

#include "input_v2/actions/ActionManifest.h"
#include "input_v2/actions/InteractionSpec.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace dualpad::input_v2::actions
{
    using ActionId = std::string;
    using BindingId = std::uint32_t;

    struct DisplayBindingRecord
    {
        BindingId bindingId{ 0 };
        DisplayBindingMode mode{ DisplayBindingMode::Primary };
        std::string token;
        std::string localizedLabel;
        std::uint16_t priority{ 0 };
        std::string deviceProfile{ "DualSense" };
        bool legacyTokenRenderable{ true };

        friend bool operator==(const DisplayBindingRecord&, const DisplayBindingRecord&) = default;
    };

    struct CompiledGraphBinding
    {
        BindingId bindingId{ 0 };
        ActionId actionId;
        std::string actionSetId;
        std::vector<ControlPath> paths;
        std::vector<BindingModifier> modifiers;
        InteractionSpec interaction;
        BindingMatchPolicy matchPolicy{ BindingMatchPolicy::ExactOnly };
        BindingId primaryDisplayBindingId{ 0 };
        std::string legacyOrigin;

        friend bool operator==(const CompiledGraphBinding&, const CompiledGraphBinding&) = default;
    };

    struct CompiledBindingLookupTables
    {
        std::unordered_map<BindingId, std::size_t> bindingIndexById;
        std::unordered_map<ActionId, std::vector<BindingId>> bindingIdsByActionId;
        std::unordered_map<std::string, std::vector<BindingId>> bindingIdsByActionSetId;
    };

    struct CompiledActionGraph
    {
        std::uint64_t manifestEpoch{ 0 };
        std::vector<ActionDefinition> actions;
        std::vector<CompiledGraphBinding> bindings;
        std::vector<DisplayBindingRecord> displayBindings;
        CompiledBindingLookupTables lookups;

        [[nodiscard]] const CompiledGraphBinding* FindBinding(BindingId bindingId) const;
        [[nodiscard]] std::vector<const CompiledGraphBinding*> BindingsForActionSet(
            const std::string& actionSetId,
            const std::vector<std::string>& layerIds) const;
    };

    struct ActionGraphCompileResult
    {
        bool ok{ false };
        std::string message;
        CompiledActionGraph graph;
    };

    class ActionGraphCompiler
    {
    public:
        static ActionGraphCompileResult Compile(const CompiledActionManifest& manifest);
    };
}
