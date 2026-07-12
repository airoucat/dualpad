#include "pch.h"

#include "input/injection/RouteHealthContract.h"
#include "input/injection/PollMaterializationReceipt.h"
#include "input/injection/PadEventSnapshot.h"
#include "input/Action.h"
#include "input/PadProfile.h"
#include "input/Trigger.h"
#include "input/backend/NativeActionDescriptor.h"
#include "input/backend/PollCommitCoordinator.h"
#include "input/XInputStateBridge.h"
#include "input_v2/actions/CompiledActionGraph.h"
#include "input_v2/actions/CompiledActionGraphPublisher.h"
#include "input_v2/actions/InteractionEngine.h"
#include "input_v2/actions/LegacyInteractionInputAdapter.h"
#include "input_v2/actions/LegacyLifecycleBridge.h"
#include "input_v2/config/ActionManifestPublisher.h"
#include "input_v2/config/AtomicConfigReloader.h"
#include "input_v2/context/ContextCatalog.h"
#include "input_v2/context/ContextResolver.h"
#include "input_v2/gameplay/DualPadRuntime.h"
#include "input_v2/gameplay/PollOutputFrame.h"
#include "input_v2/gameplay/PollOutputAdapter.h"
#include "input_v2/gameplay/RuntimeDiagnostics.h"
#include "input_v2/ingress/FrameAssembler.h"
#include "input_v2/ingress/IngressHub.h"
#include "input_v2/ingress/LiveInputFactProducer.h"
#include "input_v2/menu/MenuInstanceRegistry.h"
#include "input_v2/presentation/SkyrimCompatibilitySurface.h"
#include "input_v2/prompt/PromptRuntimeOwner.h"
#include "input_v2/runtime/RuntimeOwnerGuard.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
    namespace actions = dualpad::input_v2::actions;
    namespace config = dualpad::input_v2::config;
    namespace context = dualpad::input_v2::context;
    namespace gameplay = dualpad::input_v2::gameplay;
    namespace ingress = dualpad::input_v2::ingress;
    namespace runtime = dualpad::input_v2::runtime;
    namespace menu = dualpad::input_v2::menu;
    namespace presentation = dualpad::input_v2::presentation;
    namespace prompt = dualpad::input_v2::prompt;
    namespace input_actions = dualpad::input::actions;
    namespace input_backend = dualpad::input::backend;

    void Require(bool condition, std::string_view message)
    {
        if (!condition) {
            throw std::runtime_error(std::string(message));
        }
    }

    bool Contains(const std::vector<std::string>& values, std::string_view expected)
    {
        for (const auto& value : values) {
            if (value == expected) {
                return true;
            }
        }
        return false;
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
            actions::ActionDefinition{ .id = "LookX", .valueKind = actions::ActionValueKind::Axis1D },
            actions::ActionDefinition{ .id = "Game.Look", .valueKind = actions::ActionValueKind::Axis2D },
            actions::ActionDefinition{ .id = "RightTrigger", .valueKind = actions::ActionValueKind::Axis1D }
        };
        return manifest;
    }

    class RecordingPollOutputExecutor final : public gameplay::IPollOutputExecutor
    {
    public:
        std::vector<gameplay::PollOutputApplyStep> steps;

        bool ClearNativeOutput() override
        {
            steps.push_back(gameplay::PollOutputApplyStep::ClearNativeOutput);
            return true;
        }

        bool ClearHelperOutput() override
        {
            steps.push_back(gameplay::PollOutputApplyStep::ClearHelperOutput);
            return true;
        }

        bool ClearSustainedDigitalAggregator() override
        {
            steps.push_back(gameplay::PollOutputApplyStep::ClearSustainedDigitalAggregator);
            return true;
        }

        bool ClearProjectionStickyOwners() override
        {
            steps.push_back(gameplay::PollOutputApplyStep::ClearProjectionStickyOwners);
            return true;
        }

        bool ApplyGatePlan(const gameplay::GatePlan&) override
        {
            steps.push_back(gameplay::PollOutputApplyStep::ApplyGatePlan);
            return true;
        }

        bool ApplyPreOutputPresentationHandoff(const gameplay::GameplayPresentationPlan&) override
        {
            steps.push_back(gameplay::PollOutputApplyStep::ApplyPreOutputPresentationHandoff);
            return true;
        }

        bool ApplySustainedDigital(const gameplay::NativeSustainedCommand&) override
        {
            steps.push_back(gameplay::PollOutputApplyStep::ApplySustainedDigital);
            return true;
        }

        bool ApplyTransientDigital(const gameplay::NativeTransientCommand&) override
        {
            steps.push_back(gameplay::PollOutputApplyStep::ApplyTransientDigital);
            return true;
        }

        bool ApplyHelperCommand(const gameplay::HelperOutputCommand&) override
        {
            steps.push_back(gameplay::PollOutputApplyStep::ApplyHelperCommand);
            return true;
        }

        bool PublishAnalogState(const gameplay::ProjectedAnalogState&) override
        {
            steps.push_back(gameplay::PollOutputApplyStep::PublishAnalogState);
            return true;
        }

        bool CommitCleanRecoveryBaseline() override
        {
            steps.push_back(gameplay::PollOutputApplyStep::CommitCleanRecoveryBaseline);
            return true;
        }
    };

    context::ResolvedContextSnapshot PublishJournalContext()
    {
        menu::ReconciledMenuStack stack{};
        stack.menuStackRevision = 11;
        stack.trackedMenus.push_back(menu::TrackedMenuInstance{
            .instanceId = 1,
            .menuName = "Journal Menu",
            .menuPtr = 0x100,
            .delegatePtr = 0x200,
            .moviePtr = 0x300,
            .observationOrder = 1,
            .identityQuality = menu::MenuIdentityQuality::StablePointer,
            .observedInLastSnapshot = true
        });
        return context::ContextResolver::GetSingleton().ResolveAndPublish(
            stack,
            context::GameplaySubstate::None,
            context::ContextCatalog::BuiltInCatalog());
    }

    context::ResolvedContextSnapshot PublishGenericMenuContext()
    {
        menu::ReconciledMenuStack stack{};
        stack.menuStackRevision = 13;
        stack.trackedMenus.push_back(menu::TrackedMenuInstance{
            .instanceId = 2,
            .menuName = "Main Menu",
            .menuPtr = 0x110,
            .delegatePtr = 0x210,
            .moviePtr = 0x310,
            .observationOrder = 1,
            .identityQuality = menu::MenuIdentityQuality::StablePointer,
            .observedInLastSnapshot = true
        });
        return context::ContextResolver::GetSingleton().ResolveAndPublish(
            stack,
            context::GameplaySubstate::None,
            context::ContextCatalog::BuiltInCatalog());
    }

    context::ResolvedContextSnapshot PublishGameplayContext()
    {
        menu::ReconciledMenuStack stack{};
        stack.menuStackRevision = 12;
        return context::ContextResolver::GetSingleton().ResolveAndPublish(
            stack,
            context::GameplaySubstate::None,
            context::ContextCatalog::BuiltInCatalog());
    }

    class ContextInterleavingPollOutputExecutor final : public gameplay::IPollOutputExecutor
    {
    public:
        bool ClearNativeOutput() override { return true; }
        bool ClearHelperOutput() override { return true; }
        bool ClearSustainedDigitalAggregator() override { return true; }
        bool ClearProjectionStickyOwners() override { return true; }

        bool ApplyGatePlan(const gameplay::GatePlan&) override
        {
            (void)PublishGameplayContext();
            return true;
        }

        bool ApplyPreOutputPresentationHandoff(const gameplay::GameplayPresentationPlan&) override { return true; }
        bool ApplySustainedDigital(const gameplay::NativeSustainedCommand&) override { return true; }
        bool ApplyTransientDigital(const gameplay::NativeTransientCommand&) override { return true; }
        bool ApplyHelperCommand(const gameplay::HelperOutputCommand&) override { return true; }
        bool PublishAnalogState(const gameplay::ProjectedAnalogState&) override { return true; }
        bool CommitCleanRecoveryBaseline() override { return true; }
    };

    presentation::SourceEvidenceSnapshot SourceEvidence(
        presentation::DeviceFamily family,
        std::uint32_t deviceFamilyRevision,
        bool gamepadEvidence,
        std::uint64_t tick)
    {
        presentation::SourceEvidenceSnapshot snapshot{};
        snapshot.deviceFamilyEvidence = presentation::PublishedDeviceFamilyEvidence{
            .family = family,
            .deviceFamilyRevision = deviceFamilyRevision,
            .source = presentation::DeviceFamilyEvidenceSource::RawInputIngress,
            .publishedTick = tick
        };
        snapshot.gamepadEvidence = gamepadEvidence;
        snapshot.contextRevision = context::ContextResolver::GetSingleton().GetPublishedSnapshot().contextRevision;
        snapshot.uiContextId = context::UiContextId::Journal;
        snapshot.presentationPolicyId = "PolicyOnlyPH2MayChoose";
        snapshot.collectedTick = tick;
        return snapshot;
    }

    ingress::AssembledFactFrame StableMenuFrame(
        std::uint64_t seq,
        presentation::SourceEvidenceSnapshot evidence)
    {
        const auto& contextSnapshot = context::ContextResolver::GetSingleton().GetPublishedSnapshot();
        ingress::AssembledFactFrame frame{};
        frame.kind = ingress::AssembledFrameKind::Stable;
        frame.firstSeq = seq;
        frame.lastSeq = seq;
        frame.boundaryKey = ingress::IngressBoundaryKey{
            42,
            contextSnapshot.contextRevision,
            contextSnapshot.menuStackRevision,
            evidence.deviceFamilyEvidence.deviceFamilyRevision
        };
        frame.facts.manifestEpoch = frame.boundaryKey.manifestEpoch;
        frame.facts.contextRevision = frame.boundaryKey.contextRevision;
        frame.facts.menuStackRevision = frame.boundaryKey.menuStackRevision;
        frame.facts.deviceFamilyRevision = frame.boundaryKey.deviceFamilyRevision;
        frame.facts.sourceEvidence = std::move(evidence);
        return frame;
    }

    dualpad::input::PadEventSnapshot LiveHidSnapshot(
        std::uint64_t sequence,
        std::uint32_t mask,
        std::uint64_t timestampUs,
        dualpad::input::InputContext context = dualpad::input::InputContext::JournalMenu,
        std::uint32_t contextEpoch = 7,
        std::uint32_t contextRevision = 0)
    {
        dualpad::input::PadEventSnapshot snapshot{};
        snapshot.sequence = sequence;
        snapshot.firstSequence = sequence;
        snapshot.sourceTimestampUs = timestampUs;
        snapshot.context = context;
        snapshot.contextEpoch = contextEpoch;
        snapshot.contextRevision = contextRevision;
        snapshot.state.sequence = sequence;
        snapshot.state.timestampUs = timestampUs;
        snapshot.state.buttons.digitalMask = mask;
        return snapshot;
    }

    ingress::AssembledFactFrame HardTransitionFrame(
        std::uint64_t seq,
        ingress::TransitionReason reason);

    ingress::AssembledFactFrame HardTransitionFrame(std::uint64_t seq)
    {
        return HardTransitionFrame(seq, ingress::TransitionReason::QueueOverflow);
    }

    ingress::AssembledFactFrame HardTransitionFrame(
        std::uint64_t seq,
        ingress::TransitionReason reason)
    {
        ingress::AssembledFactFrame frame{};
        frame.kind = ingress::AssembledFrameKind::Transition;
        frame.firstSeq = seq;
        frame.lastSeq = seq;
        frame.boundaryKey = ingress::IngressBoundaryKey{ 42, 1, 11, 2 };
        frame.transition = ingress::TransitionFrameMeta{
            .from = ingress::IngressBoundaryKey{ 42, 1, 11, 1 },
            .to = frame.boundaryKey,
            .reason = reason,
            .requestHardResync = true,
            .flushPendingPulseEdges = true
        };
        return frame;
    }

    ingress::AssembledFactFrame SoftSequenceGapTransitionFrame(std::uint64_t seq)
    {
        ingress::AssembledFactFrame frame{};
        frame.kind = ingress::AssembledFrameKind::Transition;
        frame.firstSeq = seq;
        frame.lastSeq = seq;
        frame.boundaryKey = ingress::IngressBoundaryKey{ 42, 1, 11, 2 };
        frame.facts.manifestEpoch = frame.boundaryKey.manifestEpoch;
        frame.facts.contextRevision = frame.boundaryKey.contextRevision;
        frame.facts.menuStackRevision = frame.boundaryKey.menuStackRevision;
        frame.facts.deviceFamilyRevision = frame.boundaryKey.deviceFamilyRevision;
        frame.facts.health.sequenceGap = true;
        frame.transition = ingress::TransitionFrameMeta{
            .from = ingress::IngressBoundaryKey{ 42, 1, 11, 2 },
            .to = frame.boundaryKey,
            .reason = ingress::TransitionReason::SequenceGap,
            .requestSoftResync = true,
            .requestHardResync = false,
            .flushPendingPulseEdges = false
        };
        return frame;
    }

    void ResetRuntimeSurfaceState(gameplay::DualPadRuntime& runtime)
    {
        runtime.ResetForTests();
        config::AtomicConfigReloader::GetSingleton().ResetForTests();
        context::ContextResolver::GetSingleton().ResetForTests();
        actions::CompiledActionGraphPublisher::GetRuntimeOwner().ResetForTests();
        prompt::PromptRuntimeOwner::GetSingleton().ResetForTests();
        auto& compat = presentation::SkyrimCompatibilitySurface::GetSingleton();
        compat.DisableRollback();
        compat.Commit(presentation::PublishedPresentationState{});
        compat.ResetRefreshStateForTests();
        compat.SetMenuRefreshTaskSinkForTests([](auto) {
            presentation::SkyrimCompatibilitySurface::GetSingleton().CompleteQueuedRefreshForTests();
            return true;
        });
        compat.ForceInstallResultForTests(
            presentation::detail::MakeHookInstallResult(
                presentation::HookInstallStatus::Success,
                "test_hook_installed"));
        dualpad::input::detail::ResetUpstreamRouteInstallSnapshotForTests();

        actions::CompiledActionGraph graph{};
        graph.manifestEpoch = 42;
        Require(
            actions::CompiledActionGraphPublisher::GetRuntimeOwner().Publish(graph, 42).ok,
            "runtime surface tests need an active manifest epoch for prompt scope publication");
        (void)PublishJournalContext();
    }

    std::filesystem::path FindProjectRoot(std::filesystem::path from = std::filesystem::current_path())
    {
        while (!from.empty()) {
            if (std::filesystem::is_regular_file(from / "xmake.lua")) {
                return from;
            }
            const auto parent = from.parent_path();
            if (parent == from) {
                break;
            }
            from = parent;
        }
        throw std::runtime_error("could not find project root");
    }

    void LoadRuntimeConfigForGameplayBindingTests()
    {
        const auto root = FindProjectRoot();
        const auto loaded = config::AtomicConfigReloader::GetSingleton().LoadOrRecover(
            root / "config" / "DualPadBindings.ini",
            root / "config" / "DualPadMenuPolicy.ini");
        Require(loaded.ok, loaded.message);
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

    actions::ControlSample AxisSample(
        std::uint32_t code,
        float value,
        std::uint64_t now)
    {
        return actions::ControlSample{
            .path = actions::ControlPath{ .kind = actions::ControlPathKind::AnalogAxis1D, .code = code },
            .down = value != 0.0f,
            .scalar = value,
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
            const auto& axis = compiled.graph.bindings[3];
            Require(axis.modifiers.size() == 1, "Axis lowering must attach one neutral deadzone modifier");
            Require(
                axis.modifiers[0].kind == actions::BindingModifierKind::Deadzone,
                "Axis lowering modifier must be a deadzone");
            Require(
                axis.modifiers[0].primary > (1.0f / 255.0f),
                "Axis neutral deadzone must suppress one-step HID neutral quantization");
            Require(
                axis.modifiers[0].primary > 0.012f,
                "Axis neutral deadzone must suppress field-observed menu stick drift");
            Require(
                axis.modifiers[0].primary < 0.10f,
                "Axis neutral deadzone must stay below deliberate menu stick movement");
            Require(
                axis.modifiers[0].primary < 0.25f,
                "Axis neutral deadzone must stay below intentional menu stick movement");

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
        const auto snapshot = publisher.GetActiveSnapshot();
        Require(snapshot.graph == published.graph, "publisher snapshot must expose the active graph");
        Require(snapshot.manifestEpoch == 42, "publisher snapshot must expose graph and epoch atomically");

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
            auto analogManifest = ManifestWithActions();
            analogManifest.bindings.push_back(Binding(
                "LookX",
                Trigger(
                    dualpad::input::TriggerType::Axis,
                    static_cast<std::uint32_t>(dualpad::input::PadAxisId::RightStickX))));
            analogManifest.bindings.push_back(Binding(
                "RightTrigger",
                Trigger(
                    dualpad::input::TriggerType::Axis,
                    static_cast<std::uint32_t>(dualpad::input::PadAxisId::RightTrigger))));
            const auto analogCompiled = actions::ActionGraphCompiler::Compile(analogManifest);
            Require(analogCompiled.ok, analogCompiled.message);

            actions::LegacyInteractionInputFrame legacy{};
            legacy.manifestEpoch = 42;
            legacy.contextRevision = 7;
            legacy.monotonicUs = 1'625;
            legacy.samples = {
                AxisSample(static_cast<std::uint32_t>(dualpad::input::PadAxisId::RightStickX), 0.5f, 1'625),
                AxisSample(static_cast<std::uint32_t>(dualpad::input::PadAxisId::RightTrigger), 1.0f, 1'625)
            };
            const auto frame = actions::LegacyInteractionInputAdapter::BuildKernelFrame(legacy);

            const auto resolved = engine.Resolve(analogCompiled.graph, stack, frame, state);
            Require(resolved.values.size() == 2, "ExactOnly axis bindings must resolve independently when multiple axes are active");
            Require(resolved.changes.size() == 2, "each active axis binding must emit a Value phase change");

            legacy.monotonicUs = 1'626;
            for (auto& sample : legacy.samples) {
                sample.timestampUs = 1'626;
            }
            const auto unchangedFrame = actions::LegacyInteractionInputAdapter::BuildKernelFrame(legacy);
            const auto unchangedResolved = engine.Resolve(analogCompiled.graph, stack, unchangedFrame, state);
            Require(
                unchangedResolved.values.size() == 2,
                "unchanged non-neutral axes must remain in the absolute current-state snapshot");
            Require(
                unchangedResolved.changes.empty(),
                "unchanged non-neutral axes must not repeat Value phase changes");
        }

        state.Reset();
        {
            auto axis2DManifest = ManifestWithActions();
            axis2DManifest.bindings.push_back(Binding(
                "Game.Look",
                Trigger(
                    dualpad::input::TriggerType::Axis,
                    static_cast<std::uint32_t>(dualpad::input::PadAxisId::RightStickX))));
            axis2DManifest.bindings.push_back(Binding(
                "Game.Look",
                Trigger(
                    dualpad::input::TriggerType::Axis,
                    static_cast<std::uint32_t>(dualpad::input::PadAxisId::RightStickY))));
            const auto axis2DCompiled = actions::ActionGraphCompiler::Compile(axis2DManifest);
            Require(axis2DCompiled.ok, axis2DCompiled.message);

            actions::LegacyInteractionInputFrame legacy{};
            legacy.manifestEpoch = 42;
            legacy.contextRevision = 7;
            legacy.monotonicUs = 1'650;
            legacy.samples = {
                AxisSample(static_cast<std::uint32_t>(dualpad::input::PadAxisId::RightStickX), 1.25f, 1'640),
                AxisSample(static_cast<std::uint32_t>(dualpad::input::PadAxisId::RightStickY), -1.25f, 1'645)
            };
            const auto frame = actions::LegacyInteractionInputAdapter::BuildKernelFrame(legacy);

            const auto resolved = engine.Resolve(axis2DCompiled.graph, stack, frame, state);
            Require(resolved.values.size() == 1, "Axis2D pair must coalesce into one action value");
            Require(resolved.changes.size() == 1, "Axis2D pair must emit one coalesced Value phase change");
            Require(resolved.values[0].actionId == "Game.Look", "Axis2D value must retain the action id");
            Require(resolved.values[0].kind == actions::ActionValueKind::Axis2D, "Axis2D pair must publish Axis2D kind");
            Require(resolved.values[0].x == 1.0f, "Axis2D X must clamp to the [-1, 1] domain");
            Require(resolved.values[0].y == -1.0f, "Axis2D Y must clamp to the [-1, 1] domain");
            Require(resolved.values[0].timestampUs == 1'650, "Axis2D coalesced value timestamp must use frame evaluation time");
            Require(resolved.changes[0].timestampUs == 1'650, "Axis2D Value change timestamp must use frame evaluation time");
        }

        state.Reset();
        {
            auto neutralAxisManifest = ManifestWithActions();
            neutralAxisManifest.bindings.push_back(Binding(
                "Game.Look",
                Trigger(
                    dualpad::input::TriggerType::Axis,
                    static_cast<std::uint32_t>(dualpad::input::PadAxisId::LeftStickX))));
            neutralAxisManifest.bindings.push_back(Binding(
                "Game.Look",
                Trigger(
                    dualpad::input::TriggerType::Axis,
                    static_cast<std::uint32_t>(dualpad::input::PadAxisId::LeftStickY))));
            const auto neutralAxisCompiled = actions::ActionGraphCompiler::Compile(neutralAxisManifest);
            Require(neutralAxisCompiled.ok, neutralAxisCompiled.message);

            actions::LegacyInteractionInputFrame neutralLegacy{};
            neutralLegacy.manifestEpoch = 42;
            neutralLegacy.contextRevision = 7;
            neutralLegacy.monotonicUs = 1'700;
            neutralLegacy.samples = {
                AxisSample(static_cast<std::uint32_t>(dualpad::input::PadAxisId::LeftStickX), 1.0f / 255.0f, 1'700),
                AxisSample(static_cast<std::uint32_t>(dualpad::input::PadAxisId::LeftStickY), -1.0f / 255.0f, 1'700)
            };
            const auto neutralFrame = actions::LegacyInteractionInputAdapter::BuildKernelFrame(neutralLegacy);
            const auto neutralResolved = engine.Resolve(neutralAxisCompiled.graph, stack, neutralFrame, state);
            Require(
                neutralResolved.values.empty(),
                "one-step HID neutral quantization must not emit an Axis2D value");
            Require(
                neutralResolved.changes.empty(),
                "one-step HID neutral quantization must not emit a Value phase change");

            actions::LegacyInteractionInputFrame fieldDriftLegacy{};
            fieldDriftLegacy.manifestEpoch = 42;
            fieldDriftLegacy.contextRevision = 7;
            fieldDriftLegacy.monotonicUs = 1'712;
            fieldDriftLegacy.samples = {
                AxisSample(static_cast<std::uint32_t>(dualpad::input::PadAxisId::LeftStickX), 0.0f, 1'712),
                AxisSample(static_cast<std::uint32_t>(dualpad::input::PadAxisId::LeftStickY), -0.012f, 1'712)
            };
            const auto fieldDriftFrame = actions::LegacyInteractionInputAdapter::BuildKernelFrame(fieldDriftLegacy);
            const auto fieldDriftResolved = engine.Resolve(neutralAxisCompiled.graph, stack, fieldDriftFrame, state);
            Require(
                fieldDriftResolved.values.empty(),
                "field-observed left-stick neutral drift must not emit an Axis2D value");
            Require(
                fieldDriftResolved.changes.empty(),
                "field-observed left-stick neutral drift must not emit a Value phase change");

            actions::LegacyInteractionInputFrame smallIntentionalLegacy{};
            smallIntentionalLegacy.manifestEpoch = 42;
            smallIntentionalLegacy.contextRevision = 7;
            smallIntentionalLegacy.monotonicUs = 1'718;
            smallIntentionalLegacy.samples = {
                AxisSample(static_cast<std::uint32_t>(dualpad::input::PadAxisId::LeftStickX), 0.05f, 1'718),
                AxisSample(static_cast<std::uint32_t>(dualpad::input::PadAxisId::LeftStickY), -0.05f, 1'718)
            };
            const auto smallIntentionalFrame =
                actions::LegacyInteractionInputAdapter::BuildKernelFrame(smallIntentionalLegacy);
            const auto smallIntentionalResolved = engine.Resolve(
                neutralAxisCompiled.graph,
                stack,
                smallIntentionalFrame,
                state);
            Require(
                smallIntentionalResolved.values.size() == 1,
                "small deliberate menu stick movement above drift deadzone must still emit Axis2D value");
            Require(
                smallIntentionalResolved.changes.size() == 1,
                "small deliberate menu stick movement above drift deadzone must still emit Value phase change");
            Require(
                smallIntentionalResolved.values[0].x == 0.05f,
                "small deliberate menu stick X must survive neutral deadzone");
            Require(
                smallIntentionalResolved.values[0].y == -0.05f,
                "small deliberate menu stick Y must survive neutral deadzone");
            state.Reset();

            actions::LegacyInteractionInputFrame intentionalLegacy{};
            intentionalLegacy.manifestEpoch = 42;
            intentionalLegacy.contextRevision = 7;
            intentionalLegacy.monotonicUs = 1'725;
            intentionalLegacy.samples = {
                AxisSample(static_cast<std::uint32_t>(dualpad::input::PadAxisId::LeftStickX), 0.25f, 1'725),
                AxisSample(static_cast<std::uint32_t>(dualpad::input::PadAxisId::LeftStickY), -0.25f, 1'725)
            };
            const auto intentionalFrame = actions::LegacyInteractionInputAdapter::BuildKernelFrame(intentionalLegacy);
            const auto intentionalResolved = engine.Resolve(neutralAxisCompiled.graph, stack, intentionalFrame, state);
            Require(
                intentionalResolved.values.size() == 1,
                "intentional menu stick movement above neutral deadzone must still emit Axis2D value");
            Require(
                intentionalResolved.changes.size() == 1,
                "intentional menu stick movement above neutral deadzone must still emit Value phase change");
            Require(
                intentionalResolved.values[0].x == 0.25f,
                "intentional menu stick X must survive neutral deadzone");
            Require(
                intentionalResolved.values[0].y == -0.25f,
                "intentional menu stick Y must survive neutral deadzone");
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
            Require(resolved.changes[0].firstEdgeUs == 1'950, "Combo pulse must expose firstEdgeUs");
            Require(resolved.changes[0].lastEdgeUs == 2'000, "Combo pulse must expose lastEdgeUs");
            Require(resolved.changes[0].evaluationUs == 2'000, "Combo pulse must expose evaluationUs");
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
            Require(resolved.changes[0].firstEdgeUs == 2'950, "unordered combo firstEdgeUs must use the earlier participant edge");
            Require(resolved.changes[0].lastEdgeUs == 3'000, "unordered combo lastEdgeUs must use the later participant edge");
            Require(resolved.changes[0].evaluationUs == 3'000, "unordered combo evaluationUs must use frame evaluation time");
        }

        state.Reset();
        {
            actions::LegacyInteractionInputFrame first{};
            first.manifestEpoch = 42;
            first.contextRevision = 7;
            first.monotonicUs = 5'000;
            first.samples = {
                Sample(actions::ControlPath{ .kind = actions::ControlPathKind::DigitalButton, .code = 2 }, true, false, false, 4'950, 5'000),
                Sample(actions::ControlPath{ .kind = actions::ControlPathKind::DigitalButton, .code = 3 }, true, true, false, 5'000, 5'000)
            };
            const auto firstResolved = engine.Resolve(
                compiled.graph,
                stack,
                actions::LegacyInteractionInputAdapter::BuildKernelFrame(first),
                state);
            Require(firstResolved.changes.size() == 1, "initial chord edge must fire");

            actions::LegacyInteractionInputFrame held{};
            held.manifestEpoch = 42;
            held.contextRevision = 7;
            held.monotonicUs = 5'050;
            held.samples = {
                Sample(actions::ControlPath{ .kind = actions::ControlPathKind::DigitalButton, .code = 2 }, true, false, false, 4'950, 5'050),
                Sample(actions::ControlPath{ .kind = actions::ControlPathKind::DigitalButton, .code = 3 }, true, false, false, 5'000, 5'050)
            };
            const auto heldResolved = engine.Resolve(
                compiled.graph,
                stack,
                actions::LegacyInteractionInputAdapter::BuildKernelFrame(held),
                state);
            Require(heldResolved.changes.empty(), "level-held chord must not refire without a new edge");

            auto degradedFrame = actions::LegacyInteractionInputAdapter::BuildKernelFrame(held);
            degradedFrame.facts.monotonicUs = 5'100;
            degradedFrame.state.healthDegraded = true;
            const auto degradedResolved = engine.Resolve(compiled.graph, stack, degradedFrame, state);
            Require(degradedResolved.changes.empty(), "overflow/degraded frame must invalidate chord without dirty pulse output");

            actions::LegacyInteractionInputFrame refire{};
            refire.manifestEpoch = 42;
            refire.contextRevision = 7;
            refire.monotonicUs = 5'200;
            refire.samples = {
                Sample(actions::ControlPath{ .kind = actions::ControlPathKind::DigitalButton, .code = 2 }, true, false, false, 5'150, 5'200),
                Sample(actions::ControlPath{ .kind = actions::ControlPathKind::DigitalButton, .code = 3 }, true, true, false, 5'200, 5'200)
            };
            const auto refired = engine.Resolve(
                compiled.graph,
                stack,
                actions::LegacyInteractionInputAdapter::BuildKernelFrame(refire),
                state);
            Require(refired.changes.size() == 1, "new clean chord edge after invalidation must fire");
        }

        state.Reset();
        {
            actions::CompiledActionGraph malformed{};
            malformed.manifestEpoch = 42;
            malformed.actions = {
                actions::ActionDefinition{ .id = "NativeCombo", .valueKind = actions::ActionValueKind::Digital }
            };
            malformed.bindings.push_back(actions::CompiledGraphBinding{
                .bindingId = 99,
                .actionId = "NativeCombo",
                .actionSetId = "GameplayBase",
                .paths = {
                    actions::ControlPath{ .kind = actions::ControlPathKind::DigitalButton, .code = 3 }
                },
                .interaction = actions::InteractionSpec{
                    .kind = actions::InteractionKind::Chord,
                    .primaryPathIndex = 0,
                    .requiredPathIndices = { 1 },
                    .chordWindowUs = actions::kLegacyComboWindowUs,
                    .unordered = true
                },
                .matchPolicy = actions::BindingMatchPolicy::ExactOnly
            });
            malformed.lookups.bindingIndexById[99] = 0;
            malformed.lookups.bindingIdsByActionSetId["GameplayBase"].push_back(99);

            actions::LegacyInteractionInputFrame legacy{};
            legacy.manifestEpoch = 42;
            legacy.contextRevision = 7;
            legacy.monotonicUs = 5'500;
            legacy.samples = {
                Sample(actions::ControlPath{ .kind = actions::ControlPathKind::DigitalButton, .code = 3 }, true, true, false, 5'500, 5'500)
            };
            const auto resolved = engine.Resolve(
                malformed,
                stack,
                actions::LegacyInteractionInputAdapter::BuildKernelFrame(legacy),
                state);
            Require(resolved.changes.empty(), "malformed chord requiredPathIndices must fail closed without a dirty pulse");
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

    void RunRuntimePublishedSurfacePipelineTests()
    {
        gameplay::DualPadRuntime runtime;
        ResetRuntimeSurfaceState(runtime);

        RecordingPollOutputExecutor firstExecutor;
        const auto first = runtime.ProcessAssembledFrameForTests(
            StableMenuFrame(
                100,
                SourceEvidence(
                    presentation::DeviceFamily::KeyboardMouse,
                    1,
                    false,
                    1000)),
            firstExecutor);
        Require(first.output.outputApplySucceeded, "stable assembled frame must apply output before publishing surfaces");

        const auto& committedAfterStable =
            presentation::SkyrimCompatibilitySurface::GetSingleton().GetCommittedState();
        Require(committedAfterStable.epoch > 0, "stable assembled frame must update SkyrimCompatibilitySurface committed epoch");
        Require(
            !presentation::SkyrimCompatibilitySurface::GetSingleton().ShouldRefreshMenus(),
            "stable presentation publish must consume the menu platform refresh request");

        const auto scopeAfterStable = prompt::PromptRuntimeOwner::GetSingleton().GetPublishedPromptScopeForTests();
        Require(scopeAfterStable.state == prompt::PromptScopeState::Ready, "prompt scope must become ready after presentation publish");
        Require(scopeAfterStable.promptScopeRevision > 0, "prompt scope revision must advance after presentation publish");

        RecordingPollOutputExecutor gamepadExecutor;
        runtime.ProcessAssembledFrameForTests(
            StableMenuFrame(
                101,
                SourceEvidence(
                    presentation::DeviceFamily::Gamepad,
                    2,
                    true,
                    1010)),
            gamepadExecutor);
        Require(
            presentation::SkyrimCompatibilitySurface::GetSingleton().GetCommittedState().owner ==
                presentation::PresentationOwner::Gamepad,
            "stable gamepad evidence must update the independent presentation owner");
    }

    void RunPromptStatePublishedBeforeRefreshCallbackTests()
    {
        gameplay::DualPadRuntime runtime;
        ResetRuntimeSurfaceState(runtime);

        bool promptReadyBeforeRefreshCallback = false;
        presentation::SkyrimCompatibilitySurface::GetSingleton().SetMenuRefreshTaskSinkForTests([&](auto) {
            const auto scope = prompt::PromptRuntimeOwner::GetSingleton().GetPublishedPromptScopeForTests();
            promptReadyBeforeRefreshCallback =
                scope.state == prompt::PromptScopeState::Ready &&
                scope.uiContextId == context::UiContextId::Journal;
            presentation::SkyrimCompatibilitySurface::GetSingleton().CompleteQueuedRefreshForTests();
            return true;
        });

        RecordingPollOutputExecutor executor;
        const auto result = runtime.ProcessAssembledFrameForTests(
            StableMenuFrame(
                104,
                SourceEvidence(
                    presentation::DeviceFamily::Gamepad,
                    2,
                    true,
                    104'000)),
            executor);
        Require(result.output.outputApplySucceeded, "prompt-before-refresh setup must publish a stable frame");
        Require(promptReadyBeforeRefreshCallback, "PromptStatePublishedBeforeRefreshCallback");
    }

    void RunRuntimeLiveStyleGamepadPublishTests()
    {
        gameplay::DualPadRuntime runtime;
        ResetRuntimeSurfaceState(runtime);

        RecordingPollOutputExecutor baselineExecutor;
        (void)runtime.ProcessAssembledFrameForTests(
            StableMenuFrame(
                50,
                SourceEvidence(
                    presentation::DeviceFamily::KeyboardMouse,
                    1,
                    false,
                    500)),
            baselineExecutor);

        auto& producer = ingress::LiveInputFactProducer::GetSingleton();
        producer.ResetForTests();
        ingress::IngressHub::GetSingleton().ResetForTests();
        producer.PublishGamepadSourceEvidence(
            context::ContextResolver::GetSingleton().GetPublishedSnapshot(),
            60'000);
        (void)ingress::IngressHub::GetSingleton().PushPadSnapshot(LiveHidSnapshot(60, 0x0, 60'000));

        ingress::FrameAssembler assembler;
        const auto capture = ingress::IngressHub::GetSingleton().Capture(256);
        const auto frames = assembler.Assemble(
            capture.events,
            capture.latestPadState,
            capture.latestSourceEvidence);
        RecordingPollOutputExecutor executor;
        for (const auto& frame : frames) {
            (void)runtime.ProcessAssembledFrameForTests(frame, executor);
        }

        const auto& committed = presentation::SkyrimCompatibilitySurface::GetSingleton().GetCommittedState();
        Require(committed.owner == presentation::PresentationOwner::Gamepad, "live-style gamepad evidence must publish Gamepad owner");
        Require(
            presentation::SkyrimCompatibilitySurface::GetSingleton().IsUsingGamepadHook(),
            "live-style presentation changes must not replace the unscoped original engine result");

        const auto scope = prompt::PromptRuntimeOwner::GetSingleton().GetPublishedPromptScopeForTests();
        Require(scope.state == prompt::PromptScopeState::Ready, "live-style presentation publish must update prompt scope");
        Require(scope.promptScopeRevision > 0, "live-style presentation publish must advance prompt scope revision");
    }

    void RunRuntimeLiveKeyboardMouseEvidenceProducerTests()
    {
        gameplay::DualPadRuntime runtime;
        ResetRuntimeSurfaceState(runtime);
        auto& producer = ingress::LiveInputFactProducer::GetSingleton();
        producer.ResetForTests();
        ingress::IngressHub::GetSingleton().ResetForTests();
        ingress::FrameAssembler assembler;

        auto drive = [&]() {
            const auto capture = ingress::IngressHub::GetSingleton().Capture(256);
            const auto frames = assembler.Assemble(
                capture.events,
                capture.latestPadState,
                capture.latestSourceEvidence);
            RecordingPollOutputExecutor executor;
            for (const auto& frame : frames) {
                (void)runtime.ProcessAssembledFrameForTests(frame, executor);
            }
        };

        const auto& contextSnapshot = context::ContextResolver::GetSingleton().GetPublishedSnapshot();
        RecordingPollOutputExecutor baselineExecutor;
        (void)runtime.ProcessAssembledFrameForTests(
            StableMenuFrame(
                69,
                SourceEvidence(
                    presentation::DeviceFamily::KeyboardMouse,
                    1,
                    false,
                    69'000)),
            baselineExecutor);

        producer.PublishGamepadSourceEvidence(contextSnapshot, 70'000);
        drive();
        Require(
            presentation::SkyrimCompatibilitySurface::GetSingleton().IsUsingGamepadHook(),
            "gamepad input must leave the unscoped original engine result unchanged");
        Require(
            presentation::SkyrimCompatibilitySurface::GetSingleton().GetCommittedState().owner ==
                presentation::PresentationOwner::Gamepad,
            "gamepad input must publish Gamepad presentation owner");

        producer.PublishKeyboardSourceEvidence(contextSnapshot, 0x1E, 71'000);
        drive();
        Require(
            presentation::SkyrimCompatibilitySurface::GetSingleton().IsUsingGamepadHook(),
            "keyboard presentation evidence must not globally override the original engine result");
        Require(
            presentation::SkyrimCompatibilitySurface::GetSingleton().GetCommittedState().owner ==
                presentation::PresentationOwner::KeyboardMouse,
            "keyboard evidence must publish KeyboardMouse owner");

        producer.PublishGamepadSourceEvidence(contextSnapshot, 72'000);
        drive();
        Require(
            presentation::SkyrimCompatibilitySurface::GetSingleton().GetCommittedState().owner ==
                presentation::PresentationOwner::Gamepad,
            "gamepad reclaim must restore Gamepad owner");

        producer.PublishMouseMoveSourceEvidence(contextSnapshot, 12, -4, 73'000);
        drive();
        const auto& afterMouseMove =
            presentation::SkyrimCompatibilitySurface::GetSingleton().GetCommittedState();
        Require(
            afterMouseMove.owner == presentation::PresentationOwner::Gamepad,
            "pointer-only candidate must not change menu owner before owner-tick promotion");
        Require(
            afterMouseMove.cursorOwner == presentation::CursorOwner::KeyboardMouse,
            "I-CURSOR shadow must preserve the previously committed KBM cursor owner");

        producer.PublishGamepadSourceEvidence(contextSnapshot, 74'000);
        drive();
        producer.PublishMouseButtonSourceEvidence(contextSnapshot, 75'000);
        drive();
        const auto& afterMouseButton =
            presentation::SkyrimCompatibilitySurface::GetSingleton().GetCommittedState();
        Require(
            afterMouseButton.owner == presentation::PresentationOwner::KeyboardMouse,
            "mouse button evidence must publish KeyboardMouse owner");
        Require(
            afterMouseButton.cursorOwner == presentation::CursorOwner::KeyboardMouse,
            "mouse button evidence must publish KeyboardMouse cursor owner");

        producer.PublishGamepadSourceEvidence(contextSnapshot, 76'000);
        drive();
        producer.MarkSyntheticKeyboardScancode(0x64, 1, 250'000, 76'100);
        producer.PublishKeyboardSourceEvidence(contextSnapshot, 0x64, 76'200);
        drive();
        Require(
            presentation::SkyrimCompatibilitySurface::GetSingleton().IsUsingGamepadHook(),
            "synthetic keyboard window must leave the original engine result unchanged");
        Require(
            presentation::SkyrimCompatibilitySurface::GetSingleton().GetCommittedState().owner ==
                presentation::PresentationOwner::Gamepad,
            "synthetic keyboard window must not take over presentation owner");
    }

    void RunRuntimeGraphSkewHealthTests()
    {
        gameplay::DualPadRuntime runtime;
        ResetRuntimeSurfaceState(runtime);

        actions::CompiledActionGraph mismatchedGraph{};
        mismatchedGraph.manifestEpoch = 77;
        Require(
            actions::CompiledActionGraphPublisher::GetRuntimeOwner().Publish(mismatchedGraph, 77).ok,
            "test setup must publish a graph with a mismatched manifest epoch");

        RecordingPollOutputExecutor executor;
        const auto result = runtime.ProcessAssembledFrameForTests(
            StableMenuFrame(
                95,
                SourceEvidence(
                    presentation::DeviceFamily::Gamepad,
                    2,
                    true,
                    95'000)),
            executor);

        Require(result.RuntimeHealthDegraded(), "runtime graph epoch skew must surface as a health marker");
        Require(
            gameplay::HasRuntimeHealthReason(
                result.runtimeHealthReasons,
                gameplay::RuntimeHealthReason::ManifestEpochSkew),
            "runtime graph epoch skew must expose ManifestEpochSkew");
        Require(
            result.projectionFrame.gamepadPlan.transientDigital.count == 0,
            "runtime graph epoch skew must fail closed without transient gamepad commands");
    }

    void RunRuntimeDiagnosticsProjectionTests()
    {
        ingress::AssembledFactFrame frame{};
        frame.kind = ingress::AssembledFrameKind::Stable;
        frame.firstSeq = 700;
        frame.lastSeq = 701;
        frame.facts.overflowCompaction = ingress::OverflowCompactionDebugSummary{
            .transitionObserved = true,
            .typedCompactionApplied = true,
            .retainedManifest = true,
            .retainedUi = true,
            .retainedDeviceFamily = true,
            .retainedSourceEvidence = true,
            .droppedControlSamples = true,
            .droppedPulseLedger = true,
            .droppedLegacySnapshot = true,
            .debugSummary = "overflow_transition=true typed_compaction=true retained_manifest=true retained_ui=true retained_device_family=true retained_source_evidence=true dropped_control_samples=true dropped_pulse_ledger=true dropped_legacy_snapshot=true"
        };

        auto mask = gameplay::RuntimeHealthMask(gameplay::RuntimeHealthReason::None);
        for (const auto reason : {
                 gameplay::RuntimeHealthReason::GraphUnavailable,
                 gameplay::RuntimeHealthReason::ManifestEpochSkew,
                 gameplay::RuntimeHealthReason::ContextRevisionSkew,
                 gameplay::RuntimeHealthReason::QueueOverflow,
                 gameplay::RuntimeHealthReason::SequenceGap,
                 gameplay::RuntimeHealthReason::BoundaryMismatch,
                 gameplay::RuntimeHealthReason::PromptScopeFrozen,
                 gameplay::RuntimeHealthReason::UpstreamXInputRouteFailed,
                 gameplay::RuntimeHealthReason::SkyrimCompatSurfaceHookFailed,
                 gameplay::RuntimeHealthReason::SkyrimCompatSurfacePartialInstall,
                 gameplay::RuntimeHealthReason::MenuObserverPartial,
                 gameplay::RuntimeHealthReason::MenuObserverUnavailable,
                 gameplay::RuntimeHealthReason::MenuIdentityDegraded }) {
            mask = gameplay::AddRuntimeHealthReason(mask, reason);
        }

        const auto hook = presentation::detail::MakeHookInstallResult(
            presentation::HookInstallStatus::PartialInstall,
            "exception_after_patch_started");
        const auto snapshot = gameplay::ProjectRuntimeDebugSnapshot(gameplay::RuntimeDebugProjectionInput{
            .frame = frame,
            .runtimeHealthReasons = mask,
            .runtimeHealthDebugReason = "hook_partial_install",
            .outputApplySucceeded = true,
            .hookInstall = hook
        });

        Require(snapshot.runtimeHealthDegraded, "all-reasons mask must project as degraded");
        for (const auto expected : {
                 "GraphUnavailable",
                 "ManifestEpochSkew",
                 "ContextRevisionSkew",
                 "QueueOverflow",
                 "SequenceGap",
                 "BoundaryMismatch",
                 "PromptScopeFrozen",
                 "UpstreamXInputRouteFailed",
                 "SkyrimCompatSurfaceHookFailed",
                 "SkyrimCompatSurfacePartialInstall",
                 "MenuObserverPartial",
                 "MenuObserverUnavailable",
                 "MenuIdentityDegraded" }) {
            Require(Contains(snapshot.runtimeHealthReasonNames, expected), "reason mask projection missed a reason name");
        }
        Require(
            snapshot.hookInstallStatusName == "partial_install",
            "debug snapshot must expose hook partial_install status");
        Require(
            snapshot.hookInstallDebugReason == "exception_after_patch_started",
            "debug snapshot must expose hook debug reason");
        Require(
            snapshot.hookOperationalStateName == "safe_passthrough" &&
                snapshot.hookFailureDispositionName == "rolled_back",
            "debug snapshot must expose compat operational state and rollback disposition");
        Require(snapshot.promptState == gameplay::RuntimePromptDebugState::Frozen, "degraded stable frame must freeze prompt");
        Require(
            snapshot.promptDebugReason.find("PromptScopeFrozen") != std::string::npos,
            "prompt freeze reason must include PromptScopeFrozen");
        Require(snapshot.overflowTransition, "debug snapshot must expose overflow transition");
        Require(snapshot.overflowTypedCompaction, "debug snapshot must expose typed overflow compaction");
        Require(
            snapshot.overflowCompactionSummary.find("retained_source_evidence=true") != std::string::npos,
            "debug snapshot must expose typed compaction retained facts");

        const auto unsupportedSnapshot = gameplay::ProjectRuntimeDebugSnapshot(gameplay::RuntimeDebugProjectionInput{
            .frame = frame,
            .runtimeHealthReasons = gameplay::AddRuntimeHealthReason(
                gameplay::RuntimeHealthMask(gameplay::RuntimeHealthReason::PromptScopeFrozen),
                gameplay::RuntimeHealthReason::SkyrimCompatSurfaceHookFailed),
            .runtimeHealthDebugReason = "unsupported_runtime_1.6.640",
            .outputApplySucceeded = false,
            .hookInstall = presentation::detail::MakeHookInstallResult(
                presentation::HookInstallStatus::UnsupportedRuntime,
                "unsupported_runtime_1.6.640")
        });
        Require(
            unsupportedSnapshot.hookInstallStatusName == "unsupported_runtime",
            "debug snapshot must expose unsupported_runtime hook status");
        Require(
            unsupportedSnapshot.hookInstallDebugReason == "unsupported_runtime_1.6.640",
            "debug snapshot must expose unsupported runtime debug reason");

        gameplay::RuntimeDiagnosticsLogState logState;
        Require(gameplay::ShouldEmitRuntimeDebugLog(logState, snapshot), "first degraded snapshot must log");
        Require(!gameplay::ShouldEmitRuntimeDebugLog(logState, snapshot), "duplicate degraded snapshot must not log");

        auto recovered = snapshot;
        recovered.runtimeHealthDegraded = false;
        recovered.runtimeHealthReasons = gameplay::RuntimeHealthMask(gameplay::RuntimeHealthReason::None);
        recovered.runtimeHealthReasonNames = gameplay::RuntimeHealthReasonNames(recovered.runtimeHealthReasons);
        recovered.runtimeHealthReasonSummary = gameplay::RuntimeHealthReasonSummary(recovered.runtimeHealthReasons);
        recovered.runtimeHealthDebugReason.clear();
        recovered.promptState = gameplay::RuntimePromptDebugState::Ready;
        recovered.promptStateName = gameplay::ToString(recovered.promptState);
        recovered.promptDebugReason = "published";
        recovered.overflowTransition = false;
        recovered.overflowTypedCompaction = false;
        recovered.overflowCompactionSummary.clear();
        Require(gameplay::ShouldEmitRuntimeDebugLog(logState, recovered), "health recovery transition must log once");
        Require(!gameplay::ShouldEmitRuntimeDebugLog(logState, recovered), "duplicate recovery snapshot must not log");

        ingress::AssembledFactFrame transition{};
        transition.kind = ingress::AssembledFrameKind::Transition;
        transition.transition.reason = ingress::TransitionReason::SequenceGap;
        const auto transitionSnapshot = gameplay::ProjectRuntimeDebugSnapshot(gameplay::RuntimeDebugProjectionInput{
            .frame = transition,
            .runtimeHealthReasons = gameplay::RuntimeHealthMask(gameplay::RuntimeHealthReason::SequenceGap),
            .outputApplySucceeded = false,
            .hookInstall = presentation::detail::MakeHookInstallResult(
                presentation::HookInstallStatus::Success,
                "installed")
        });
        Require(
            transitionSnapshot.promptState == gameplay::RuntimePromptDebugState::Unavailable,
            "transition frame prompt state must be unavailable");
        Require(
            transitionSnapshot.promptDebugReason == "transition_frame",
            "transition frame must expose prompt unavailable reason");
    }

    void RunRuntimeDegradedFramePromptPublishContractTests()
    {
        gameplay::DualPadRuntime runtime;
        ResetRuntimeSurfaceState(runtime);

        RecordingPollOutputExecutor baselineExecutor;
        (void)runtime.ProcessAssembledFrameForTests(
            StableMenuFrame(
                94,
                SourceEvidence(
                    presentation::DeviceFamily::KeyboardMouse,
                    1,
                    false,
                    94'000)),
            baselineExecutor);

        const auto beforeScope = prompt::PromptRuntimeOwner::GetSingleton().GetPublishedPromptScopeForTests();
        const auto beforeCompatEpoch = presentation::SkyrimCompatibilitySurface::GetSingleton().GetCommittedState().epoch;
        Require(beforeScope.state == prompt::PromptScopeState::Ready, "baseline frame must publish a prompt scope");

        actions::CompiledActionGraph mismatchedGraph{};
        mismatchedGraph.manifestEpoch = 77;
        Require(
            actions::CompiledActionGraphPublisher::GetRuntimeOwner().Publish(mismatchedGraph, 77).ok,
            "test setup must publish a graph with a mismatched manifest epoch");

        RecordingPollOutputExecutor degradedExecutor;
        const auto result = runtime.ProcessAssembledFrameForTests(
            StableMenuFrame(
                96,
                SourceEvidence(
                    presentation::DeviceFamily::Gamepad,
                    2,
                    true,
                    96'000)),
            degradedExecutor);

        Require(result.RuntimeHealthDegraded(), "graph skew frame must be marked degraded");
        Require(
            gameplay::HasRuntimeHealthReason(
                result.runtimeHealthReasons,
                gameplay::RuntimeHealthReason::ManifestEpochSkew),
            "graph skew frame must expose ManifestEpochSkew");
        Require(
            gameplay::HasRuntimeHealthReason(
                result.runtimeHealthReasons,
                gameplay::RuntimeHealthReason::PromptScopeFrozen),
            "graph skew degraded stable frame must expose PromptScopeFrozen");
        const auto afterCompat = presentation::SkyrimCompatibilitySurface::GetSingleton().GetCommittedState();
        Require(afterCompat.epoch > beforeCompatEpoch, "degraded frame may still publish Skyrim compatibility owner state");
        Require(afterCompat.owner == presentation::PresentationOwner::Gamepad, "degraded frame must preserve public owner projection");

        const auto afterScope = prompt::PromptRuntimeOwner::GetSingleton().GetPublishedPromptScopeForTests();
        Require(
            afterScope.promptScopeRevision == beforeScope.promptScopeRevision,
            "degraded frame must not publish a new prompt scope");
        Require(
            afterScope.manifestEpoch == beforeScope.manifestEpoch,
            "degraded frame must not move prompt scope to a mismatched graph epoch");
        const auto& debug = runtime.GetLastDebugSnapshot();
        Require(debug.runtimeHealthDegraded, "degraded frame must publish a runtime debug snapshot");
        Require(debug.promptState == gameplay::RuntimePromptDebugState::Frozen, "debug snapshot must expose prompt freeze");
        Require(
            debug.promptDebugReason.find("ManifestEpochSkew") != std::string::npos,
            "debug snapshot prompt freeze reason must include manifest skew");
    }

    void AssertSkyrimCompatSurfaceFailureDegradesPresentationOnly(
        presentation::HookInstallStatus status,
        gameplay::RuntimeHealthReason expectedReason,
        std::string_view debugReason)
    {
        gameplay::DualPadRuntime runtime;
        ResetRuntimeSurfaceState(runtime);

        const auto beforeScope = prompt::PromptRuntimeOwner::GetSingleton().GetPublishedPromptScopeForTests();
        presentation::SkyrimCompatibilitySurface::GetSingleton().ForceInstallResultForTests(
            presentation::detail::MakeHookInstallResult(status, debugReason));

        RecordingPollOutputExecutor executor;
        const auto result = runtime.ProcessAssembledFrameForTests(
            StableMenuFrame(
                97,
                SourceEvidence(
                    presentation::DeviceFamily::Gamepad,
                    2,
                    true,
                    97'000)),
            executor);

        Require(result.RuntimeHealthDegraded(), "SkyrimCompatSurface failure must surface as degraded runtime health");
        Require(
            gameplay::HasRuntimeHealthReason(
                result.runtimeHealthReasons,
                expectedReason),
            "SkyrimCompatSurface failure must expose split runtime health reason");
        Require(
            !gameplay::HasRuntimeHealthReason(
                result.runtimeHealthReasons,
                gameplay::RuntimeHealthReason::PromptScopeFrozen),
            "SkyrimCompatSurface failure must not freeze prompt scope by default");
        Require(
            result.runtimeHealthDebugReason.find(debugReason) != std::string::npos,
            "SkyrimCompatSurface failure must carry debug reason");
        Require(!executor.steps.empty(), "SkyrimCompatSurface failure must not disable native/action output");
        Require(result.output.outputApplySucceeded, "SkyrimCompatSurface failure must report output apply success");
        Require(
            result.projectionFrame.gamepadPlan.transientDigital.count == 0 &&
                result.projectionFrame.helperPlan.commands.count == 0,
            "empty test graph must still resolve no action commands");

        const auto afterScope = prompt::PromptRuntimeOwner::GetSingleton().GetPublishedPromptScopeForTests();
        Require(
            afterScope.promptScopeRevision > beforeScope.promptScopeRevision,
            "SkyrimCompatSurface failure must still publish prompt scope");
        Require(afterScope.state == prompt::PromptScopeState::Ready, "SkyrimCompatSurface failure must keep prompt scope ready");
        const auto& debug = runtime.GetLastDebugSnapshot();
        Require(debug.hookInstallStatusName == presentation::ToString(status), "debug snapshot must expose hook status");
        Require(
            debug.hookInstallDebugReason == debugReason,
            "debug snapshot must expose hook debug reason");
        Require(
            debug.hookOperationalStateName ==
                dualpad::input::patching::ToString(
                    presentation::detail::MakeHookInstallResult(status, debugReason).operationalState),
            "debug snapshot must expose compat operational state");
        Require(debug.promptState == gameplay::RuntimePromptDebugState::Ready, "SkyrimCompatSurface failure must not freeze prompt in debug snapshot");
        presentation::SkyrimCompatibilitySurface::GetSingleton().ForceInstallResultForTests(
            presentation::detail::MakeHookInstallResult(
                presentation::HookInstallStatus::Success,
                "test_hook_installed"));
    }

    void RunRuntimeSkyrimCompatSurfaceFailurePresentationOnlyTests()
    {
        AssertSkyrimCompatSurfaceFailureDegradesPresentationOnly(
            presentation::HookInstallStatus::PartialInstall,
            gameplay::RuntimeHealthReason::SkyrimCompatSurfacePartialInstall,
            "exception_after_patch_started");
        AssertSkyrimCompatSurfaceFailureDegradesPresentationOnly(
            presentation::HookInstallStatus::UnsafePartial,
            gameplay::RuntimeHealthReason::SkyrimCompatSurfacePartialInstall,
            "rollback_expected_current_mismatch");
        AssertSkyrimCompatSurfaceFailureDegradesPresentationOnly(
            presentation::HookInstallStatus::UnsupportedRuntime,
            gameplay::RuntimeHealthReason::SkyrimCompatSurfaceHookFailed,
            "unsupported_runtime_1.6.640");
    }

    void AssertUpstreamRouteFailureFailsClosed(
        dualpad::input::UpstreamGamepadHookInstallStatus status,
        std::string_view debugReason)
    {
        gameplay::DualPadRuntime runtime;
        ResetRuntimeSurfaceState(runtime);

        const auto beforeScope = prompt::PromptRuntimeOwner::GetSingleton().GetPublishedPromptScopeForTests();
        dualpad::input::detail::ForceUpstreamRouteInstallSnapshotForTests(dualpad::input::UpstreamRouteInstallSnapshot{
            .configured = true,
            .installAttempted = true,
            .installed = false,
            .failed = dualpad::input::HasUpstreamGamepadHookInstallFailed(status),
            .status = status,
            .operationalState = dualpad::input::ResolveUpstreamHookOperationalState(status),
            .disposition = dualpad::input::ResolveUpstreamHookFailureDisposition(status),
            .debugReason = std::string(debugReason)
        });

        RecordingPollOutputExecutor executor;
        const auto result = runtime.ProcessAssembledFrameForTests(
            StableMenuFrame(
                98,
                SourceEvidence(
                    presentation::DeviceFamily::Gamepad,
                    2,
                    true,
                    98'000)),
            executor);

        Require(result.RuntimeHealthDegraded(), "upstream route install failure must degrade runtime health");
        Require(
            gameplay::HasRuntimeHealthReason(
                result.runtimeHealthReasons,
                gameplay::RuntimeHealthReason::UpstreamXInputRouteFailed),
            "upstream route install failure must expose UpstreamXInputRouteFailed");
        Require(
            gameplay::HasRuntimeHealthReason(
                result.runtimeHealthReasons,
                gameplay::RuntimeHealthReason::PromptScopeFrozen),
            "upstream route install failure must freeze prompt scope");
        Require(
            result.runtimeHealthDebugReason == debugReason,
            "upstream route install failure must carry debug reason");
        Require(executor.steps.empty(), "upstream route install failure must not apply native output");
        Require(!result.output.outputApplySucceeded, "upstream route install failure must not report output success");
        Require(
            result.projectionFrame.gamepadPlan.transientDigital.count == 0 &&
                result.projectionFrame.helperPlan.commands.count == 0,
            "upstream route install failure must fail closed without resolved action commands");

        const auto afterScope = prompt::PromptRuntimeOwner::GetSingleton().GetPublishedPromptScopeForTests();
        Require(
            afterScope.promptScopeRevision == beforeScope.promptScopeRevision,
            "upstream route install failure must not publish a new prompt scope");
        const auto& debug = runtime.GetLastDebugSnapshot();
        Require(
            debug.upstreamRouteInstallStatusName == dualpad::input::ToString(status),
            "debug snapshot must expose upstream route install status");
        Require(
            debug.upstreamRouteInstallDebugReason == debugReason,
            "debug snapshot must expose upstream route debug reason");
        Require(
            debug.upstreamOperationalStateName ==
                dualpad::input::patching::ToString(
                    dualpad::input::ResolveUpstreamHookOperationalState(status)) &&
                debug.upstreamFailureDispositionName ==
                    dualpad::input::patching::ToString(
                        dualpad::input::ResolveUpstreamHookFailureDisposition(status)),
            "debug snapshot must expose upstream operational state and disposition");
        Require(
            debug.promptState == gameplay::RuntimePromptDebugState::Frozen,
            "upstream route failure must freeze prompt in debug snapshot");

        dualpad::input::detail::ResetUpstreamRouteInstallSnapshotForTests();
    }

    void RunRuntimeUpstreamRouteInstallFailureFailClosedTests()
    {
        AssertUpstreamRouteFailureFailsClosed(
            dualpad::input::UpstreamGamepadHookInstallStatus::SignatureMismatch,
            "is_using_gamepad_call_signature_mismatch");
        AssertUpstreamRouteFailureFailsClosed(
            dualpad::input::UpstreamGamepadHookInstallStatus::UnsupportedRuntime,
            "unsupported_runtime_1.6.640");
        AssertUpstreamRouteFailureFailsClosed(
            dualpad::input::UpstreamGamepadHookInstallStatus::UnsafePartial,
            "unsafe_partial_patch");
    }

    void RunRuntimeDisabledUpstreamRouteDoesNotFailClosedTests()
    {
        gameplay::DualPadRuntime runtime;
        ResetRuntimeSurfaceState(runtime);
        dualpad::input::detail::ForceUpstreamRouteInstallSnapshotForTests(dualpad::input::UpstreamRouteInstallSnapshot{
            .configured = false,
            .installAttempted = false,
            .installed = false,
            .failed = false,
            .status = dualpad::input::UpstreamGamepadHookInstallStatus::DisabledByConfig,
            .operationalState = dualpad::input::patching::HookOperationalState::Disabled,
            .disposition = dualpad::input::patching::HookFailureDisposition::None,
            .debugReason = "disabled_by_config"
        });

        RecordingPollOutputExecutor executor;
        const auto result = runtime.ProcessAssembledFrameForTests(
            StableMenuFrame(
                99,
                SourceEvidence(
                    presentation::DeviceFamily::Gamepad,
                    2,
                    true,
                    99'000)),
            executor);

        Require(
            !gameplay::HasRuntimeHealthReason(
                result.runtimeHealthReasons,
                gameplay::RuntimeHealthReason::UpstreamXInputRouteFailed),
            "disabled upstream route must not expose UpstreamXInputRouteFailed");
        Require(
            result.runtimeHealthDebugReason.empty(),
            "disabled upstream route must not set runtime health debug reason");
        const auto& debug = runtime.GetLastDebugSnapshot();
        Require(
            debug.upstreamRouteInstallStatusName == "disabled_by_config",
            "debug snapshot must expose disabled upstream route status");
        Require(
            !debug.upstreamRouteInstallFailed,
            "disabled upstream route must not be marked failed in debug snapshot");

        dualpad::input::detail::ResetUpstreamRouteInstallSnapshotForTests();
    }

    void RunRuntimeMenuObserverDegradedHealthTests()
    {
        gameplay::DualPadRuntime runtime;
        ResetRuntimeSurfaceState(runtime);

        auto degradedMenu = context::ResolvedContextSnapshot{};
        degradedMenu.hostMode = context::HostMode::Menu;
        degradedMenu.uiContextId = context::UiContextId::UnknownTrackedMenu;
        degradedMenu.actionSetStack = actions::ActionSetStack{
            .baseSetId = "MenuBase",
            .layerIds = { "UnknownTrackedMenuLayer" },
            .scopeAnchorIds = { "MenuBase", "UnknownTrackedMenuLayer" }
        };
        degradedMenu.presentationPolicyId = "Menu";
        degradedMenu.contextRevision = 70;
        degradedMenu.menuStackRevision = 71;
        degradedMenu.legacyInputContext = dualpad::input::InputContext::Menu;
        degradedMenu.legacyContextEpoch = 2;
        degradedMenu.menuObserverCompleteness = menu::ObserverCompleteness::Partial;
        context::ContextResolver::GetSingleton().PublishSnapshotForReplayTests(degradedMenu);

        RecordingPollOutputExecutor partialExecutor;
        const auto partial = runtime.ProcessAssembledFrameForTests(
            StableMenuFrame(
                102,
                SourceEvidence(
                    presentation::DeviceFamily::Gamepad,
                    2,
                    true,
                    102'000)),
            partialExecutor);
        Require(partial.RuntimeHealthDegraded(), "Observer Partial must mark runtime health degraded");
        Require(
            gameplay::HasRuntimeHealthReason(
                partial.runtimeHealthReasons,
                gameplay::RuntimeHealthReason::MenuObserverPartial),
            "Observer Partial must expose MenuObserverPartial");
        Require(partial.output.outputApplySucceeded, "Observer Partial must not disable native output");
        Require(
            degradedMenu.actionSetStack.baseSetId == "MenuBase",
            "Observer partial degraded menu snapshot must not dispatch GameplayBase actions");

        degradedMenu.contextRevision = 72;
        degradedMenu.menuStackRevision = 73;
        degradedMenu.menuObserverCompleteness = menu::ObserverCompleteness::Unavailable;
        context::ContextResolver::GetSingleton().PublishSnapshotForReplayTests(degradedMenu);

        RecordingPollOutputExecutor unavailableExecutor;
        const auto unavailable = runtime.ProcessAssembledFrameForTests(
            StableMenuFrame(
                103,
                SourceEvidence(
                    presentation::DeviceFamily::Gamepad,
                    3,
                    true,
                    103'000)),
            unavailableExecutor);
        Require(unavailable.RuntimeHealthDegraded(), "Observer Unavailable must mark runtime health degraded");
        Require(
            gameplay::HasRuntimeHealthReason(
                unavailable.runtimeHealthReasons,
                gameplay::RuntimeHealthReason::MenuObserverUnavailable),
            "ObserverUnavailable_DoesNotDispatchGameplayActions must expose MenuObserverUnavailable");
        Require(unavailable.output.outputApplySucceeded, "Observer Unavailable must not disable native output");
    }

    void RunRuntimePresentationUsesFrameBoundContextTests()
    {
        gameplay::DualPadRuntime runtime;
        ResetRuntimeSurfaceState(runtime);

        auto frame = StableMenuFrame(
            98,
            SourceEvidence(
                presentation::DeviceFamily::Gamepad,
                2,
                true,
                98'000));
        const auto frameContextRevision = frame.facts.contextRevision;

        ContextInterleavingPollOutputExecutor executor;
        const auto result = runtime.ProcessAssembledFrameForTests(frame, executor);

        Require(
            !result.RuntimeHealthDegraded(),
            "context interleaving after envelope binding must not degrade the current frame");
        const auto afterCompat = presentation::SkyrimCompatibilitySurface::GetSingleton().GetCommittedState();
        Require(
            afterCompat.uiContextId == context::UiContextId::Journal,
            "presentation projection must use the frame-bound context instead of a later active context");
        Require(
            afterCompat.contextRevision == frameContextRevision,
            "presentation projection must preserve the frame-bound context revision");

        const auto promptScope = prompt::PromptRuntimeOwner::GetSingleton().GetPublishedPromptScopeForTests();
        Require(
            promptScope.uiContextId == context::UiContextId::Journal,
            "prompt publish must use the same frame-bound context as presentation projection");
        Require(
            promptScope.manifestEpoch == frame.facts.manifestEpoch,
            "prompt publish must use the frame-bound manifest epoch");
    }

    void RunRuntimeTransitionRecoveryContractTests()
    {
        gameplay::DualPadRuntime runtime;
        ResetRuntimeSurfaceState(runtime);

        const auto initialEpoch =
            presentation::SkyrimCompatibilitySurface::GetSingleton().GetCommittedState().epoch;
        RecordingPollOutputExecutor transitionExecutor;
        const auto transition = runtime.ProcessAssembledFrameForTests(
            HardTransitionFrame(200),
            transitionExecutor);
        Require(
            transitionExecutor.steps.empty(),
            "transition frame must not call gameplay projection or output executor");
        Require(
            !transition.output.outputApplySucceeded,
            "transition frame must not report stable output apply");
        Require(
            presentation::SkyrimCompatibilitySurface::GetSingleton().GetCommittedState().epoch == initialEpoch,
            "transition frame must not publish Skyrim compatibility state");
        const auto transitionDebug = runtime.GetLastDebugSnapshot();
        Require(
            transitionDebug.promptState == gameplay::RuntimePromptDebugState::Unavailable,
            "transition debug snapshot must expose prompt unavailable state");
        Require(
            transitionDebug.promptDebugReason == "transition_frame",
            "transition debug snapshot must expose prompt unavailable reason");
        Require(transitionDebug.overflowTransition, "queue overflow transition must be visible in debug snapshot");

        RecordingPollOutputExecutor stableExecutor;
        const auto stable = runtime.ProcessAssembledFrameForTests(
            StableMenuFrame(
                201,
                SourceEvidence(
                    presentation::DeviceFamily::Gamepad,
                    2,
                    true,
                    2010)),
            stableExecutor);
        Require(stable.output.outputApplySucceeded, "next stable frame after transition must apply output");
        Require(
            std::find(
                stableExecutor.steps.begin(),
                stableExecutor.steps.end(),
                gameplay::PollOutputApplyStep::ClearNativeOutput) != stableExecutor.steps.end(),
            "next stable frame after hard transition must clear native output from pending recovery");
        Require(
            std::find(
                stableExecutor.steps.begin(),
                stableExecutor.steps.end(),
                gameplay::PollOutputApplyStep::CommitCleanRecoveryBaseline) != stableExecutor.steps.end(),
            "next stable frame after hard transition must commit recovery clean baseline after apply");
    }

    bool HasStep(
        const std::vector<gameplay::PollOutputApplyStep>& steps,
        gameplay::PollOutputApplyStep expected)
    {
        return std::find(steps.begin(), steps.end(), expected) != steps.end();
    }

    void RunRuntimeSoftSequenceGapDoesNotClearAuthoritativePollTests()
    {
        gameplay::DualPadRuntime runtime;
        ResetRuntimeSurfaceState(runtime);

        RecordingPollOutputExecutor transitionExecutor;
        (void)runtime.ProcessAssembledFrameForTests(
            SoftSequenceGapTransitionFrame(220),
            transitionExecutor);
        Require(
            transitionExecutor.steps.empty(),
            "TransitionFrame_DoesNotDispatchActions for soft sequence gap");

        RecordingPollOutputExecutor stableExecutor;
        const auto stable = runtime.ProcessAssembledFrameForTests(
            StableMenuFrame(
                221,
                SourceEvidence(
                    presentation::DeviceFamily::Gamepad,
                    2,
                    true,
                    2210)),
            stableExecutor);
        Require(stable.output.outputApplySucceeded, "stable frame after soft sequence gap must apply output");
        Require(
            !HasStep(stableExecutor.steps, gameplay::PollOutputApplyStep::ClearNativeOutput),
            "RuntimeSnapshotSeqGap_WithoutBoundaryChange_DoesNotClearAuthoritativePoll");
        Require(
            !HasStep(stableExecutor.steps, gameplay::PollOutputApplyStep::ClearHelperOutput),
            "SoftGap must not clear helper output");
        Require(
            !HasStep(stableExecutor.steps, gameplay::PollOutputApplyStep::ClearSustainedDigitalAggregator),
            "SoftGap must not clear sustained output");
        Require(
            !HasStep(stableExecutor.steps, gameplay::PollOutputApplyStep::ClearProjectionStickyOwners),
            "SoftGap must not clear projection sticky owners");
        Require(
            HasStep(stableExecutor.steps, gameplay::PollOutputApplyStep::CommitCleanRecoveryBaseline),
            "soft sequence gap must commit clean recovery baseline after a clean stable frame");
    }

    void RunHardRecoveryConsumesPendingResetOnceTests(
        ingress::TransitionReason reason,
        std::string_view assertionName)
    {
        gameplay::DualPadRuntime runtime;
        ResetRuntimeSurfaceState(runtime);

        RecordingPollOutputExecutor transitionExecutor;
        (void)runtime.ProcessAssembledFrameForTests(
            HardTransitionFrame(230, reason),
            transitionExecutor);
        Require(transitionExecutor.steps.empty(), "TransitionFrame_DoesNotDispatchActions for hard recovery");

        RecordingPollOutputExecutor firstStableExecutor;
        const auto firstStable = runtime.ProcessAssembledFrameForTests(
            StableMenuFrame(
                231,
                SourceEvidence(
                    presentation::DeviceFamily::Gamepad,
                    2,
                    true,
                    2310)),
            firstStableExecutor);
        Require(firstStable.output.outputApplySucceeded, "first stable frame after hard recovery must apply output");
        Require(
            HasStep(firstStableExecutor.steps, gameplay::PollOutputApplyStep::ClearNativeOutput),
            assertionName);

        RecordingPollOutputExecutor secondStableExecutor;
        const auto secondStable = runtime.ProcessAssembledFrameForTests(
            StableMenuFrame(
                232,
                SourceEvidence(
                    presentation::DeviceFamily::Gamepad,
                    2,
                    true,
                    2320)),
            secondStableExecutor);
        Require(secondStable.output.outputApplySucceeded, "second stable frame after hard recovery must apply output");
        Require(
            !HasStep(secondStableExecutor.steps, gameplay::PollOutputApplyStep::ClearNativeOutput),
            assertionName);
        Require(
            !HasStep(secondStableExecutor.steps, gameplay::PollOutputApplyStep::ClearSustainedDigitalAggregator),
            "hard recovery pending clear must be consumed after one stable frame");
    }

    void RunManifestEpochChangeHardResetsOnceTests()
    {
        RunHardRecoveryConsumesPendingResetOnceTests(
            ingress::TransitionReason::ManifestEpochChanged,
            "ManifestEpochChange_HardResetsOnce");
    }

    void RunContextEpochChangeHardResetsOnceTests()
    {
        RunHardRecoveryConsumesPendingResetOnceTests(
            ingress::TransitionReason::ExplicitReset,
            "ContextEpochChange_HardResetsOnce");
    }

    void RunRuntimeFrameEnvelopeUsesActiveConfigGraphForGameplayBindingsTests()
    {
        gameplay::DualPadRuntime runtime;
        runtime.ResetForTests();
        config::AtomicConfigReloader::GetSingleton().ResetForTests();
        context::ContextResolver::GetSingleton().ResetForTests();
        actions::CompiledActionGraphPublisher::GetRuntimeOwner().ResetForTests();
        prompt::PromptRuntimeOwner::GetSingleton().ResetForTests();
        LoadRuntimeConfigForGameplayBindingTests();

        const auto contextSnapshot = PublishGameplayContext();
        const auto bundle = config::AtomicConfigReloader::GetSingleton().GetActiveBundleSnapshot();
        Require(bundle != nullptr, "runtime binding test needs active config bundle");

        ingress::AssembledFactFrame frame{};
        frame.kind = ingress::AssembledFrameKind::Stable;
        frame.firstSeq = 300;
        frame.lastSeq = 300;
        frame.boundaryKey = ingress::IngressBoundaryKey{
            static_cast<std::uint32_t>(bundle->manifestEpoch),
            contextSnapshot.contextRevision,
            contextSnapshot.menuStackRevision,
            1
        };
        frame.facts.manifestEpoch = frame.boundaryKey.manifestEpoch;
        frame.facts.contextRevision = frame.boundaryKey.contextRevision;
        frame.facts.menuStackRevision = frame.boundaryKey.menuStackRevision;
        frame.facts.deviceFamilyRevision = frame.boundaryKey.deviceFamilyRevision;
        frame.facts.monotonicUs = 300'000;
        frame.facts.controlSamples = {
            Sample(actions::ControlPath{ .kind = actions::ControlPathKind::DigitalButton, .code = 4 }, true, true, false, 300'000, 300'000),
            AxisSample(static_cast<std::uint32_t>(dualpad::input::PadAxisId::RightStickX), 0.5f, 300'000),
            AxisSample(static_cast<std::uint32_t>(dualpad::input::PadAxisId::RightTrigger), 1.0f, 300'000)
        };

        RecordingPollOutputExecutor executor;
        const auto result = runtime.ProcessAssembledFrameForTests(frame, executor);

        Require(!result.RuntimeHealthDegraded(), "active config graph and frame baseline must not degrade");
        Require(
            result.projectionFrame.helperPlan.commands.count == 1,
            "Gameplay Button:Circle must resolve to a helper command through the frame-bound graph");
        Require(
            result.projectionFrame.helperPlan.commands.items[0].actionId == "ModEvent1",
            "Gameplay Button:Circle must resolve to ModEvent1 from active config");
        Require(
            result.projectionFrame.gamepadPlan.analog.lookX == 0.5f,
            "Game.Look must resolve from frame-bound active config graph");
        Require(
            result.projectionFrame.gamepadPlan.analog.rightTrigger == 1.0f,
            "Game.RightTrigger must resolve from frame-bound active config graph");

        frame.firstSeq = 301;
        frame.lastSeq = 301;
        frame.facts.monotonicUs = 301'000;
        frame.facts.controlSamples = {
            AxisSample(static_cast<std::uint32_t>(dualpad::input::PadAxisId::RightStickX), 0.5f, 301'000),
            AxisSample(static_cast<std::uint32_t>(dualpad::input::PadAxisId::RightTrigger), 1.0f, 301'000)
        };

        RecordingPollOutputExecutor unchangedExecutor;
        const auto unchanged = runtime.ProcessAssembledFrameForTests(frame, unchangedExecutor);
        Require(unchanged.output.outputApplySucceeded, "unchanged current-state frame must still apply output");
        Require(
            unchanged.projectionFrame.gamepadPlan.analog.lookX == 0.5f,
            "unchanged stick current-state must remain present in every complete projection frame");
        Require(
            unchanged.projectionFrame.gamepadPlan.analog.rightTrigger == 1.0f,
            "unchanged trigger current-state must remain present in every complete projection frame");

        frame.firstSeq = 302;
        frame.lastSeq = 302;
        frame.facts.monotonicUs = 302'000;
        frame.facts.controlSamples = {
            AxisSample(static_cast<std::uint32_t>(dualpad::input::PadAxisId::RightStickX), 0.0f, 302'000),
            AxisSample(static_cast<std::uint32_t>(dualpad::input::PadAxisId::RightTrigger), 0.0f, 302'000)
        };
        RecordingPollOutputExecutor neutralExecutor;
        const auto neutral = runtime.ProcessAssembledFrameForTests(frame, neutralExecutor);
        Require(
            neutral.projectionFrame.gamepadPlan.analog.lookX == 0.0f,
            "stick current-state must return to neutral without sticky carry");
        Require(
            neutral.projectionFrame.gamepadPlan.analog.rightTrigger == 0.0f,
            "trigger current-state must return to neutral without sticky carry");
    }

    void RunRuntimeFrameEnvelopeUsesActiveConfigGraphForMenuBindingsTests()
    {
        gameplay::DualPadRuntime runtime;
        runtime.ResetForTests();
        config::AtomicConfigReloader::GetSingleton().ResetForTests();
        context::ContextResolver::GetSingleton().ResetForTests();
        actions::CompiledActionGraphPublisher::GetRuntimeOwner().ResetForTests();
        prompt::PromptRuntimeOwner::GetSingleton().ResetForTests();
        ingress::LiveInputFactProducer::GetSingleton().ResetForTests();
        ingress::IngressHub::GetSingleton().ResetForTests();

        auto& compat = presentation::SkyrimCompatibilitySurface::GetSingleton();
        compat.DisableRollback();
        compat.Commit(presentation::PublishedPresentationState{});
        compat.ResetRefreshStateForTests();
        compat.ForceInstallResultForTests(
            presentation::detail::MakeHookInstallResult(
                presentation::HookInstallStatus::Success,
                "test_hook_installed"));
        dualpad::input::detail::ResetUpstreamRouteInstallSnapshotForTests();

        LoadRuntimeConfigForGameplayBindingTests();
        const auto contextSnapshot = PublishGenericMenuContext();
        Require(
            contextSnapshot.legacyInputContext == dualpad::input::InputContext::Menu,
            "generic menu context must mirror legacy Menu");
        Require(
            contextSnapshot.actionSetStack.baseSetId == "MenuBase",
            "generic menu context must use MenuBase action set");

        const auto bundle = config::AtomicConfigReloader::GetSingleton().GetActiveBundleSnapshot();
        Require(bundle != nullptr, "menu binding test needs active config bundle");

        auto& hub = ingress::IngressHub::GetSingleton();
        hub.PushManifestEpochChanged(bundle->manifestEpoch);
        ingress::LiveInputFactProducer::GetSingleton().PublishGamepadSourceEvidence(
            contextSnapshot,
            399'000);
        const auto& bits = dualpad::input::GetPadBits(dualpad::input::GetActivePadProfile());
        (void)hub.PushPadSnapshot(LiveHidSnapshot(
            400,
            0,
            400'000,
            contextSnapshot.legacyInputContext,
            contextSnapshot.legacyContextEpoch,
            contextSnapshot.contextRevision));
        (void)hub.PushPadSnapshot(LiveHidSnapshot(
            401,
            bits.dpadDown,
            401'000,
            contextSnapshot.legacyInputContext,
            contextSnapshot.legacyContextEpoch,
            contextSnapshot.contextRevision));

        ingress::FrameAssembler assembler;
        auto capture = hub.Capture(256);
        const auto frames = assembler.Assemble(
            capture.events,
            capture.latestPadState,
            capture.latestSourceEvidence);
        bool processedStable = false;
        gameplay::DualPadRuntimeResult result{};
        RecordingPollOutputExecutor executor;
        for (const auto& frame : frames) {
            result = runtime.ProcessAssembledFrameForTests(frame, executor);
            processedStable = processedStable || frame.kind == ingress::AssembledFrameKind::Stable;
        }

        Require(processedStable, "live menu HID snapshots must assemble at least one stable frame");
        Require(!result.RuntimeHealthDegraded(), "live menu binding frame must not degrade before projection");
        Require(
            result.projectionFrame.gamepadPlan.sustainedDigital.count == 1,
            "live Menu DpadDown must resolve into one sustained native output");
        Require(
            result.projectionFrame.gamepadPlan.sustainedDigital.items[0].actionId == dualpad::input::actions::MenuScrollDown,
            "live Menu DpadDown must resolve to Menu.ScrollDown");
        Require(
            result.projectionFrame.gamepadPlan.sustainedDigital.items[0].control ==
                dualpad::input::backend::NativeControlCode::MenuScrollDown,
            "Menu.ScrollDown must retain its native menu control");
        Require(
            presentation::SkyrimCompatibilitySurface::GetSingleton().GetCommittedState().owner ==
                presentation::PresentationOwner::Gamepad,
            "live Menu source evidence must publish Gamepad presentation owner with the resolved native output");
        Require(
            presentation::SkyrimCompatibilitySurface::GetSingleton().IsUsingGamepadHook(),
            "live Menu presentation evidence must not replace the unscoped original engine result");
        Require(
            prompt::PromptRuntimeOwner::GetSingleton().ResolveLegacyGlyphToken(
                dualpad::input::actions::MenuScrollDown,
                "Menu") == "360_DPAD_DOWN",
            "live Menu source evidence must publish a Gamepad prompt scope that resolves Menu.ScrollDown glyphs");
        const auto scrollGlyph = prompt::PromptRuntimeOwner::GetSingleton().ResolveLegacyGlyph(
            dualpad::input::actions::MenuScrollDown,
            "Menu");
        Require(scrollGlyph.ok, "live Menu ScrollDown legacy glyph descriptor must resolve");
        Require(
            scrollGlyph.buttonArtToken == "360_DPAD_DOWN",
            "live Menu ScrollDown legacy glyph descriptor must preserve the compiled ButtonArt token");
    }

    void RunRuntimeFrameEnvelopeKeepsJournalTriggerCurrentStateTests()
    {
        gameplay::DualPadRuntime runtime;
        ResetRuntimeSurfaceState(runtime);
        LoadRuntimeConfigForGameplayBindingTests();

        const auto contextSnapshot = context::ContextResolver::GetSingleton().GetPublishedSnapshot();
        Require(
            contextSnapshot.legacyInputContext == dualpad::input::InputContext::JournalMenu,
            "Journal trigger current-state test needs the JournalMenu context");
        const auto bundle = config::AtomicConfigReloader::GetSingleton().GetActiveBundleSnapshot();
        Require(bundle != nullptr, "Journal trigger current-state test needs an active config bundle");

        ingress::AssembledFactFrame frame{};
        frame.kind = ingress::AssembledFrameKind::Stable;
        frame.firstSeq = 410;
        frame.lastSeq = 410;
        frame.boundaryKey = ingress::IngressBoundaryKey{
            static_cast<std::uint32_t>(bundle->manifestEpoch),
            contextSnapshot.contextRevision,
            contextSnapshot.menuStackRevision,
            1
        };
        frame.facts.manifestEpoch = frame.boundaryKey.manifestEpoch;
        frame.facts.contextRevision = frame.boundaryKey.contextRevision;
        frame.facts.menuStackRevision = frame.boundaryKey.menuStackRevision;
        frame.facts.deviceFamilyRevision = frame.boundaryKey.deviceFamilyRevision;
        frame.facts.monotonicUs = 410'000;
        frame.facts.controlSamples = {
            AxisSample(static_cast<std::uint32_t>(dualpad::input::PadAxisId::RightTrigger), 1.0f, 410'000)
        };

        RecordingPollOutputExecutor firstExecutor;
        const auto first = runtime.ProcessAssembledFrameForTests(frame, firstExecutor);
        Require(!first.RuntimeHealthDegraded(), "Journal trigger frame must not degrade before projection");
        Require(
            first.projectionFrame.gamepadPlan.analog.rightTrigger == 1.0f,
            "Journal.TabRight must project the first trigger sample to native current-state");

        frame.firstSeq = 411;
        frame.lastSeq = 411;
        frame.facts.monotonicUs = 411'000;
        frame.facts.controlSamples.front().timestampUs = 411'000;
        RecordingPollOutputExecutor unchangedExecutor;
        const auto unchanged = runtime.ProcessAssembledFrameForTests(frame, unchangedExecutor);
        Require(
            unchanged.projectionFrame.gamepadPlan.analog.rightTrigger == 1.0f,
            "held Journal.TabRight must remain nonzero in every complete projection frame");
    }

    void RunRuntimeFrameEnvelopeResolvesMenuLeftStickAsAxis2DTests()
    {
        gameplay::DualPadRuntime runtime;
        runtime.ResetForTests();
        config::AtomicConfigReloader::GetSingleton().ResetForTests();
        context::ContextResolver::GetSingleton().ResetForTests();
        actions::CompiledActionGraphPublisher::GetRuntimeOwner().ResetForTests();
        prompt::PromptRuntimeOwner::GetSingleton().ResetForTests();
        ingress::LiveInputFactProducer::GetSingleton().ResetForTests();
        ingress::IngressHub::GetSingleton().ResetForTests();

        auto& compat = presentation::SkyrimCompatibilitySurface::GetSingleton();
        compat.DisableRollback();
        compat.Commit(presentation::PublishedPresentationState{});
        compat.ResetRefreshStateForTests();
        compat.SetMenuRefreshTaskSinkForTests([](auto) {
            presentation::SkyrimCompatibilitySurface::GetSingleton().CompleteQueuedRefreshForTests();
            return true;
        });
        compat.ForceInstallResultForTests(
            presentation::detail::MakeHookInstallResult(
                presentation::HookInstallStatus::Success,
                "test_hook_installed"));
        dualpad::input::detail::ResetUpstreamRouteInstallSnapshotForTests();

        LoadRuntimeConfigForGameplayBindingTests();
        const auto contextSnapshot = PublishGenericMenuContext();
        Require(
            contextSnapshot.legacyInputContext == dualpad::input::InputContext::Menu,
            "generic menu context must mirror legacy Menu for stick projection");

        const auto bundle = config::AtomicConfigReloader::GetSingleton().GetActiveBundleSnapshot();
        Require(bundle != nullptr, "menu stick binding test needs active config bundle");

        auto& hub = ingress::IngressHub::GetSingleton();
        hub.PushManifestEpochChanged(bundle->manifestEpoch);
        ingress::LiveInputFactProducer::GetSingleton().PublishGamepadSourceEvidence(
            contextSnapshot,
            449'000);
        (void)hub.PushPadSnapshot(LiveHidSnapshot(
            450,
            0,
            450'000,
            contextSnapshot.legacyInputContext,
            contextSnapshot.legacyContextEpoch,
            contextSnapshot.contextRevision));

        auto stickSnapshot = LiveHidSnapshot(
            451,
            0,
            451'000,
            contextSnapshot.legacyInputContext,
            contextSnapshot.legacyContextEpoch,
            contextSnapshot.contextRevision);
        stickSnapshot.state.leftStick.x = 0.25f;
        stickSnapshot.state.leftStick.y = -0.75f;
        (void)hub.PushPadSnapshot(stickSnapshot);

        ingress::FrameAssembler assembler;
        auto capture = hub.Capture(256);
        const auto frames = assembler.Assemble(
            capture.events,
            capture.latestPadState,
            capture.latestSourceEvidence);
        bool processedStable = false;
        gameplay::DualPadRuntimeResult result{};
        RecordingPollOutputExecutor executor;
        for (const auto& frame : frames) {
            result = runtime.ProcessAssembledFrameForTests(frame, executor);
            processedStable = processedStable || frame.kind == ingress::AssembledFrameKind::Stable;
        }

        Require(processedStable, "menu left stick projection needs a stable frame");
        Require(!result.RuntimeHealthDegraded(), "menu left stick frame must not degrade before projection");
        Require(
            result.projectionFrame.gamepadPlan.analog.moveX == 0.25f,
            "Menu.LeftStick X must project to native moveX from checked-in bindings");
        Require(
            result.projectionFrame.gamepadPlan.analog.moveY == -0.75f,
            "Menu.LeftStick Y must project to native moveY from checked-in bindings");
    }

    void RunRuntimeFrameEnvelopeUsesActiveConfigGraphForMenuCrossCancelTests()
    {
        gameplay::DualPadRuntime runtime;
        runtime.ResetForTests();
        config::AtomicConfigReloader::GetSingleton().ResetForTests();
        context::ContextResolver::GetSingleton().ResetForTests();
        actions::CompiledActionGraphPublisher::GetRuntimeOwner().ResetForTests();
        prompt::PromptRuntimeOwner::GetSingleton().ResetForTests();
        ingress::LiveInputFactProducer::GetSingleton().ResetForTests();
        ingress::IngressHub::GetSingleton().ResetForTests();

        auto& compat = presentation::SkyrimCompatibilitySurface::GetSingleton();
        compat.DisableRollback();
        compat.Commit(presentation::PublishedPresentationState{});
        compat.ResetRefreshStateForTests();
        compat.SetMenuRefreshTaskSinkForTests([](auto) {
            presentation::SkyrimCompatibilitySurface::GetSingleton().CompleteQueuedRefreshForTests();
            return true;
        });
        compat.ForceInstallResultForTests(
            presentation::detail::MakeHookInstallResult(
                presentation::HookInstallStatus::Success,
                "test_hook_installed"));
        dualpad::input::detail::ResetUpstreamRouteInstallSnapshotForTests();

        LoadRuntimeConfigForGameplayBindingTests();
        const auto contextSnapshot = PublishGenericMenuContext();
        Require(
            contextSnapshot.legacyInputContext == dualpad::input::InputContext::Menu,
            "generic menu context must mirror legacy Menu");

        const auto bundle = config::AtomicConfigReloader::GetSingleton().GetActiveBundleSnapshot();
        Require(bundle != nullptr, "menu Cross binding test needs active config bundle");

        auto& hub = ingress::IngressHub::GetSingleton();
        hub.PushManifestEpochChanged(bundle->manifestEpoch);
        ingress::LiveInputFactProducer::GetSingleton().PublishGamepadSourceEvidence(
            contextSnapshot,
            499'000);
        const auto& bits = dualpad::input::GetPadBits(dualpad::input::GetActivePadProfile());
        (void)hub.PushPadSnapshot(LiveHidSnapshot(
            500,
            0,
            500'000,
            contextSnapshot.legacyInputContext,
            contextSnapshot.legacyContextEpoch,
            contextSnapshot.contextRevision));
        (void)hub.PushPadSnapshot(LiveHidSnapshot(
            501,
            bits.cross,
            501'000,
            contextSnapshot.legacyInputContext,
            contextSnapshot.legacyContextEpoch,
            contextSnapshot.contextRevision));
        (void)hub.PushPadSnapshot(LiveHidSnapshot(
            502,
            bits.cross,
            502'000,
            contextSnapshot.legacyInputContext,
            contextSnapshot.legacyContextEpoch,
            contextSnapshot.contextRevision));

        ingress::FrameAssembler assembler;
        auto capture = hub.Capture(256);
        auto frames = assembler.Assemble(
            capture.events,
            capture.latestPadState,
            capture.latestSourceEvidence);
        bool processedStable = false;
        gameplay::DualPadRuntimeResult result{};
        RecordingPollOutputExecutor executor;
        for (const auto& frame : frames) {
            result = runtime.ProcessAssembledFrameForTests(frame, executor);
            processedStable = processedStable || frame.kind == ingress::AssembledFrameKind::Stable;
        }

        Require(processedStable, "MenuCross_EdgeProducesRuntimePlanAction_MenuCancel needs a stable frame");
        Require(!result.RuntimeHealthDegraded(), "Menu Cross live frame must not degrade before projection");
        Require(
            result.projectionFrame.gamepadPlan.transientDigital.count == 1,
            "MenuCross_EdgeProducesRuntimePlanAction_MenuCancel must produce one native transient");
        const auto& command = result.projectionFrame.gamepadPlan.transientDigital.items[0];
        Require(
            command.actionId == dualpad::input::actions::MenuCancel,
            "MenuCross_EdgeProducesRuntimePlanAction_MenuCancel");
        Require(
            command.control == dualpad::input::backend::NativeControlCode::MenuCancel,
            "MenuCancel_HasNativeCommitDescriptor");
        Require(
            command.phase == actions::ActionPhase::Press,
            "Menu Cross edge must produce a Press phase for Menu.Cancel");

        const auto outputMask = dualpad::input::backend::ResolveVirtualPadBitMask(
            dualpad::input::backend::NativeControlCode::MenuCancel,
            bits);
        Require(outputMask != 0, "MenuCancel_WhenRouteActive_ProducesNonZeroAuthoritativePoll output mask");

        (void)hub.PushPadSnapshot(LiveHidSnapshot(
            503,
            bits.cross,
            503'000,
            contextSnapshot.legacyInputContext,
            contextSnapshot.legacyContextEpoch,
            contextSnapshot.contextRevision));
        capture = hub.Capture(256);
        frames = assembler.Assemble(
            capture.events,
            capture.latestPadState,
            capture.latestSourceEvidence);
        bool processedHeldStable = false;
        gameplay::DualPadRuntimeResult heldResult{};
        for (const auto& frame : frames) {
            heldResult = runtime.ProcessAssembledFrameForTests(frame, executor);
            processedHeldStable = processedHeldStable || frame.kind == ingress::AssembledFrameKind::Stable;
        }

        Require(processedHeldStable, "MenuCross_HeldFrameDoesNotRepeatMenuCancel needs a stable frame");
        Require(!heldResult.RuntimeHealthDegraded(), "Menu Cross held frame must not degrade before projection");
        Require(
            heldResult.projectionFrame.gamepadPlan.transientDigital.count == 0,
            "MenuCross_HeldFrameDoesNotRepeatMenuCancel must not emit another native transient");
    }

    void RunRuntimeFrameEnvelopeResolvesFirstStableAfterManifestTransitionTests()
    {
        gameplay::DualPadRuntime runtime;
        runtime.ResetForTests();
        config::AtomicConfigReloader::GetSingleton().ResetForTests();
        context::ContextResolver::GetSingleton().ResetForTests();
        actions::CompiledActionGraphPublisher::GetRuntimeOwner().ResetForTests();
        prompt::PromptRuntimeOwner::GetSingleton().ResetForTests();
        LoadRuntimeConfigForGameplayBindingTests();

        const auto contextSnapshot = PublishGameplayContext();
        const auto bundle = config::AtomicConfigReloader::GetSingleton().GetActiveBundleSnapshot();
        Require(bundle != nullptr, "manifest transition test needs active config bundle");

        ingress::AssembledFactFrame transition{};
        transition.kind = ingress::AssembledFrameKind::Transition;
        transition.boundaryKey = ingress::IngressBoundaryKey{
            static_cast<std::uint32_t>(bundle->manifestEpoch),
            0,
            0,
            0
        };
        transition.facts.manifestEpoch = transition.boundaryKey.manifestEpoch;
        transition.transition = ingress::TransitionFrameMeta{
            .from = ingress::IngressBoundaryKey{},
            .to = transition.boundaryKey,
            .reason = ingress::TransitionReason::ManifestEpochChanged,
            .requestHardResync = true,
            .flushPendingPulseEdges = true
        };
        RecordingPollOutputExecutor transitionExecutor;
        (void)runtime.ProcessAssembledFrameForTests(transition, transitionExecutor);

        ingress::AssembledFactFrame stable{};
        stable.kind = ingress::AssembledFrameKind::Stable;
        stable.firstSeq = 301;
        stable.lastSeq = 302;
        stable.boundaryKey = ingress::IngressBoundaryKey{
            static_cast<std::uint32_t>(bundle->manifestEpoch),
            contextSnapshot.contextRevision,
            contextSnapshot.menuStackRevision,
            0
        };
        stable.facts.manifestEpoch = stable.boundaryKey.manifestEpoch;
        stable.facts.contextRevision = stable.boundaryKey.contextRevision;
        stable.facts.menuStackRevision = stable.boundaryKey.menuStackRevision;
        stable.facts.monotonicUs = 301'000;
        stable.facts.controlSamples = {
            Sample(actions::ControlPath{ .kind = actions::ControlPathKind::DigitalButton, .code = 4 }, true, true, false, 301'000, 301'000),
            AxisSample(static_cast<std::uint32_t>(dualpad::input::PadAxisId::RightStickX), 0.5f, 301'000),
            AxisSample(static_cast<std::uint32_t>(dualpad::input::PadAxisId::RightTrigger), 1.0f, 301'000)
        };

        RecordingPollOutputExecutor stableExecutor;
        const auto result = runtime.ProcessAssembledFrameForTests(stable, stableExecutor);
        Require(
            result.projectionFrame.helperPlan.commands.count == 1,
            "first stable frame after manifest transition must still resolve helper command");
        Require(
            result.projectionFrame.gamepadPlan.analog.lookX == 0.5f,
            "first stable frame after manifest transition must still resolve analog values");
    }

    void RunRuntimeFrameEnvelopeResolvesReplayBoundaryStackTests()
    {
        gameplay::DualPadRuntime runtime;
        runtime.ResetForTests();
        config::AtomicConfigReloader::GetSingleton().ResetForTests();
        context::ContextResolver::GetSingleton().ResetForTests();
        actions::CompiledActionGraphPublisher::GetRuntimeOwner().ResetForTests();
        prompt::PromptRuntimeOwner::GetSingleton().ResetForTests();
        LoadRuntimeConfigForGameplayBindingTests();

        auto contextSnapshot = PublishGameplayContext();
        contextSnapshot.contextRevision = 2;
        contextSnapshot.legacyContextEpoch = 2;
        context::ContextResolver::GetSingleton().PublishSnapshotForReplayTests(contextSnapshot);

        const auto bundle = config::AtomicConfigReloader::GetSingleton().GetActiveBundleSnapshot();
        Require(bundle != nullptr, "replay boundary stack test needs active config bundle");

        ingress::FrameAssembler assembler;
        std::vector<ingress::IngressEvent> events;

        ingress::IngressEvent manifest{};
        manifest.kind = ingress::IngressKind::ManifestEpochChanged;
        manifest.source = ingress::IngressSource::ManifestPublisher;
        manifest.seq = 1;
        manifest.monotonicUs = 9000;
        manifest.manifest.manifestEpoch = static_cast<std::uint32_t>(bundle->manifestEpoch);
        events.push_back(manifest);

        ingress::IngressEvent ui{};
        ui.kind = ingress::IngressKind::UiSnapshot;
        ui.source = ingress::IngressSource::LegacyDispatcher;
        ui.seq = 2;
        ui.monotonicUs = 9000;
        ui.ui.contextRevision = contextSnapshot.contextRevision;
        ui.ui.menuStackRevision = contextSnapshot.menuStackRevision;
        events.push_back(ui);

        ingress::IngressEvent pad{};
        pad.kind = ingress::IngressKind::PadSnapshot;
        pad.source = ingress::IngressSource::LegacyDispatcher;
        pad.seq = 3;
        pad.monotonicUs = 9000;
        pad.pad.sequence = 9;
        pad.pad.firstSequence = 9;
        pad.pad.samples = {
            Sample(actions::ControlPath{ .kind = actions::ControlPathKind::DigitalButton, .code = 4 }, true, true, false, 9000, 9000),
            AxisSample(static_cast<std::uint32_t>(dualpad::input::PadAxisId::RightStickX), 0.5f, 9000),
            AxisSample(static_cast<std::uint32_t>(dualpad::input::PadAxisId::RightTrigger), 1.0f, 9000)
        };
        events.push_back(std::move(pad));

        const auto frames = assembler.Assemble(events);
        Require(frames.size() == 3, "replay boundary stack must assemble manifest transition, ui transition, and stable frame");

        RecordingPollOutputExecutor transitionExecutor;
        (void)runtime.ProcessAssembledFrameForTests(frames[0], transitionExecutor);
        RecordingPollOutputExecutor uiExecutor;
        (void)runtime.ProcessAssembledFrameForTests(frames[1], uiExecutor);
        RecordingPollOutputExecutor stableExecutor;
        const auto result = runtime.ProcessAssembledFrameForTests(frames[2], stableExecutor);

        Require(!result.RuntimeHealthDegraded(), "replay boundary stable frame must not degrade runtime health");
        Require(
            result.projectionFrame.helperPlan.commands.count == 1,
            "replay boundary stable frame must resolve helper command");
        Require(
            result.projectionFrame.gamepadPlan.analog.lookX == 0.5f,
            "replay boundary stable frame must resolve right stick value");
        Require(
            result.projectionFrame.gamepadPlan.analog.rightTrigger == 1.0f,
            "replay boundary stable frame must resolve right trigger value");
    }

    void RunFrameAssemblerMultiProducerTimestampOrderingTests()
    {
        ingress::FrameAssembler assembler;
        std::vector<ingress::IngressEvent> events;

        ingress::IngressEvent first{};
        first.kind = ingress::IngressKind::PadSnapshot;
        first.source = ingress::IngressSource::LegacyDispatcher;
        first.seq = 1;
        first.monotonicUs = 2'000;
        first.pad.samples = {
            AxisSample(
                static_cast<std::uint32_t>(dualpad::input::PadAxisId::LeftStickX),
                0.25f,
                2'000)
        };
        events.push_back(first);

        ingress::IngressEvent second{};
        second.kind = ingress::IngressKind::SourceEvidence;
        second.source = ingress::IngressSource::DeviceFamilyPublisher;
        second.seq = 2;
        second.monotonicUs = 1'999;
        second.sourceEvidence.collectedTick = 1'999;
        events.push_back(second);

        const auto frames = assembler.Assemble(events);
        Require(frames.size() == 1, "serialized multi-producer timestamps must not manufacture a transition frame");
        Require(
            frames.front().kind == ingress::AssembledFrameKind::Stable,
            "strictly increasing ingress sequence remains the ordering authority when producer timestamps overlap");
        Require(
            !frames.front().facts.health.sequenceGap,
            "a timestamp regression without an ingress sequence gap must not report sequence loss");
        Require(
            frames.front().facts.monotonicUs == 2'000,
            "frame evaluation time must retain the maximum observed producer timestamp");
    }

    void RunRuntimeOwnerGuardTests()
    {
        runtime::RuntimeOwnerGuard guard;
        {
            auto first = guard.TryEnter(1);
            Require(first.Accepted(), "first owner tick must bind and enter");
            Require(first.Generation() == 1, "first owner tick must publish generation 1");

            auto reentrant = guard.TryEnter(1);
            Require(!reentrant.Accepted(), "same-tick reentry must not gain mutation authority");
            Require(
                reentrant.Failure() == runtime::RuntimeOwnerFailure::ReentrantTick,
                "same-tick reentry must expose ReentrantTick");
        }
        auto snapshot = guard.GetSnapshot();
        Require(snapshot.degraded, "same-tick reentry must permanently degrade the owner guard");
        Require(snapshot.rejectedTicks == 1, "same-tick reentry must increment rejected tick count");

        guard.ResetForTests();
        {
            auto first = guard.TryEnter(10);
            Require(first.Accepted(), "reset owner guard must accept a new first tick");
        }
        {
            auto repeated = guard.TryEnter(10);
            Require(!repeated.Accepted(), "repeated completed frame token must not mutate twice");
            Require(
                repeated.Failure() == runtime::RuntimeOwnerFailure::RepeatedFrameToken,
                "repeated completed frame token must expose RepeatedFrameToken");
        }

        guard.ResetForTests();
        {
            auto first = guard.TryEnter(20);
            Require(first.Accepted(), "thread-handoff fixture must bind on the main test thread");
        }
        bool handoffAccepted = false;
        std::uint64_t handoffGeneration = 0;
        std::thread handoff([&]() {
            auto otherThread = guard.TryEnter(21);
            handoffAccepted = otherThread.Accepted();
            handoffGeneration = otherThread.Generation();
        });
        handoff.join();
        snapshot = guard.GetSnapshot();
        Require(handoffAccepted, "a later monotonic tick may rebind after the previous owner lease is released");
        Require(handoffGeneration == 2, "serialized thread handoff must advance exactly one runtime generation");
        Require(!snapshot.degraded && snapshot.generation == 2, "serialized thread handoff must preserve healthy single-writer state");
        Require(snapshot.ownerThreadHandoffs == 1, "serialized thread handoff must remain explicitly observable");

        guard.ResetForTests();
        runtime::RuntimeOwnerFailure concurrentFailure = runtime::RuntimeOwnerFailure::None;
        {
            auto first = guard.TryEnter(25);
            Require(first.Accepted(), "concurrent-writer fixture must hold the first owner lease");
            std::thread concurrent([&]() {
                auto otherThread = guard.TryEnter(26);
                concurrentFailure = otherThread.Failure();
            });
            concurrent.join();
        }
        snapshot = guard.GetSnapshot();
        Require(
            concurrentFailure == runtime::RuntimeOwnerFailure::ThreadDrift,
            "a different thread must be rejected while an owner lease is active");
        Require(snapshot.degraded && snapshot.generation == 1, "concurrent writer attempt must not advance runtime generation");

        guard.ResetForTests();
        {
            auto first = guard.TryEnter(30);
            Require(first.Accepted() && first.Generation() == 1, "first healthy tick must use generation 1");
        }
        {
            auto second = guard.TryEnter(31);
            Require(second.Accepted() && second.Generation() == 2, "same owner must advance one generation per frame token");
        }
        snapshot = guard.GetSnapshot();
        Require(!snapshot.degraded && snapshot.generation == 2, "healthy owner cadence must remain non-degraded");

        guard.Stop();
        snapshot = guard.GetSnapshot();
        Require(snapshot.degraded && snapshot.failure == runtime::RuntimeOwnerFailure::OwnerStopped, "owner stop must publish fail-closed lifecycle state");
        auto afterStop = guard.TryEnter(32);
        Require(!afterStop.Accepted(), "stopped owner must never silently bind a new writer");
    }

    void RunPollOutputPublicationTests()
    {
        auto& publication = gameplay::PollOutputPublication::GetSingleton();
        auto& ownerGuard = runtime::RuntimeOwnerGuard::GetSingleton();
        ownerGuard.ResetForTests();
        publication.ResetForTests();

        auto frame = publication.AcquireForPoll();
        Require(frame != nullptr, "Poll publication must always return a frame before the first owner tick");
        Require(frame->publicationGeneration == 0, "initial neutral Poll frame must use generation 0");
        Require(frame->neutral, "initial Poll frame must be neutral");
        Require(
            frame->routeHealth == gameplay::PollOutputRouteHealth::Initializing,
            "initial neutral Poll frame must expose Initializing route health");

        auto makeFrame = [](std::uint64_t generation) {
            const auto revision = static_cast<std::uint32_t>(generation);
            return gameplay::PollOutputFrame{
                .runtimeGeneration = generation,
                .manifestEpoch = generation + 1'000'000,
                .contextRevision = revision,
                .presentationEpoch = revision ^ 0x55AA55AAu,
                .actionEpoch = revision * 3u,
                .contextEpoch = revision * 5u,
                .menuStackRevision = revision * 9u,
                .sourceTimestampUs = generation * 11u,
                .inputStateEpoch = generation * 13u,
                .gamepadSessionId = generation * 17u,
                .controlMapRevision = revision * 19u,
                .orderedCutoffSeq = generation * 23u,
                .eventBatchToken = generation * 29u,
                .buttons = static_cast<std::uint16_t>(revision),
                .pressedMask = revision ^ 0x0F0F0F0Fu,
                .releasedMask = revision ^ 0xF0F0F0F0u,
                .lx = static_cast<std::int16_t>(revision % 32767u),
                .ly = -static_cast<std::int16_t>(revision % 32767u),
                .rx = static_cast<std::int16_t>(revision % 16384u),
                .ry = -static_cast<std::int16_t>(revision % 16384u),
                .lt = static_cast<std::uint8_t>(revision % 255u),
                .rt = static_cast<std::uint8_t>((revision + 1u) % 255u),
                .pulseToken = generation * 7u,
                .pulseDownGeneration = generation,
                .pulseUpGeneration = generation + 1,
                .routeHealth = gameplay::PollOutputRouteHealth::Ready,
                .neutral = false
            };
        };

        publication.PublishForTests(makeFrame(1));
        frame = publication.AcquireForPoll();
        Require(frame->publicationGeneration == 1 && frame->runtimeGeneration == 1, "first owner publication must be complete generation 1");
        Require(frame->packetNumber == 1, "first changed gamepad payload must allocate packet 1 on the owner");

        const auto heldOldFrame = frame;
        publication.PublishForTests(makeFrame(2));
        const auto newer = publication.AcquireForPoll();
        Require(newer->runtimeGeneration == 2, "newer owner frame must publish");
        Require(heldOldFrame->runtimeGeneration == 1 && heldOldFrame->buttons == 1, "slow reader must retain an immutable old frame lifetime");

        auto runReaders = [&](std::size_t readerCount) {
            publication.ResetForTests();
            ownerGuard.ResetForTests();
            publication.PublishForTests(makeFrame(1));
            std::atomic_bool done{ false };
            std::atomic_bool torn{ false };
            std::vector<std::thread> readers;
            readers.reserve(readerCount);
            for (std::size_t index = 0; index < readerCount; ++index) {
                readers.emplace_back([&]() {
                    while (!done.load(std::memory_order_acquire)) {
                        const auto acquired = publication.AcquireForPoll();
                        if (acquired->routeHealth != gameplay::PollOutputRouteHealth::Ready) {
                            continue;
                        }
                        const auto generation = acquired->runtimeGeneration;
                        const auto revision = static_cast<std::uint32_t>(generation);
                        if (acquired->publicationGeneration != generation ||
                            acquired->manifestEpoch != generation + 1'000'000 ||
                            acquired->contextRevision != revision ||
                            acquired->presentationEpoch != (revision ^ 0x55AA55AAu) ||
                            acquired->actionEpoch != revision * 3u ||
                            acquired->inputStateEpoch != generation * 13u ||
                            acquired->gamepadSessionId != generation * 17u ||
                            acquired->controlMapRevision != revision * 19u ||
                            acquired->orderedCutoffSeq != generation * 23u ||
                            acquired->eventBatchToken != generation * 29u ||
                            acquired->contextEpoch != revision * 5u ||
                            acquired->menuStackRevision != revision * 9u ||
                            acquired->sourceTimestampUs != generation * 11u ||
                            acquired->buttons != static_cast<std::uint16_t>(revision) ||
                            acquired->pulseToken != generation * 7u ||
                            acquired->pulseDownGeneration != generation ||
                            acquired->pulseUpGeneration != generation + 1) {
                            torn.store(true, std::memory_order_release);
                            break;
                        }
                    }
                });
            }
            for (std::uint64_t generation = 2; generation <= 100'000; ++generation) {
                publication.PublishForTests(makeFrame(generation));
            }
            done.store(true, std::memory_order_release);
            for (auto& reader : readers) {
                reader.join();
            }
            Require(!torn.load(std::memory_order_acquire), "multi-reader Poll publication must never tear generations");
            Require(publication.AcquireForPoll()->runtimeGeneration == 100'000, "multi-reader stress must retain final owner generation");
        };
        runReaders(2);
        runReaders(4);
        runReaders(8);

        publication.ResetForTests();
        auto contextOnly = makeFrame(1);
        publication.PublishForTests(contextOnly);
        const auto stablePacket = publication.AcquireForPoll()->packetNumber;
        contextOnly.contextRevision += 1;
        contextOnly.contextEpoch += 1;
        publication.PublishForTests(contextOnly);
        Require(
            publication.AcquireForPoll()->packetNumber == stablePacket,
            "context-only publication must not change the XInput packet number");
        contextOnly.lx += 1;
        publication.PublishForTests(contextOnly);
        Require(
            publication.AcquireForPoll()->packetNumber == stablePacket + 1,
            "serialized gamepad payload change must advance packet number on the owner");

        struct XInputGamepadView
        {
            std::uint16_t buttons;
            std::uint8_t leftTrigger;
            std::uint8_t rightTrigger;
            std::int16_t thumbLX;
            std::int16_t thumbLY;
            std::int16_t thumbRX;
            std::int16_t thumbRY;
        };
        struct XInputStateView
        {
            std::uint32_t packetNumber;
            XInputGamepadView gamepad;
        };
        const auto serializedFrame = publication.AcquireForPoll();
        XInputStateView serialized{};
        Require(
            dualpad::input::FillSyntheticXInputState(&serialized, *serializedFrame) == 0,
            "Poll serializer must accept a complete immutable frame");
        Require(
            serialized.packetNumber == serializedFrame->packetNumber &&
                serialized.gamepad.buttons == serializedFrame->buttons &&
                serialized.gamepad.thumbLX == serializedFrame->lx &&
                serialized.gamepad.thumbLY == serializedFrame->ly &&
                serialized.gamepad.thumbRX == serializedFrame->rx &&
                serialized.gamepad.thumbRY == serializedFrame->ry &&
                serialized.gamepad.leftTrigger == serializedFrame->lt &&
                serialized.gamepad.rightTrigger == serializedFrame->rt,
            "Poll serializer must copy one frame without consulting mutable runtime state");

        auto& receipts = dualpad::input::PollMaterializationReceiptStore::GetSingleton();
        receipts.ResetForTests();
        const auto receipt = receipts.PublishAfterSuccessfulSerialize(*serializedFrame, 1234);
        Require(receipt && receipt->identity.publicationGeneration == serializedFrame->publicationGeneration &&
                receipt->identity.packetNumber == serializedFrame->packetNumber &&
                receipt->identity.inputStateEpoch == serializedFrame->inputStateEpoch &&
                receipt->identity.gamepadSessionId == serializedFrame->gamepadSessionId,
            "verified serializer must freeze complete Poll identity into a receipt");
        Require(receipts.ConsumeExact(receipt->hookCallSequence, 1234).Succeeded(),
            "materialization receipt must be exact-consume compatible");

        publication.SetUnavailableForTests(true);
        frame = publication.AcquireForPoll();
        Require(frame->neutral && frame->routeHealth == gameplay::PollOutputRouteHealth::PublicationUnavailable, "unavailable publication must return explicit neutral frame");
        publication.SetUnavailableForTests(false);

        ownerGuard.ResetForTests();
        (void)ownerGuard.TryEnter(0);
        frame = publication.AcquireForPoll();
        Require(frame->neutral && frame->routeHealth == gameplay::PollOutputRouteHealth::OwnerDegraded, "degraded owner must force neutral Poll frame");

        ownerGuard.ResetForTests();
        ownerGuard.Stop();
        frame = publication.AcquireForPoll();
        Require(frame->neutral && frame->routeHealth == gameplay::PollOutputRouteHealth::Shutdown, "stopped owner must force shutdown-neutral Poll frame");

        ownerGuard.ResetForTests();
        publication.ResetForTests();
    }

    class RecordingCommitEmitter final : public input_backend::IPollCommitEmitter
    {
    public:
        input_backend::EmitResult Emit(const input_backend::EmitRequest& request) override
        {
            requests.push_back(request);
            return { .submitted = true };
        }

        std::vector<input_backend::EmitRequest> requests;
    };

    input_backend::PollCommitRequest GenerationPulseRequest(
        dualpad::input::InputContext context,
        std::uint32_t epoch,
        std::string_view actionId = input_actions::MenuConfirm)
    {
        return {
            .actionId = std::string(actionId),
            .context = context,
            .outputCode = input_backend::NativeControlCode::MenuConfirm,
            .mode = input_backend::PollCommitMode::Pulse,
            .kind = input_backend::PollCommitRequestKind::Pulse,
            .epoch = epoch
        };
    }

    void RunProductionKbmGameplayPolicyBuilderTests()
    {
        ingress::FactFrame facts{};
        facts.monotonicUs = 900'000;
        facts.kbmGameplay = ingress::LatestKbmGameplayFacts{};
        auto& kbm = *facts.kbmGameplay;
        kbm.virtualGameplayEligible = true;
        kbm.current.complete = true;
        kbm.current.keyboardMoveHeldMask = 1u;
        kbm.current.keyboardCombatHeldMask = 2u;
        kbm.current.mouseCombatHeldMask = 4u;
        kbm.current.keyboardTransientHeldMask = 8u;
        kbm.current.mouseTransientHeldMask = 16u;
        kbm.current.keyboardSustainedHeldMask = 32u;
        kbm.current.mouseSustainedHeldMask = 64u;
        kbm.lastPhysicalMouseMoveOwnerUs = 899'000;
        kbm.physicalMouseMoveThisFrame = true;

        const auto policy = gameplay::BuildGameplayPolicyFromFacts(
            facts,
            true,
            gameplay::GameplayRecoveryInput{});
        Require(policy.mouseLookActive, "production policy must consume physical mouse Look facts");
        Require(policy.keyboardMoveActive, "production policy must consume keyboard Move facts");
        Require(policy.keyboardMouseCombatActive, "production policy must consume keyboard/mouse Combat facts");
        Require(policy.keyboardMouseDigitalActive, "production policy must consume keyboard/mouse transient facts");
        Require(policy.keyboardPhysicalSustainedActive, "production policy must expose keyboard sustained shadow facts");
        Require(policy.mousePhysicalSustainedActive, "production policy must expose mouse sustained shadow facts");
        Require(policy.lastPhysicalMouseMoveOwnerUs == 899'000, "production policy must preserve the owner-clock mouse timestamp");
    }

    void RunGenerationBasedPulseTests()
    {
        input_backend::PollCommitCoordinator coordinator;
        RecordingCommitEmitter emitter;

        coordinator.BeginFrame(dualpad::input::InputContext::Menu, 42, 1'000, 10);
        Require(coordinator.QueueRequest(GenerationPulseRequest(dualpad::input::InputContext::Menu, 42)), "pulse request must queue");
        coordinator.Tick(1'000, true);
        coordinator.Flush(emitter, 1'000);
        Require(
            emitter.requests.size() == 1 &&
                emitter.requests[0].edge == input_backend::EmitEdge::Down &&
                emitter.requests[0].runtimeGeneration == 10,
            "pulse down must publish exactly once in its owner generation");

        coordinator.BeginFrame(dualpad::input::InputContext::Menu, 42, 2'000, 10);
        coordinator.Tick(2'000, true);
        coordinator.Flush(emitter, 2'000);
        Require(emitter.requests.size() == 1, "repeating work or Poll reads in one generation must not release pulse");

        coordinator.BeginFrame(dualpad::input::InputContext::Menu, 42, 3'000, 11);
        coordinator.Tick(3'000, true);
        coordinator.Flush(emitter, 3'000);
        Require(
            emitter.requests.size() == 2 &&
                emitter.requests[1].edge == input_backend::EmitEdge::Up &&
                emitter.requests[1].runtimeGeneration == 11,
            "pulse up must publish exactly once in a later owner generation");
        const auto completed = coordinator.LastPulseRecord();
        Require(
            completed.downGeneration == 10 && completed.upGeneration == 11 && !completed.cancelled,
            "completed pulse record must bind one token to down/up owner generations");

        coordinator.Reset();
        emitter.requests.clear();
        coordinator.BeginFrame(dualpad::input::InputContext::Menu, 70, 4'000, 20);
        Require(coordinator.QueueRequest(GenerationPulseRequest(dualpad::input::InputContext::Menu, 70)), "context test pulse must queue");
        coordinator.Tick(4'000, true);
        coordinator.Flush(emitter, 4'000);
        coordinator.BeginFrame(dualpad::input::InputContext::FavoritesMenu, 71, 5'000, 21);
        coordinator.Tick(5'000, true);
        coordinator.Flush(emitter, 5'000);
        coordinator.BeginFrame(dualpad::input::InputContext::FavoritesMenu, 71, 5'100, 21);
        coordinator.Tick(5'100, true);
        coordinator.Flush(emitter, 5'100);
        Require(
            emitter.requests.size() == 2 && emitter.requests.back().edge == input_backend::EmitEdge::Up,
            "context/epoch transition must release a visible pulse once and discard stale pending work");

        for (const auto reason : {
                 input_backend::PulseBoundaryReason::Overflow,
                 input_backend::PulseBoundaryReason::DeviceDisconnected,
                 input_backend::PulseBoundaryReason::RouteUnavailable }) {
            coordinator.Reset();
            emitter.requests.clear();
            coordinator.BeginFrame(dualpad::input::InputContext::Gameplay, 90, 6'000, 30);
            Require(coordinator.QueueRequest(GenerationPulseRequest(dualpad::input::InputContext::Gameplay, 90)), "boundary pulse must queue");
            coordinator.Tick(6'000, true);
            coordinator.Flush(emitter, 6'000);
            coordinator.CancelForBoundary(reason, 31);
            const auto cancelled = coordinator.LastPulseRecord();
            Require(
                cancelled.cancelled && cancelled.downGeneration == 30 && cancelled.upGeneration == 31 &&
                    cancelled.boundaryReason == reason,
                "overflow/device boundary must cancel visible pulse at an explicit owner generation");
        }

        coordinator.Reset();
        emitter.requests.clear();
        coordinator.BeginFrame(dualpad::input::InputContext::Gameplay, 100, 7'000, 40);
        const auto rapid = GenerationPulseRequest(
            dualpad::input::InputContext::Gameplay,
            100,
            input_actions::Favorites);
        Require(coordinator.QueueRequest(rapid) && coordinator.QueueRequest(rapid), "rapid double pulse must coalesce one pending request");
        coordinator.Tick(7'000, true);
        coordinator.Flush(emitter, 7'000);
        for (std::uint64_t generation = 41; generation <= 43; ++generation) {
            coordinator.BeginFrame(dualpad::input::InputContext::Gameplay, 100, 7'000 + generation, generation);
            coordinator.Tick(7'000 + generation, true);
            coordinator.Flush(emitter, 7'000 + generation);
        }
        Require(
            std::count_if(emitter.requests.begin(), emitter.requests.end(), [](const auto& request) {
                return request.edge == input_backend::EmitEdge::Down;
            }) == 2 &&
                std::count_if(emitter.requests.begin(), emitter.requests.end(), [](const auto& request) {
                    return request.edge == input_backend::EmitEdge::Up;
                }) == 2,
            "rapid double pulse must produce two non-overlapping down/up generation pairs");
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
        RunRuntimePublishedSurfacePipelineTests();
        RunPromptStatePublishedBeforeRefreshCallbackTests();
        RunRuntimeLiveStyleGamepadPublishTests();
        RunRuntimeLiveKeyboardMouseEvidenceProducerTests();
        RunRuntimeGraphSkewHealthTests();
        RunRuntimeDiagnosticsProjectionTests();
        RunRuntimeDegradedFramePromptPublishContractTests();
        RunRuntimeSkyrimCompatSurfaceFailurePresentationOnlyTests();
        RunRuntimeUpstreamRouteInstallFailureFailClosedTests();
        RunRuntimeDisabledUpstreamRouteDoesNotFailClosedTests();
        RunRuntimeMenuObserverDegradedHealthTests();
        RunRuntimePresentationUsesFrameBoundContextTests();
        RunRuntimeTransitionRecoveryContractTests();
        RunRuntimeSoftSequenceGapDoesNotClearAuthoritativePollTests();
        RunManifestEpochChangeHardResetsOnceTests();
        RunContextEpochChangeHardResetsOnceTests();
        RunRuntimeFrameEnvelopeUsesActiveConfigGraphForGameplayBindingsTests();
        RunRuntimeFrameEnvelopeUsesActiveConfigGraphForMenuBindingsTests();
        RunRuntimeFrameEnvelopeKeepsJournalTriggerCurrentStateTests();
        RunRuntimeFrameEnvelopeResolvesMenuLeftStickAsAxis2DTests();
        RunRuntimeFrameEnvelopeUsesActiveConfigGraphForMenuCrossCancelTests();
        RunRuntimeFrameEnvelopeResolvesFirstStableAfterManifestTransitionTests();
        RunRuntimeFrameEnvelopeResolvesReplayBoundaryStackTests();
        RunFrameAssemblerMultiProducerTimestampOrderingTests();
        RunRuntimeOwnerGuardTests();
        RunPollOutputPublicationTests();
        RunProductionKbmGameplayPolicyBuilderTests();
        RunGenerationBasedPulseTests();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
