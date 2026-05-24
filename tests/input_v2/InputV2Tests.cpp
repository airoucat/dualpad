#include "pch.h"

#include "input/Trigger.h"
#include "input_v2/actions/CompiledActionGraph.h"
#include "input_v2/actions/CompiledActionGraphPublisher.h"
#include "input_v2/actions/InteractionEngine.h"

#include <iostream>
#include <stdexcept>
#include <string_view>

namespace
{
    namespace actions = dualpad::input_v2::actions;

    void Require(bool condition, std::string_view message)
    {
        if (!condition) {
            throw std::runtime_error(std::string(message));
        }
    }

    dualpad::input::Trigger Trigger(
        dualpad::input::TriggerType type,
        std::uint32_t code,
        std::vector<std::uint32_t> modifiers = {})
    {
        return dualpad::input::Trigger{
            .type = type,
            .code = code,
            .modifiers = std::move(modifiers)
        };
    }

    actions::CompiledActionManifest ManifestWithActions()
    {
        actions::CompiledActionManifest manifest{};
        manifest.manifestEpoch = 42;
        manifest.actions = {
            actions::ActionDefinition{ .id = "Jump", .valueKind = actions::ActionValueKind::Digital },
            actions::ActionDefinition{ .id = "PowerAttack", .valueKind = actions::ActionValueKind::Digital },
            actions::ActionDefinition{ .id = "NativeCombo", .valueKind = actions::ActionValueKind::Digital },
            actions::ActionDefinition{ .id = "LookX", .valueKind = actions::ActionValueKind::Axis1D }
        };
        return manifest;
    }

    actions::CompiledBinding Binding(
        std::string actionId,
        dualpad::input::Trigger legacyTrigger,
        std::string baseSetId = "GameplayBase")
    {
        actions::CompiledBinding binding{};
        binding.actionId = std::move(actionId);
        binding.baseSetId = std::move(baseSetId);
        binding.legacyTrigger = std::move(legacyTrigger);
        return binding;
    }

    actions::ControlSample Sample(
        actions::ControlPath path,
        bool down,
        bool pressed,
        bool released,
        std::uint64_t downAtUs,
        std::uint64_t now)
    {
        return actions::ControlSample{
            .path = path,
            .down = down,
            .pressed = pressed,
            .released = released,
            .scalar = down ? 1.0f : 0.0f,
            .downAtUs = downAtUs,
            .timestampUs = now
        };
    }

    void RunActionGraphCompilerTests()
    {
        {
            auto manifest = ManifestWithActions();
            manifest.bindings.push_back(Binding("Jump", Trigger(dualpad::input::TriggerType::Button, 10)));
            manifest.bindings.push_back(Binding("PowerAttack", Trigger(dualpad::input::TriggerType::Layer, 11, { 1 })));
            manifest.bindings.push_back(Binding("NativeCombo", Trigger(dualpad::input::TriggerType::Combo, 3, { 2 })));
            manifest.bindings.push_back(Binding("LookX", Trigger(dualpad::input::TriggerType::Axis, 100)));
            manifest.displayBindings.push_back(actions::DisplayBinding{
                .actionId = "LookX",
                .baseSetId = "GameplayBase",
                .controlPath = "Right Stick X",
                .interaction = "priority:3"
            });

            const auto compiled = actions::ActionGraphCompiler::Compile(manifest);
            Require(compiled.ok, compiled.message);
            Require(compiled.graph.manifestEpoch == 42, "compiled graph must carry PH1 manifest epoch");
            Require(compiled.graph.bindings.size() == 4, "compiled graph must include all manifest bindings");

            const auto& button = compiled.graph.bindings[0];
            Require(
                button.matchPolicy == actions::BindingMatchPolicy::PreferExactThenSubset,
                "Button lowering must preserve legacy subset fallback only for button-like bindings");

            const auto& layer = compiled.graph.bindings[1];
            Require(layer.interaction.kind == actions::InteractionKind::Press, "Layer is not a new InteractionKind");
            Require(layer.interaction.requiredPathIndices.size() == 1, "Layer must lower to required ControlPath constraints");
            Require(layer.matchPolicy == actions::BindingMatchPolicy::ExactOnly, "Layer must be ExactOnly");
            Require(layer.interaction.chordWindowUs == 0, "Layer must not invent a combo timing window");

            const auto& combo = compiled.graph.bindings[2];
            Require(combo.interaction.kind == actions::InteractionKind::Chord, "Combo must lower to Chord");
            Require(combo.interaction.primaryPathIndex == 1, "Combo primary path must be the final legacy participant");
            Require(combo.interaction.unordered, "Combo must be unordered");
            Require(combo.interaction.chordWindowUs == actions::kLegacyComboWindowUs, "Combo must use the frozen legacy window");
            Require(!compiled.graph.displayBindings[2].legacyTokenRenderable, "Combo display must mark legacy token bridge limitations");

            Require(
                compiled.graph.displayBindings[3].mode == actions::DisplayBindingMode::Primary,
                "Explicit manifest display token must make an axis display binding visible");
            Require(compiled.graph.displayBindings[3].token == "Right Stick X", "DisplayBinding token must come from manifest metadata");
        }

        {
            auto manifest = ManifestWithActions();
            manifest.bindings.push_back(Binding("MissingAction", Trigger(dualpad::input::TriggerType::Button, 10)));
            const auto compiled = actions::ActionGraphCompiler::Compile(manifest);
            Require(!compiled.ok, "unknown action must fail closed");
        }

        {
            auto manifest = ManifestWithActions();
            manifest.bindings.push_back(Binding("NativeCombo", Trigger(dualpad::input::TriggerType::Combo, 3, { 1, 2 })));
            const auto compiled = actions::ActionGraphCompiler::Compile(manifest);
            Require(!compiled.ok, "three-key combo must fail closed in PH4");
        }

        {
            auto manifest = ManifestWithActions();
            manifest.bindings.push_back(Binding("Jump", Trigger(dualpad::input::TriggerType::Button, 10)));
            manifest.bindings.push_back(Binding("Jump", Trigger(dualpad::input::TriggerType::Hold, 11)));
            manifest.displayBindings.push_back(actions::DisplayBinding{
                .actionId = "Jump",
                .baseSetId = "GameplayBase",
                .controlPath = "Face Button",
                .interaction = "priority:0"
            });
            const auto compiled = actions::ActionGraphCompiler::Compile(manifest);
            Require(!compiled.ok, "display binding priority conflict must fail closed");
        }
    }

