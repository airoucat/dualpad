#include "pch.h"

#include "input/Trigger.h"
#include "input_v2/actions/CompiledActionGraph.h"
#include "input_v2/actions/CompiledActionGraphPublisher.h"
#include "input_v2/actions/InteractionEngine.h"
#include "input_v2/actions/LegacyInteractionInputAdapter.h"
#include "input_v2/actions/LegacyLifecycleBridge.h"
#include "input_v2/config/ActionManifestPublisher.h"
#include "input_v2/config/AtomicConfigReloader.h"

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

            auto holdTapGestureManifest = ManifestWithActions();
            holdTapGestureManifest.bindings.push_back(Binding("Jump", Trigger(dualpad::input::TriggerType::Hold, 10)));
            holdTapGestureManifest.bindings.push_back(Binding("PowerAttack", Trigger(dualpad::input::TriggerType::Tap, 11)));
            holdTapGestureManifest.bindings.push_back(Binding("NativeCombo", Trigger(dualpad::input::TriggerType::Gesture, 12)));
            const auto holdTapGesture = actions::ActionGraphCompiler::Compile(holdTapGestureManifest);
            Require(holdTapGesture.ok, holdTapGesture.message);
            Require(
                holdTapGesture.graph.bindings[0].matchPolicy == actions::BindingMatchPolicy::PreferExactThenSubset,
                "Hold lowering may use PreferExactThenSubset");
            Require(
                holdTapGesture.graph.bindings[1].matchPolicy == actions::BindingMatchPolicy::PreferExactThenSubset,
                "Tap lowering may use PreferExactThenSubset");
            Require(
                holdTapGesture.graph.bindings[2].matchPolicy == actions::BindingMatchPolicy::ExactOnly,
                "Gesture lowering must be ExactOnly");

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
            manifest.bindings.push_back(Binding("NativeCombo", Trigger(dualpad::input::TriggerType::Combo, 3, { 2 })));
            manifest.bindings.push_back(Binding("PowerAttack", Trigger(dualpad::input::TriggerType::Combo, 2, { 3 })));
            const auto compiled = actions::ActionGraphCompiler::Compile(manifest);
            Require(!compiled.ok, "reverse-order duplicate combo must fail closed as the same unordered shape");
        }

        {
            auto manifest = ManifestWithActions();
            manifest.bindings.push_back(Binding("Jump", Trigger(dualpad::input::TriggerType::Button, 10), "BookLayer"));
            manifest.bindings.push_back(Binding("Jump", Trigger(dualpad::input::TriggerType::Button, 10), "BookLayer"));
            const auto compiled = actions::ActionGraphCompiler::Compile(manifest);
            Require(compiled.ok, "same-action alias duplicate in a collapsed action set must be deduped");
            Require(compiled.graph.bindings.size() == 1, "same-action alias duplicate must not create two runtime bindings");
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

        auto& runtimeOwner = actions::CompiledActionGraphPublisher::GetRuntimeOwner();
        runtimeOwner.ResetForTests();
        dualpad::input_v2::config::ActionManifestPublisher::GetSingleton().ResetForTests();

        dualpad::input_v2::config::CompiledConfigBundle bundle{};
        bundle.manifestEpoch = 42;
        bundle.catalog.manifestEpoch = 42;
        bundle.manifest = ManifestWithActions();
        bundle.manifest.bindings.push_back(Binding("Jump", Trigger(dualpad::input::TriggerType::Button, 10)));
        bundle.manifest.legacyBindingProjection.manifestEpoch = 42;

        Require(
            dualpad::input_v2::config::ActionManifestPublisher::GetSingleton().PublishPromotedBundle(bundle, 42),
            "manifest publisher must compile and publish the runtime action graph before reporting promote success");
        Require(runtimeOwner.GetActiveGraph() != nullptr, "runtime graph owner must hold the published compiled graph");
        Require(runtimeOwner.GetActiveManifestEpoch() == 42, "runtime graph owner must publish the manifest epoch");
        Require(
            dualpad::input_v2::config::ActionManifestPublisher::GetSingleton().GetPublishCount() == 1,
            "manifest publish count must advance after graph publication succeeds");

        auto badBundle = bundle;
        badBundle.manifest.manifestEpoch = 43;
        Require(
            !dualpad::input_v2::config::ActionManifestPublisher::GetSingleton().PublishPromotedBundle(badBundle, 42),
            "graph/manifest epoch mismatch must fail before manifest publication is recorded");
        Require(runtimeOwner.GetActiveManifestEpoch() == 42, "failed graph publish must leave the previous active graph intact");
        Require(
            dualpad::input_v2::config::ActionManifestPublisher::GetSingleton().GetPublishCount() == 1,
            "failed graph publish must not leave manifest switched without a matching graph");

        auto graphCompileFailureBundle = bundle;
        graphCompileFailureBundle.manifest.bindings.clear();
        graphCompileFailureBundle.manifest.bindings.push_back(
            Binding("NativeCombo", Trigger(dualpad::input::TriggerType::Combo, 3, { 2 })));
        graphCompileFailureBundle.manifest.bindings.push_back(
            Binding("PowerAttack", Trigger(dualpad::input::TriggerType::Combo, 2, { 3 })));
        Require(
            !dualpad::input_v2::config::ActionManifestPublisher::GetSingleton().PublishPromotedBundle(graphCompileFailureBundle, 42),
            "graph compile failure during manifest promote must fail closed");
        Require(runtimeOwner.GetActiveManifestEpoch() == 42, "graph compile failure must leave previous graph active");
        Require(
            dualpad::input_v2::config::ActionManifestPublisher::GetSingleton().GetPublishCount() == 1,
            "graph compile failure must not record manifest publication");
    }

    void RunLegacyInteractionInputAdapterTests()
    {
        actions::LegacyInteractionInputFrame legacy{};
        legacy.manifestEpoch = 42;
        legacy.contextRevision = 7;
        legacy.menuStackRevision = 8;
        legacy.deviceFamilyRevision = 9;
        legacy.monotonicUs = 1234;
        legacy.samples = {
            Sample(actions::ControlPath{ .kind = actions::ControlPathKind::DigitalButton, .code = 10 }, true, true, false, 1200, 1234)
        };

        const auto kernel = actions::LegacyInteractionInputAdapter::BuildKernelFrame(legacy);
        Require(kernel.facts.manifestEpoch == 42, "legacy adapter must copy manifestEpoch without inventing a second source");
        Require(kernel.facts.contextRevision == 7, "legacy adapter must copy PH2 contextRevision");
        Require(kernel.facts.menuStackRevision == 8, "legacy adapter must copy PH2 menuStackRevision");
        Require(kernel.facts.deviceFamilyRevision == 9, "legacy adapter must copy PH3 deviceFamilyRevision");
        Require(kernel.state.controlSamples.size() == 1, "legacy adapter must only carry control samples into KernelFrame");
        Require(
            actions::LegacyInteractionInputAdapter::DeletionCondition().find("InputKernel::BuildKernelFrame") != std::string::npos,
            "legacy adapter must document its deletion condition");

        auto manifest = ManifestWithActions();
        manifest.bindings.push_back(Binding("Jump", Trigger(dualpad::input::TriggerType::Button, 10)));
        const auto compiled = actions::ActionGraphCompiler::Compile(manifest);
        Require(compiled.ok, compiled.message);

        actions::ActionSetStack stack{};
        stack.baseSetId = "GameplayBase";
        actions::InteractionEngine engine;
        actions::InteractionStateStore state;
        const auto resolved = engine.Resolve(compiled.graph, stack, kernel, state);
        Require(resolved.changes.size() == 1, "InteractionEngine must consume KernelFrame instead of an anonymous input frame");
        Require(resolved.changes[0].actionId == "Jump", "KernelFrame input must resolve the same action graph binding");
    }

    void RunLegacyLifecycleBridgeTests()
    {
        actions::ResolvedActionFrame resolved{};
        resolved.manifestEpoch = 42;
        resolved.contextRevision = 7;
        resolved.changes.push_back(actions::ActionPhaseChange{
            .actionId = "Jump",
            .bindingId = 77,
            .phase = actions::ActionPhase::Press,
            .timestampUs = 1234
        });

        const auto plan = actions::LegacyLifecycleBridge::BuildShadowFrameActionPlan(
            resolved,
            dualpad::input::InputContext::Gameplay);
        Require(plan.Size() == 1, "legacy lifecycle bridge must consume ResolvedActionFrame into a shadow FrameActionPlan");
        Require(plan[0].actionId == "Jump", "bridge must preserve actionId for parity");
        Require(plan[0].sourceCode == 77, "bridge must retain bindingId as explainable sourceCode");
        Require(
            plan[0].phase == dualpad::input::backend::PlannedActionPhase::Press,
            "bridge must preserve press phase semantics");
    }

    void RunInteractionEngineTests()
    {
        auto manifest = ManifestWithActions();
        manifest.bindings.push_back(Binding("Jump", Trigger(dualpad::input::TriggerType::Button, 11)));
        manifest.bindings.push_back(Binding("PowerAttack", Trigger(dualpad::input::TriggerType::Layer, 11, { 1 })));
        manifest.bindings.push_back(Binding("NativeCombo", Trigger(dualpad::input::TriggerType::Combo, 3, { 2 })));

        const auto compiled = actions::ActionGraphCompiler::Compile(manifest);
        Require(compiled.ok, compiled.message);

        actions::ActionSetStack stack{};
        stack.baseSetId = "GameplayBase";

        actions::InteractionEngine engine;
        actions::InteractionStateStore state;

        {
            actions::LegacyInteractionInputFrame legacy{};
            legacy.manifestEpoch = 42;
            legacy.contextRevision = 7;
            legacy.monotonicUs = 1'000;
            legacy.samples = {
                Sample(actions::ControlPath{ .kind = actions::ControlPathKind::DigitalButton, .code = 1 }, true, false, false, 500, 1'000),
                Sample(actions::ControlPath{ .kind = actions::ControlPathKind::DigitalButton, .code = 11 }, true, true, false, 1'000, 1'000)
            };
            const auto frame = actions::LegacyInteractionInputAdapter::BuildKernelFrame(legacy);

            const auto resolved = engine.Resolve(compiled.graph, stack, frame, state);
            Require(resolved.changes.size() == 1, "Layer must fire when required path is already down before primary");
            Require(resolved.changes[0].actionId == "PowerAttack", "Layer must resolve to its action");
            Require(resolved.changes[0].phase == actions::ActionPhase::Press, "Layer press must emit Press");
            Require(resolved.changes[0].bindingId == 2, "ResolvedActionFrame must retain the selected layer bindingId for explainability");
        }

        state.Reset();
        {
            auto exactOnlyManifest = ManifestWithActions();
            exactOnlyManifest.bindings.push_back(Binding("PowerAttack", Trigger(dualpad::input::TriggerType::Layer, 11, { 1 })));
            const auto exactOnlyCompiled = actions::ActionGraphCompiler::Compile(exactOnlyManifest);
            Require(exactOnlyCompiled.ok, exactOnlyCompiled.message);

            actions::LegacyInteractionInputFrame legacy{};
            legacy.manifestEpoch = 42;
            legacy.contextRevision = 7;
            legacy.monotonicUs = 1'500;
            legacy.samples = {
                Sample(actions::ControlPath{ .kind = actions::ControlPathKind::DigitalButton, .code = 1 }, true, false, false, 500, 1'500),
                Sample(actions::ControlPath{ .kind = actions::ControlPathKind::DigitalButton, .code = 2 }, true, false, false, 600, 1'500),
                Sample(actions::ControlPath{ .kind = actions::ControlPathKind::DigitalButton, .code = 11 }, true, true, false, 1'500, 1'500)
            };
            const auto frame = actions::LegacyInteractionInputAdapter::BuildKernelFrame(legacy);

            const auto resolved = engine.Resolve(exactOnlyCompiled.graph, stack, frame, state);
            Require(resolved.changes.empty(), "ExactOnly layer must not match when an extra active digital path is present");
        }

        state.Reset();
        {
            auto fallbackManifest = ManifestWithActions();
            fallbackManifest.bindings.push_back(Binding("Jump", Trigger(dualpad::input::TriggerType::Button, 11)));
            const auto fallbackCompiled = actions::ActionGraphCompiler::Compile(fallbackManifest);
            Require(fallbackCompiled.ok, fallbackCompiled.message);

            actions::LegacyInteractionInputFrame legacy{};
            legacy.manifestEpoch = 42;
            legacy.contextRevision = 7;
            legacy.monotonicUs = 1'750;
            legacy.samples = {
                Sample(actions::ControlPath{ .kind = actions::ControlPathKind::DigitalButton, .code = 1 }, true, false, false, 500, 1'750),
                Sample(actions::ControlPath{ .kind = actions::ControlPathKind::DigitalButton, .code = 11 }, true, true, false, 1'750, 1'750)
            };
            const auto frame = actions::LegacyInteractionInputAdapter::BuildKernelFrame(legacy);

            const auto resolved = engine.Resolve(fallbackCompiled.graph, stack, frame, state);
            Require(resolved.changes.size() == 1, "PreferExactThenSubset button must fall back to subset when no exact binding exists");
            Require(resolved.changes[0].actionId == "Jump", "subset fallback must resolve the base button action");
        }

        state.Reset();
        {
            actions::LegacyInteractionInputFrame legacy{};
            legacy.manifestEpoch = 42;
            legacy.contextRevision = 7;
            legacy.monotonicUs = 2'000;
            legacy.samples = {
                Sample(actions::ControlPath{ .kind = actions::ControlPathKind::DigitalButton, .code = 2 }, true, false, false, 1'950, 2'000),
                Sample(actions::ControlPath{ .kind = actions::ControlPathKind::DigitalButton, .code = 3 }, true, true, false, 2'000, 2'000)
            };
            const auto frame = actions::LegacyInteractionInputAdapter::BuildKernelFrame(legacy);

            const auto resolved = engine.Resolve(compiled.graph, stack, frame, state);
            Require(resolved.changes.size() == 1, "Combo must fire when final legacy participant arrives second");
            Require(resolved.changes[0].actionId == "NativeCombo", "Combo must resolve to its action");
            Require(resolved.changes[0].phase == actions::ActionPhase::Pulse, "Combo must emit a pulse");
        }

        state.Reset();
        {
            actions::LegacyInteractionInputFrame legacy{};
            legacy.manifestEpoch = 42;
            legacy.contextRevision = 7;
            legacy.monotonicUs = 3'000;
            legacy.samples = {
                Sample(actions::ControlPath{ .kind = actions::ControlPathKind::DigitalButton, .code = 2 }, true, true, false, 3'000, 3'000),
                Sample(actions::ControlPath{ .kind = actions::ControlPathKind::DigitalButton, .code = 3 }, true, false, false, 2'950, 3'000)
            };
            const auto frame = actions::LegacyInteractionInputAdapter::BuildKernelFrame(legacy);

            const auto resolved = engine.Resolve(compiled.graph, stack, frame, state);
            Require(resolved.changes.size() == 1, "Combo must be unordered and fire when required participant arrives second");
            Require(resolved.changes[0].actionId == "NativeCombo", "unordered combo must still resolve to combo action");
        }

        state.Reset();
        {
            actions::LegacyInteractionInputFrame legacy{};
            legacy.manifestEpoch = 41;
            legacy.contextRevision = 7;
            legacy.monotonicUs = 4'000;
            legacy.samples = {
                Sample(actions::ControlPath{ .kind = actions::ControlPathKind::DigitalButton, .code = 1 }, true, false, false, 3'500, 4'000),
                Sample(actions::ControlPath{ .kind = actions::ControlPathKind::DigitalButton, .code = 11 }, true, true, false, 4'000, 4'000)
            };
            const auto frame = actions::LegacyInteractionInputAdapter::BuildKernelFrame(legacy);

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
        RunLegacyInteractionInputAdapterTests();
        RunInteractionEngineTests();
        RunLegacyLifecycleBridgeTests();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
