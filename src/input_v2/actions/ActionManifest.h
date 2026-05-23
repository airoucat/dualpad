#pragma once

#include "input/InputContext.h"
#include "input/Trigger.h"
#include "input/mapping/TouchpadMapper.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace dualpad::input_v2::config
{
    struct LegacyBindingsAst;
}

namespace dualpad::input_v2::context
{
    struct CompiledContextCatalog;
}

namespace dualpad::input_v2::actions
{
    enum class ActionValueKind : std::uint8_t
    {
        Digital = 0,
        Axis1D,
        Axis2D,
        Gesture,
        Unknown
    };

    enum class ActionDomain : std::uint8_t
    {
        Gameplay = 0,
        Menu,
        Utility,
        Unknown
    };

    struct ActionDefinition
    {
        std::string id;
        ActionValueKind valueKind{ ActionValueKind::Unknown };
        std::string contract;
        std::string outputDescriptorId;
        std::string promptHintId;
        ActionDomain domain{ ActionDomain::Unknown };
    };

    struct OutputDescriptor
    {
        std::string id;
        std::string kind;
        std::string target;
    };

    struct ManifestPolicy
    {
        std::string id;
        std::string value;
    };

    struct CompiledBinding
    {
        std::string actionId;
        std::string baseSetId;
        std::optional<std::string> layerId;
        std::string deviceFamily{ "DualSense" };

        std::string controlPath;
        std::string interaction;
        std::vector<std::string> requiredChordPaths; // for Layer semantics

        // Phase 1 compatibility: normalized legacy Trigger used by BindingManager.
        dualpad::input::Trigger legacyTrigger{};
        dualpad::input::InputContext legacyContext{ dualpad::input::InputContext::Unknown };
    };

    struct DisplayBinding
    {
        std::string actionId;
        std::string baseSetId;
        std::optional<std::string> layerId;
        std::string deviceFamily{ "DualSense" };
        std::string controlPath;
        std::string interaction;

        // Phase 1 compatibility projection fields.
        dualpad::input::Trigger legacyTrigger{};
        dualpad::input::InputContext legacyContext{ dualpad::input::InputContext::Unknown };
    };

    struct ProjectedLegacyBinding
    {
        dualpad::input::InputContext context{ dualpad::input::InputContext::Unknown };
        dualpad::input::Trigger trigger{};
        std::string actionId;
    };

    struct LegacyBindingProjection
    {
        std::uint64_t manifestEpoch{ 0 };
        std::vector<ProjectedLegacyBinding> bindings;
        std::vector<ProjectedLegacyBinding> displayBindings;
        dualpad::input::TouchpadConfig touchpadConfig{};
    };

    struct CompiledActionManifest
    {
        std::uint64_t manifestEpoch{ 0 };

        std::vector<ActionDefinition> actions;
        std::vector<std::string> actionSets;   // base set registry
        std::vector<std::string> actionLayers; // layer registry
        std::vector<CompiledBinding> bindings;
        std::vector<DisplayBinding> displayBindings;
        std::vector<OutputDescriptor> outputDescriptors;
        std::vector<ManifestPolicy> policies;
        dualpad::input::TouchpadConfig touchpadConfig{};

        LegacyBindingProjection legacyBindingProjection;
    };

    struct ActionManifestCompileResult
    {
        bool ok{ false };
        std::string message;
        CompiledActionManifest manifest;
    };

    class ActionManifest
    {
    public:
        static ActionManifestCompileResult Compile(
            const dualpad::input_v2::context::CompiledContextCatalog& compiledCatalog,
            const dualpad::input_v2::config::LegacyBindingsAst& importedBindings,
            std::uint64_t manifestEpoch);

        static bool IsKnownActionId(std::string_view actionId);
    };
}