    void RunCompiledActionGraphPublisherTests()
    {
        actions::CompiledActionGraph graph{};
        graph.manifestEpoch = 42;

        actions::CompiledActionGraphPublisher publisher;
        auto mismatch = publisher.Publish(graph, 41);
        Require(!mismatch.ok, "publisher must fail closed on manifest epoch mismatch");
        Require(!publisher.GetActiveGraph(), "failed publish must not mutate active graph");

        auto published = publisher.Publish(graph, 42);
        Require(published.ok, "publisher must accept matching manifest epoch");
        Require(publisher.GetActiveGraph() == published.graph, "publisher must hot-swap the active immutable graph through Publish");
        Require(publisher.GetActiveManifestEpoch() == 42, "publisher must expose active manifest epoch");
    }

    void RunInteractionEngineTests()
    {
        auto manifest = ManifestWithActions();
        manifest.bindings.push_back(Binding("PowerAttack", Trigger(dualpad::input::TriggerType::Layer, 11, { 1 })));
        manifest.bindings.push_back(Binding("NativeCombo", Trigger(dualpad::input::TriggerType::Combo, 3, { 2 })));

        const auto compiled = actions::ActionGraphCompiler::Compile(manifest);
        Require(compiled.ok, compiled.message);

        actions::ActionSetStack stack{};
        stack.baseSetId = "GameplayBase";

        actions::InteractionEngine engine;
        actions::InteractionStateStore state;

        {
            actions::InteractionInputFrame frame{};
            frame.manifestEpoch = 42;
            frame.contextRevision = 7;
            frame.monotonicUs = 1'000;
            frame.samples = {
                Sample(actions::ControlPath{ .kind = actions::ControlPathKind::DigitalButton, .code = 1 }, true, false, false, 500, 1'000),
                Sample(actions::ControlPath{ .kind = actions::ControlPathKind::DigitalButton, .code = 11 }, true, true, false, 1'000, 1'000)
            };

            const auto resolved = engine.Resolve(compiled.graph, stack, frame, state);
            Require(resolved.changes.size() == 1, "Layer must fire when required path is already down before primary");
            Require(resolved.changes[0].actionId == "PowerAttack", "Layer must resolve to its action");
            Require(resolved.changes[0].phase == actions::ActionPhase::Press, "Layer press must emit Press");
        }

        state.Reset();
        {
            actions::InteractionInputFrame frame{};
            frame.manifestEpoch = 42;
            frame.contextRevision = 7;
            frame.monotonicUs = 2'000;
            frame.samples = {
                Sample(actions::ControlPath{ .kind = actions::ControlPathKind::DigitalButton, .code = 2 }, true, false, false, 1'950, 2'000),
                Sample(actions::ControlPath{ .kind = actions::ControlPathKind::DigitalButton, .code = 3 }, true, true, false, 2'000, 2'000)
            };

            const auto resolved = engine.Resolve(compiled.graph, stack, frame, state);
            Require(resolved.changes.size() == 1, "Combo must fire when final legacy participant arrives second");
            Require(resolved.changes[0].actionId == "NativeCombo", "Combo must resolve to its action");
            Require(resolved.changes[0].phase == actions::ActionPhase::Pulse, "Combo must emit a pulse");
        }

        state.Reset();
        {
            actions::InteractionInputFrame frame{};
            frame.manifestEpoch = 42;
            frame.contextRevision = 7;
            frame.monotonicUs = 3'000;
            frame.samples = {
                Sample(actions::ControlPath{ .kind = actions::ControlPathKind::DigitalButton, .code = 2 }, true, true, false, 3'000, 3'000),
                Sample(actions::ControlPath{ .kind = actions::ControlPathKind::DigitalButton, .code = 3 }, true, false, false, 2'950, 3'000)
            };

            const auto resolved = engine.Resolve(compiled.graph, stack, frame, state);
            Require(resolved.changes.size() == 1, "Combo must be unordered and fire when required participant arrives second");
            Require(resolved.changes[0].actionId == "NativeCombo", "unordered combo must still resolve to combo action");
        }

        state.Reset();
        {
            actions::InteractionInputFrame frame{};
            frame.manifestEpoch = 41;
            frame.contextRevision = 7;
            frame.monotonicUs = 4'000;
            frame.samples = {
                Sample(actions::ControlPath{ .kind = actions::ControlPathKind::DigitalButton, .code = 1 }, true, false, false, 3'500, 4'000),
                Sample(actions::ControlPath{ .kind = actions::ControlPathKind::DigitalButton, .code = 11 }, true, true, false, 4'000, 4'000)
            };

            const auto resolved = engine.Resolve(compiled.graph, stack, frame, state);
            Require(resolved.changes.empty(), "InteractionEngine must fail closed on manifest epoch mismatch");
        }
    }
}

int main()
{
    try {
        RunActionGraphCompilerTests();
        RunCompiledActionGraphPublisherTests();
        RunInteractionEngineTests();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
