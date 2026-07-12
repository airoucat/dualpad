#include "pch.h"

#include "input_v2/gameplay/DualPadRuntime.h"

#include "input_v2/telemetry/MixedInputEvidence.h"

#include "input_v2/actions/CompiledActionGraphPublisher.h"
#include "input_v2/config/AtomicConfigReloader.h"
#include "input_v2/context/ContextResolver.h"
#include "input_v2/ingress/IngressRecovery.h"
#include "input_v2/presentation/CursorHandoffAckMailbox.h"
#include "input_v2/presentation/SkyrimCompatibilitySurface.h"
#include "input_v2/prompt/PromptRuntimeOwner.h"

#include "input/RuntimeConfig.h"
#include "input/injection/KbmIngressDiagnostics.h"
#include "input/injection/RouteHealthContract.h"

#include <SKSE/Logger.h>

#include <algorithm>
#include <optional>

namespace logger = SKSE::log;

namespace dualpad::input_v2::gameplay
{
    namespace
    {
        input::KbmIngressDiagnosticSampler g_kbmRuntimeSampler{ 5'000 };

        const char* ToString(actions::ActionPhase phase)
        {
            switch (phase) {
            case actions::ActionPhase::Press:
                return "Press";
            case actions::ActionPhase::Hold:
                return "Hold";
            case actions::ActionPhase::Repeat:
                return "Repeat";
            case actions::ActionPhase::Release:
                return "Release";
            case actions::ActionPhase::Pulse:
                return "Pulse";
            case actions::ActionPhase::Value:
                return "Value";
            default:
                return "Unknown";
            }
        }

        const char* ToString(DigitalGateMode mode)
        {
            switch (mode) {
            case DigitalGateMode::Open:
                return "Open";
            case DigitalGateMode::SuppressNewTransient:
                return "SuppressNewTransient";
            case DigitalGateMode::CancelAndSuppressNewTransient:
                return "CancelAndSuppressNewTransient";
            default:
                return "Unknown";
            }
        }

        const char* ToString(RecoveryMode mode)
        {
            switch (mode) {
            case RecoveryMode::None:
                return "None";
            case RecoveryMode::SoftResyncOutputs:
                return "SoftResyncOutputs";
            case RecoveryMode::HardResetOutputs:
                return "HardResetOutputs";
            default:
                return "Unknown";
            }
        }

        bool ShouldClearProjectionStickyOwners(const GameplayRecoveryInput& recovery)
        {
            return (recovery.hardResetRequested || recovery.explicitResetRequested) &&
                recovery.resetScope == RecoveryResetScope::Global;
        }

        bool HasRecoveryRequest(const GameplayRecoveryInput& recovery)
        {
            return recovery.softResyncRequested ||
                recovery.hardResetRequested ||
                recovery.sequenceGapObserved ||
                recovery.explicitResetRequested;
        }

        void MergeRecovery(GameplayRecoveryInput& target, const GameplayRecoveryInput& source)
        {
            const bool targetHadRequest = HasRecoveryRequest(target);
            target.softResyncRequested = target.softResyncRequested || source.softResyncRequested;
            target.hardResetRequested = target.hardResetRequested || source.hardResetRequested;
            target.sequenceGapObserved = target.sequenceGapObserved || source.sequenceGapObserved;
            target.explicitResetRequested = target.explicitResetRequested || source.explicitResetRequested;
            if (!targetHadRequest) {
                target.resetScope = source.resetScope;
            } else if (source.resetScope == RecoveryResetScope::Global) {
                target.resetScope = RecoveryResetScope::Global;
            }
        }

        void FailClosedAffectedChannels(
            GameplayProjectionFrame& projection,
            CurrentCycleChannelMaskType affected,
            const CurrentCycleSensitiveState& previousSensitive)
        {
            if ((affected & CurrentCycleChannelMask(CurrentCycleChannel::Look)) != 0) {
                projection.lookOwner = ChannelOwner::None;
                projection.gatePlan.lookGate = AnalogGateMode::ZeroedByKeyboardMouse;
                projection.gamepadPlan.analog.lookX = 0.0f;
                projection.gamepadPlan.analog.lookY = 0.0f;
            }
            if ((affected & CurrentCycleChannelMask(CurrentCycleChannel::Move)) != 0) {
                projection.moveOwner = ChannelOwner::None;
                projection.gatePlan.moveGate = AnalogGateMode::ZeroedByKeyboardMouse;
                projection.gamepadPlan.analog.moveX = 0.0f;
                projection.gamepadPlan.analog.moveY = 0.0f;
            }
            if ((affected & CurrentCycleChannelMask(CurrentCycleChannel::Combat)) != 0) {
                projection.combatOwner = ChannelOwner::None;
                projection.gatePlan.leftTriggerGate = AnalogGateMode::ZeroedByKeyboardMouse;
                projection.gatePlan.rightTriggerGate = AnalogGateMode::ZeroedByKeyboardMouse;
                projection.gamepadPlan.analog.leftTrigger = 0.0f;
                projection.gamepadPlan.analog.rightTrigger = 0.0f;
            }
            if ((affected & CurrentCycleChannelMask(CurrentCycleChannel::TransientDigital)) != 0) {
                projection.digitalOwner = ChannelOwner::None;
                projection.gatePlan.transientDigitalGate = DigitalGateMode::CancelAndSuppressNewTransient;
                projection.gamepadPlan.transientDigital.count = 0;
            }
            if ((affected & CurrentCycleChannelMask(CurrentCycleChannel::SustainedDigital)) != 0) {
                projection.sprintDecision = SustainedContributorDecision{
                    .next = previousSensitive.sprint,
                    .aggregateHeld = previousSensitive.sprint.activeSourceMask != 0,
                    .virtualBridgeDesired = previousSensitive.sprint.virtualMaterialized
                };
                for (std::size_t index = 0;
                     index < projection.gamepadPlan.sustainedDigital.count;
                     ++index) {
                    auto& command = projection.gamepadPlan.sustainedDigital.items[index];
                    if (command.control == dualpad::input::backend::NativeControlCode::Sprint) {
                        command.activeSourceMask = previousSensitive.sprint.activeSourceMask;
                        command.virtualBridgeDesired = previousSensitive.sprint.virtualMaterialized;
                        command.joiningPressSuppressionMask = 0;
                        command.nonFinalReleaseSuppressionMask = 0;
                        command.releaseToken = 0;
                    }
                }
            }
        }

        RuntimeHealthReasonMask RuntimeHealthReasonsFromIngress(const ingress::AssembledFactFrame& frame)
        {
            auto reasons = RuntimeHealthMask(RuntimeHealthReason::None);
            if (frame.facts.health.queueOverflow) {
                reasons = AddRuntimeHealthReason(reasons, RuntimeHealthReason::QueueOverflow);
            }
            if (frame.facts.health.sequenceGap) {
                reasons = AddRuntimeHealthReason(reasons, RuntimeHealthReason::SequenceGap);
            }
            if (frame.facts.health.boundaryMarkerMismatch ||
                frame.facts.health.pendingBoundaryMarkerPair ||
                frame.facts.health.coalescedSnapshot ||
                frame.facts.health.crossContextMismatch) {
                reasons = AddRuntimeHealthReason(reasons, RuntimeHealthReason::BoundaryMismatch);
            }
            return reasons;
        }

        RuntimeHealthReasonMask RuntimeHealthReasonsFromTransition(const ingress::AssembledFactFrame& frame)
        {
            auto reasons = RuntimeHealthMask(RuntimeHealthReason::BoundaryMismatch);
            if (frame.transition.reason == ingress::TransitionReason::QueueOverflow) {
                reasons = AddRuntimeHealthReason(reasons, RuntimeHealthReason::QueueOverflow);
            }
            return reasons;
        }

        RuntimeHealthReasonMask AddPromptScopeFrozenForDegradedStableFrame(RuntimeHealthReasonMask reasons)
        {
            const bool promptBlocking =
                HasRuntimeHealthReason(reasons, RuntimeHealthReason::GraphUnavailable) ||
                HasRuntimeHealthReason(reasons, RuntimeHealthReason::ManifestEpochSkew) ||
                HasRuntimeHealthReason(reasons, RuntimeHealthReason::ContextRevisionSkew) ||
                HasRuntimeHealthReason(reasons, RuntimeHealthReason::QueueOverflow) ||
                HasRuntimeHealthReason(reasons, RuntimeHealthReason::SequenceGap) ||
                HasRuntimeHealthReason(reasons, RuntimeHealthReason::BoundaryMismatch) ||
                HasRuntimeHealthReason(reasons, RuntimeHealthReason::UpstreamXInputRouteFailed) ||
                HasRuntimeHealthReason(reasons, RuntimeHealthReason::HookInstallFailed);
            if (!promptBlocking) {
                return reasons;
            }
            return AddRuntimeHealthReason(reasons, RuntimeHealthReason::PromptScopeFrozen);
        }

        bool ShouldFailClosedRuntimeOutput(RuntimeHealthReasonMask reasons)
        {
            return HasRuntimeHealthReason(reasons, RuntimeHealthReason::UpstreamXInputRouteFailed) ||
                HasRuntimeHealthReason(reasons, RuntimeHealthReason::HookInstallFailed);
        }

        bool ShouldPublishPromptScope(RuntimeHealthReasonMask reasons)
        {
            return !HasRuntimeHealthReason(reasons, RuntimeHealthReason::PromptScopeFrozen);
        }

        std::optional<RuntimeHealthReason> SkyrimCompatHealthReason(
            presentation::HookInstallStatus status)
        {
            switch (status) {
            case presentation::HookInstallStatus::PartialInstall:
            case presentation::HookInstallStatus::UnsafePartial:
                return RuntimeHealthReason::SkyrimCompatSurfacePartialInstall;
            case presentation::HookInstallStatus::UnsupportedRuntime:
            case presentation::HookInstallStatus::SignatureMismatch:
            case presentation::HookInstallStatus::Failed:
                return RuntimeHealthReason::SkyrimCompatSurfaceHookFailed;
            case presentation::HookInstallStatus::NotAttempted:
            case presentation::HookInstallStatus::Success:
            case presentation::HookInstallStatus::AlreadyInstalled:
            default:
                return std::nullopt;
            }
        }

        bool ShouldLogRuntimePlan()
        {
            return input::RuntimeConfig::GetSingleton().LogActionPlan();
        }

        std::size_t CountPulseSamples(const std::vector<actions::ControlSample>& samples)
        {
            std::size_t count = 0;
            for (const auto& sample : samples) {
                if (sample.pressed || sample.released) {
                    ++count;
                }
            }
            return count;
        }

        void LogRuntimeProjectionPlan(
            const DualPadRuntimeInput& input,
            const GameplayProjectionFrame& projection)
        {
            if (!ShouldLogRuntimePlan()) {
                return;
            }

            const auto& kernel = input.kernel;
            logger::info(
                "[DualPad][RuntimePlan] seq={} manifest={} ctxRevision={} legacyCtx={} legacyEpoch={} health={} samples={} pulses={} changes={} values={} recovery={} gate={} sustained={} transient={} helper={} analog=move({:.3f},{:.3f}) look({:.3f},{:.3f}) triggers({:.3f},{:.3f})",
                kernel.kernelRevision,
                kernel.facts.manifestEpoch,
                kernel.facts.contextRevision,
                input::ToString(input.legacyContext),
                input.legacyContextEpoch,
                RuntimeHealthReasonSummary(input.runtimeHealthReasons),
                kernel.state.controlSamples.size(),
                CountPulseSamples(kernel.state.controlSamples),
                input.resolved.changes.size(),
                input.resolved.values.size(),
                ToString(projection.recoveryPlan.mode),
                ToString(projection.gatePlan.transientDigitalGate),
                projection.gamepadPlan.sustainedDigital.count,
                projection.gamepadPlan.transientDigital.count,
                projection.helperPlan.commands.count,
                projection.gamepadPlan.analog.moveX,
                projection.gamepadPlan.analog.moveY,
                projection.gamepadPlan.analog.lookX,
                projection.gamepadPlan.analog.lookY,
                projection.gamepadPlan.analog.leftTrigger,
                projection.gamepadPlan.analog.rightTrigger);

            constexpr std::size_t kMaxActionLogs = 6;
            const auto limit = (std::min)(input.resolved.changes.size(), kMaxActionLogs);
            for (std::size_t index = 0; index < limit; ++index) {
                const auto& change = input.resolved.changes[index];
                logger::info(
                    "[DualPad][RuntimePlanAction] seq={} index={} action={} phase={} binding={} ts={} firstEdge={} lastEdge={}",
                    kernel.kernelRevision,
                    index,
                    change.actionId,
                    ToString(change.phase),
                    change.bindingId,
                    change.timestampUs,
                    change.firstEdgeUs,
                    change.lastEdgeUs);
            }
        }
    }

    DualPadRuntime& DualPadRuntime::GetSingleton()
    {
        static DualPadRuntime runtime;
        return runtime;
    }

    bool DualPadRuntime::LiveCoordinatorPresentationAuthorityReachable()
    {
        return false;
    }

    DualPadRuntimeResult DualPadRuntime::ProcessGameplayFrameForTests(
        const DualPadRuntimeInput& input,
        IPollOutputExecutor& executor)
    {
        return ProcessGameplayFrameWithExecutor(input, executor);
    }

    DualPadRuntimeResult DualPadRuntime::ProcessAssembledFrameForTests(
        const ingress::AssembledFactFrame& frame,
        IPollOutputExecutor& executor)
    {
        if (frame.kind == ingress::AssembledFrameKind::Transition) {
            auto result = ProcessTransitionFrame(frame);
            PublishRuntimeDebugSnapshot(frame, result);
            return result;
        }

        auto envelope = BindRuntimeEnvelope(frame);
        auto input = BuildStableRuntimeInput(envelope);
        auto result = ProcessGameplayFrameWithExecutor(input, executor);
        PublishStablePresentationSurface(envelope, result);
        PublishRuntimeDebugSnapshot(frame, result);
        return result;
    }

    FrameRuntimeEnvelope DualPadRuntime::BindRuntimeEnvelope(const ingress::AssembledFactFrame& frame) const
    {
        const auto bundle = config::AtomicConfigReloader::GetSingleton().GetActiveBundleSnapshot();
        auto envelope = FrameRuntimeEnvelope{
            .frame = frame,
            .config = RuntimeConfigSnapshot{
                .bundle = bundle,
                .graph = actions::CompiledActionGraphPublisher::GetRuntimeOwner().GetActiveSnapshot(),
                .context = context::ContextResolver::GetSingleton().GetPublishedSnapshot(),
                .manifestEpoch = frame.facts.manifestEpoch,
                .configGeneration = bundle ? bundle->manifestEpoch : 0
            },
            .healthReasons = RuntimeHealthReasonsFromIngress(frame)
        };

        if (envelope.config.context.contextRevision != frame.facts.contextRevision) {
            envelope.healthReasons = AddRuntimeHealthReason(
                envelope.healthReasons,
                RuntimeHealthReason::ContextRevisionSkew);
        }
        if (envelope.config.bundle && envelope.config.bundle->manifestEpoch != frame.facts.manifestEpoch) {
            envelope.healthReasons = AddRuntimeHealthReason(
                envelope.healthReasons,
                RuntimeHealthReason::ManifestEpochSkew);
        }
        const auto upstreamRoute = input::GetUpstreamRouteInstallSnapshot();
        if (upstreamRoute.configured && upstreamRoute.failed) {
            envelope.healthReasons = AddRuntimeHealthReason(
                envelope.healthReasons,
                RuntimeHealthReason::UpstreamXInputRouteFailed);
            envelope.debugReason = std::string(upstreamRoute.debugReason);
        }
        const auto hookInstall = presentation::SkyrimCompatibilitySurface::GetSingleton().GetInstallResult();
        if (const auto reason = SkyrimCompatHealthReason(hookInstall.status)) {
            envelope.healthReasons = AddRuntimeHealthReason(
                envelope.healthReasons,
                *reason);
            if (envelope.debugReason.empty()) {
                envelope.debugReason = presentation::ToDebugString(hookInstall);
            }
        }
        switch (envelope.config.context.menuObserverCompleteness) {
        case menu::ObserverCompleteness::Partial:
            envelope.healthReasons = AddRuntimeHealthReason(
                envelope.healthReasons,
                RuntimeHealthReason::MenuObserverPartial);
            break;
        case menu::ObserverCompleteness::Unavailable:
            envelope.healthReasons = AddRuntimeHealthReason(
                envelope.healthReasons,
                RuntimeHealthReason::MenuObserverUnavailable);
            break;
        case menu::ObserverCompleteness::Complete:
        default:
            break;
        }
        if (envelope.config.context.menuIdentityDegraded) {
            envelope.healthReasons = AddRuntimeHealthReason(
                envelope.healthReasons,
                RuntimeHealthReason::MenuIdentityDegraded);
        }
        return envelope;
    }

    GameplayPolicy BuildGameplayPolicyFromFacts(
        const ingress::FactFrame& facts,
        bool gameplayContext,
        const GameplayRecoveryInput& recovery)
    {
        const auto* kbm = facts.kbmGameplay &&
                facts.kbmGameplay->virtualGameplayEligible &&
                facts.kbmGameplay->current.complete ?
            &*facts.kbmGameplay : nullptr;
        const auto keyboardMoveActive = kbm && kbm->current.keyboardMoveHeldMask != 0;
        const auto keyboardMouseCombatActive = kbm &&
            (kbm->current.keyboardCombatHeldMask != 0 || kbm->current.mouseCombatHeldMask != 0);
        const auto keyboardMouseDigitalActive = kbm &&
            (kbm->current.keyboardTransientHeldMask != 0 || kbm->current.mouseTransientHeldMask != 0);

        return GameplayPolicy{
            .outputTickUs = facts.monotonicUs,
            .lastPhysicalMouseMoveOwnerUs = kbm ? kbm->lastPhysicalMouseMoveOwnerUs : 0,
            .gameplayContext = gameplayContext,
            .mouseLookActive = kbm && kbm->physicalMouseMoveThisFrame,
            .mouseLookActivatedThisFrame = kbm && kbm->physicalMouseMoveThisFrame,
            .keyboardMoveActive = keyboardMoveActive,
            .keyboardMoveActivatedThisFrame = keyboardMoveActive,
            .keyboardMouseCombatActive = keyboardMouseCombatActive,
            .keyboardMouseCombatActivatedThisFrame = keyboardMouseCombatActive,
            .keyboardMouseDigitalActive = keyboardMouseDigitalActive,
            .keyboardMouseDigitalActivatedThisFrame = keyboardMouseDigitalActive,
            .keyboardPhysicalSustainedActive = kbm && kbm->current.keyboardSustainedHeldMask != 0,
            .mousePhysicalSustainedActive = kbm && kbm->current.mouseSustainedHeldMask != 0,
            .keyboardSustainedEventOrdinal = kbm ? kbm->keyboardSustainedEventOrdinal : 0,
            .mouseSustainedEventOrdinal = kbm ? kbm->mouseSustainedEventOrdinal : 0,
            .clearGamepadSustainedContributor =
                recovery.resetScope == RecoveryResetScope::GamepadSource &&
                HasRecoveryRequest(recovery),
            .arbitrationResetMode = recovery.resetScope == RecoveryResetScope::GamepadSource &&
                    HasRecoveryRequest(recovery) ?
                ChannelArbitrationResetMode::GamepadSource :
                ChannelArbitrationResetMode::None
        };
    }

    DualPadRuntimeInput DualPadRuntime::BuildStableRuntimeInput(const FrameRuntimeEnvelope& envelope)
    {
        const auto& frame = envelope.frame;
        auto kernel = ingress::BuildKernelFrame(frame);
        auto resolved = actions::ResolvedActionFrame{
            .manifestEpoch = kernel.facts.manifestEpoch,
            .contextRevision = kernel.facts.contextRevision
        };
        bool graphAvailableForKernel = false;
        auto runtimeHealthReasons = envelope.healthReasons;

        if (ingress::ShouldDispatchToInteractionEngine(frame)) {
            const auto& graphSnapshot = envelope.config.graph;
            if (const auto graph = graphSnapshot.graph;
                graph && graphSnapshot.manifestEpoch == kernel.facts.manifestEpoch &&
                graph->manifestEpoch == kernel.facts.manifestEpoch &&
                !HasRuntimeHealthReason(runtimeHealthReasons, RuntimeHealthReason::ContextRevisionSkew)) {
                graphAvailableForKernel = true;
                resolved = _interactionEngine.Resolve(
                    *graph,
                    envelope.config.context.actionSetStack,
                    kernel,
                    _interactionState);
                resolved.manifestEpoch = kernel.facts.manifestEpoch;
                resolved.contextRevision = kernel.facts.contextRevision;
            }
        }
        if (ingress::ShouldDispatchToInteractionEngine(frame) && !graphAvailableForKernel) {
            kernel.state.healthDegraded = true;
            const auto graph = envelope.config.graph.graph;
            if (!graph) {
                runtimeHealthReasons = AddRuntimeHealthReason(
                    runtimeHealthReasons,
                    RuntimeHealthReason::GraphUnavailable);
            } else if (
                envelope.config.graph.manifestEpoch != kernel.facts.manifestEpoch ||
                graph->manifestEpoch != kernel.facts.manifestEpoch) {
                runtimeHealthReasons = AddRuntimeHealthReason(
                    runtimeHealthReasons,
                    RuntimeHealthReason::ManifestEpochSkew);
            }
        }

        const auto& contextSnapshot = envelope.config.context;
        GameplayRecoveryInput recovery{ .cleanFrame = true };
        if (_hasPendingRecovery) {
            MergeRecovery(recovery, _pendingRecovery);
            _pendingRecovery = GameplayRecoveryInput{};
            _hasPendingRecovery = false;
        }

        kernel.state.healthDegraded = kernel.state.healthDegraded ||
            runtimeHealthReasons != RuntimeHealthMask(RuntimeHealthReason::None);

        auto currentCycle = RuntimeInputPublication::GetSingleton().FindActiveCallbackAudit();
        if (!currentCycle && frame.facts.kbmGameplay) {
            currentCycle = RuntimeInputPublication::GetSingleton().FindCallbackAudit(
                frame.facts.kbmGameplay->ownerTickToken);
        }

        const auto policy = BuildGameplayPolicyFromFacts(
            frame.facts,
            contextSnapshot.hostMode == context::HostMode::Gameplay,
            recovery);
        const auto* kbm = frame.facts.kbmGameplay ? &*frame.facts.kbmGameplay : nullptr;
        const input::KbmIngressDiagnosticInput kbmDiagnostic{
            .contextRevision = frame.facts.contextRevision,
            .menuStackRevision = contextSnapshot.menuStackRevision,
            .controlMapRevision = frame.facts.coherence.controlMapRevision,
            .bindingGeneration = kbm ? kbm->bindingGeneration : 0,
            .physicalDownCount = kbm ? static_cast<std::uint32_t>(kbm->physical.downCodes.count) : 0,
            .quarantineCount = kbm ? static_cast<std::uint32_t>(kbm->physical.quarantineCodes.count) : 0,
            .keyboardMoveHeldMask = kbm ? kbm->current.keyboardMoveHeldMask : 0,
            .keyboardCombatHeldMask = kbm ? kbm->current.keyboardCombatHeldMask : 0,
            .mouseCombatHeldMask = kbm ? kbm->current.mouseCombatHeldMask : 0,
            .keyboardTransientHeldMask = kbm ? kbm->current.keyboardTransientHeldMask : 0,
            .mouseTransientHeldMask = kbm ? kbm->current.mouseTransientHeldMask : 0,
            .keyboardSustainedHeldMask = kbm ? kbm->current.keyboardSustainedHeldMask : 0,
            .mouseSustainedHeldMask = kbm ? kbm->current.mouseSustainedHeldMask : 0,
            .bindingsComplete = kbm && kbm->current.complete,
            .eventListComplete = kbm && kbm->physical.complete,
            .batchBuilt = kbm != nullptr,
            .batchAccepted = kbm && kbm->virtualGameplayEligible
        };
        const auto kbmSample = g_kbmRuntimeSampler.Observe(
            frame.facts.monotonicUs / 1000,
            kbmDiagnostic);
        if (kbmSample.record) {
            logger::info(
                "[DualPad][KbmRuntimeShadow] callbackCount={} sampleReason={} fingerprint=0x{:X} captureGeneration={} ownerTickToken={} eventBatchToken={} contextRevision={} menuStackRevision={} inputStateEpoch={} gamepadSessionId={} controlMapRevision={} orderedCutoffSeq={} factPresent={} virtualGameplayEligible={} currentComplete={} physicalComplete={} keyboardMove=0x{:X} keyboardCombat=0x{:X} mouseCombat=0x{:X} keyboardTransient=0x{:X} mouseTransient=0x{:X} keyboardSustained=0x{:X} mouseSustained=0x{:X} physicalDownCount={} quarantineCount={} policyGameplayContext={} policyMoveActive={} policyCombatActive={} policyDigitalActive={} policyMouseLookActive={} runtimeHealth=0x{:X} productionMutationEnabled=false enginePatchEnabled=false",
                kbmSample.callbackCount,
                input::ToString(kbmSample.reason),
                input::BuildKbmIngressFingerprint(kbmDiagnostic),
                frame.facts.coherence.captureGeneration,
                kbm ? kbm->ownerTickToken : 0,
                kbm ? kbm->eventBatchToken : 0,
                frame.facts.contextRevision,
                contextSnapshot.menuStackRevision,
                frame.facts.coherence.inputStateEpoch,
                frame.facts.coherence.gamepadSessionId,
                frame.facts.coherence.controlMapRevision,
                frame.facts.coherence.orderedCutoffSeq,
                kbm != nullptr,
                kbm && kbm->virtualGameplayEligible,
                kbm && kbm->current.complete,
                kbm && kbm->physical.complete,
                kbm ? kbm->current.keyboardMoveHeldMask : 0,
                kbm ? kbm->current.keyboardCombatHeldMask : 0,
                kbm ? kbm->current.mouseCombatHeldMask : 0,
                kbm ? kbm->current.keyboardTransientHeldMask : 0,
                kbm ? kbm->current.mouseTransientHeldMask : 0,
                kbm ? kbm->current.keyboardSustainedHeldMask : 0,
                kbm ? kbm->current.mouseSustainedHeldMask : 0,
                kbm ? kbm->physical.downCodes.count : 0,
                kbm ? kbm->physical.quarantineCodes.count : 0,
                policy.gameplayContext,
                policy.keyboardMoveActive,
                policy.keyboardMouseCombatActive,
                policy.keyboardMouseDigitalActive,
                policy.mouseLookActive,
                runtimeHealthReasons);
        }

        return DualPadRuntimeInput{
            .kernel = kernel,
            .resolved = std::move(resolved),
            .policy = policy,
            .recovery = recovery,
            .currentCyclePlan = currentCycle ?
                std::optional{ currentCycle->plan } : std::nullopt,
            .currentCycleAudit = currentCycle ?
                std::optional{ currentCycle->audit } : std::nullopt,
            .currentCycleEvidence = currentCycle ?
                std::optional{ currentCycle->evidence } : std::nullopt,
            .inputStateEpoch = frame.facts.coherence.inputStateEpoch,
            .gamepadSessionId = frame.facts.coherence.gamepadSessionId,
            .controlMapRevision = frame.facts.coherence.controlMapRevision,
            .orderedCutoffSeq = frame.facts.coherence.orderedCutoffSeq,
            .eventBatchToken = frame.facts.kbmGameplay ?
                frame.facts.kbmGameplay->eventBatchToken : 0,
            .runtimeHealthReasons = runtimeHealthReasons,
            .outputTick = kernel.facts.monotonicUs,
            .legacyContext = contextSnapshot.legacyInputContext,
            .legacyContextEpoch = contextSnapshot.legacyContextEpoch,
            .runtimeHealthDebugReason = envelope.debugReason
        };
    }

    DualPadRuntimeResult DualPadRuntime::ProcessTransitionFrame(const ingress::AssembledFactFrame& frame)
    {
        const auto recovery = ingress::ToGameplayRecoveryInput(frame);
        if (ShouldClearProjectionStickyOwners(recovery)) {
            _interactionState.Reset();
            _lastProjectionFrame = GameplayProjectionFrame{};
            _channelArbitration = ChannelArbitrationStateSet{};
            RuntimeInputPublication::GetSingleton().ResetCommittedState();
        }
        if (HasRecoveryRequest(recovery)) {
            MergeRecovery(_pendingRecovery, recovery);
            _hasPendingRecovery = true;
        }

        return DualPadRuntimeResult{
            .projectionFrame = _lastProjectionFrame,
            .output = PollOutputApplyResult{},
            .gameplayPresentation = _presentationPublisher.GetPublished(),
            .runtimeHealthReasons = RuntimeHealthReasonsFromTransition(frame)
        };
    }

    void DualPadRuntime::PublishStablePresentationSurface(
        const FrameRuntimeEnvelope& envelope,
        const DualPadRuntimeResult& result)
    {
        const auto& frame = envelope.frame;
        if (frame.kind != ingress::AssembledFrameKind::Stable || !result.output.outputApplySucceeded) {
            return;
        }

        const auto presentationBefore = _presentationProjection.GetPublished();
        const auto pendingCursorPlanBefore = _presentationProjection.GetPendingCursorPlan();
        const auto ownerTickToken = frame.facts.kbmGameplay ?
            frame.facts.kbmGameplay->ownerTickToken : frame.facts.coherence.captureGeneration;
        std::optional<presentation::CursorHandoffAck> cursorAck;
        if (const auto& pending = pendingCursorPlanBefore; pending) {
            if (const auto ackEnvelope =
                    presentation::CursorHandoffAckMailbox::GetSingleton().ConsumeExactOnOwnerTick(
                        pending->token,
                        pending->contextRevision,
                        pending->presentationEpoch,
                        pending->targetMenuInstanceId)) {
                cursorAck = ackEnvelope->ack;
            }
        }
        const actions::ResolvedActionFrame resolvedForPresentation{};
        const bool needsOrderedProjection = !frame.facts.sourceActivities.empty() ||
            cursorAck.has_value();
        const auto published = needsOrderedProjection ?
            _presentationProjection.ProjectOrdered(
                frame.facts.sourceEvidence,
                envelope.config.context,
                result.gameplayPresentation,
                frame.facts.sourceActivities,
                resolvedForPresentation,
                frame.facts.monotonicUs / 1000,
                frame.facts.coherence.inputStateEpoch,
                ownerTickToken,
                cursorAck) :
            _presentationProjection.Project(
                frame.facts.sourceEvidence,
                envelope.config.context,
                result.gameplayPresentation,
                frame.facts.monotonicUs / 1000);
        auto& compatibilitySurface = presentation::SkyrimCompatibilitySurface::GetSingleton();
        compatibilitySurface.Commit(published);
        const auto committedSensitive = RuntimeInputPublication::GetSingleton().GetCommitted();
        const bool engineSnapshotCurrent =
            committedSensitive.inputStateEpoch == frame.facts.coherence.inputStateEpoch &&
            committedSensitive.gamepadSessionId == frame.facts.coherence.gamepadSessionId &&
            committedSensitive.controlMapRevision == frame.facts.coherence.controlMapRevision &&
            committedSensitive.orderedCutoffSeq == frame.facts.coherence.orderedCutoffSeq;
        if (engineSnapshotCurrent) {
            RuntimeInputPublication::GetSingleton().PublishOriginalEngineModeShadow(
                ownerTickToken,
                frame.facts.coherence.inputStateEpoch,
                frame.facts.contextRevision);
        }
        telemetry::MixedInputEvidenceRecorder::GetSingleton().RecordPresentation(
            telemetry::PresentationEvidenceRecord{
                .monotonicUs = frame.facts.monotonicUs,
                .ownerTickToken = ownerTickToken,
                .before = presentationBefore,
                .after = published,
                .planBefore = pendingCursorPlanBefore,
                .planAfter = _presentationProjection.GetPendingCursorPlan(),
                .ack = cursorAck,
                .engine = RuntimeInputPublication::GetSingleton().GetEngineModeShadow(),
                .engineSnapshotCurrent = engineSnapshotCurrent });
        if (ShouldPublishPromptScope(result.runtimeHealthReasons)) {
            prompt::PromptRuntimeOwner::GetSingleton().PublishPresentationState(
                published,
                prompt::PromptRuntimeBaseline{
                    .manifestEpoch = envelope.config.manifestEpoch,
                    .configGeneration = envelope.config.configGeneration,
                    .bundle = envelope.config.bundle,
                    .graph = envelope.config.graph
                });
        }
        compatibilitySurface.RefreshMenusIfNeeded();
    }

    void DualPadRuntime::PublishRuntimeDebugSnapshot(
        const ingress::AssembledFactFrame& frame,
        const DualPadRuntimeResult& result)
    {
        const auto hookInstall = presentation::SkyrimCompatibilitySurface::GetSingleton().GetInstallResult();
        _lastDebugSnapshot = ProjectRuntimeDebugSnapshot(RuntimeDebugProjectionInput{
            .frame = frame,
            .runtimeHealthReasons = result.runtimeHealthReasons,
            .runtimeHealthDebugReason = result.runtimeHealthDebugReason,
            .outputApplySucceeded = result.output.outputApplySucceeded,
            .hookInstall = hookInstall,
            .upstreamRoute = input::GetUpstreamRouteInstallSnapshot()
        });
        LogRuntimeDebugSnapshotTransition(_diagnosticsLogState, _lastDebugSnapshot);
    }

    DualPadRuntimeResult DualPadRuntime::ProcessGameplayFrameWithExecutor(
        const DualPadRuntimeInput& input,
        IPollOutputExecutor& executor)
    {
        if (ShouldFailClosedRuntimeOutput(input.runtimeHealthReasons)) {
            const auto runtimeHealthReasons = AddPromptScopeFrozenForDegradedStableFrame(input.runtimeHealthReasons);
            return DualPadRuntimeResult{
                .projectionFrame = GameplayProjectionFrame{},
                .output = PollOutputApplyResult{},
                .gameplayPresentation = _presentationPublisher.GetPublished(),
                .runtimeHealthReasons = runtimeHealthReasons,
                .runtimeHealthDebugReason = input.runtimeHealthDebugReason
            };
        }

        auto previous = ShouldClearProjectionStickyOwners(input.recovery) ?
            GameplayProjectionFrame{} :
            _lastProjectionFrame;
        previous.nextArbitration = ShouldClearProjectionStickyOwners(input.recovery) ?
            ChannelArbitrationStateSet{} :
            _channelArbitration;
        auto projection = ResolveGameplayProjection(
            input.kernel,
            input.resolved,
            input.policy,
            previous,
            input.recovery);

        CurrentCycleGatePlan currentCyclePlan{};
        currentCyclePlan.commitCurrentCycleSensitiveState = true;
        if (input.currentCyclePlan) {
            currentCyclePlan = *input.currentCyclePlan;
        }
        if (projection.sprintDecision.requiresCurrentCycleMutation) {
            currentCyclePlan.sustainedDigital = CurrentCycleEventDisposition::Suppress;
            currentCyclePlan.affectedChannels |=
                CurrentCycleChannelMask(CurrentCycleChannel::SustainedDigital);
            currentCyclePlan.requiresEventMutation = true;
            currentCyclePlan.commitCurrentCycleSensitiveState = false;
            currentCyclePlan.currentEventWriterCount = 1;
            currentCyclePlan.nextPollWriterCount = 1;
        }
        CurrentCycleAdapterAudit currentCycleAudit{
            .success = true,
            .shadowOnly = true,
            .affectedChannels = currentCyclePlan.affectedChannels,
            .currentEventWriterCount = currentCyclePlan.currentEventWriterCount
        };
        if (input.currentCycleAudit) {
            currentCycleAudit = *input.currentCycleAudit;
        }

        const auto committedSensitive = RuntimeInputPublication::GetSingleton().GetCommitted();
        auto proposedSensitive = committedSensitive;
        proposedSensitive.channels = projection.nextArbitration;
        proposedSensitive.sprint = projection.sprintDecision.next;
        ++proposedSensitive.revision;
        proposedSensitive.inputStateEpoch = input.inputStateEpoch;
        proposedSensitive.gamepadSessionId = input.gamepadSessionId;
        proposedSensitive.controlMapRevision = input.controlMapRevision;
        proposedSensitive.orderedCutoffSeq = input.orderedCutoffSeq;
        proposedSensitive.eventBatchToken = input.eventBatchToken;
        proposedSensitive.sprint.inputStateEpoch = input.inputStateEpoch;
        proposedSensitive.sprint.contextRevision = input.kernel.facts.contextRevision;
        proposedSensitive.sprint.runtimeGeneration = input.kernel.kernelRevision;
        const auto prepared = RuntimeInputPublication::GetSingleton().Prepare(
            proposedSensitive,
            currentCyclePlan);

        if (currentCyclePlan.requiresEventMutation &&
            !IsCurrentCycleAuditCommitSafe(currentCyclePlan, currentCycleAudit)) {
            FailClosedAffectedChannels(
                projection,
                static_cast<CurrentCycleChannelMaskType>(
                    currentCycleAudit.affectedChannels |
                    currentCyclePlan.affectedChannels),
                committedSensitive);
        }
        LogRuntimeProjectionPlan(input, projection);

        auto output = _pollOutputAdapter.Apply(projection, executor);
        auto commitAudit = currentCycleAudit;
        if (!currentCyclePlan.requiresEventMutation &&
            (currentCyclePlan.commitCurrentCycleSensitiveState ||
                currentCyclePlan.affectedChannels == 0)) {
            commitAudit.success = true;
            commitAudit.failure = CurrentCycleGateFailure::None;
        }
        if (!output.outputApplySucceeded) {
            commitAudit.success = false;
            commitAudit.failure = CurrentCycleGateFailure::AdapterFailure;
            commitAudit.affectedChannels = currentCyclePlan.affectedChannels;
        }
        const auto sensitiveCommit = RuntimeInputPublication::GetSingleton()
            .CommitAfterCurrentCycleAudit(prepared.token, commitAudit);
        if (input.currentCycleEvidence) {
            telemetry::MixedInputEvidenceRecorder::GetSingleton().Record(
                telemetry::MixedInputEvidenceRecord{
                    .monotonicUs = input.currentCycleEvidence->monotonicUs,
                    .ownerTickToken = input.currentCycleEvidence->ownerTickToken,
                    .currentInputStateEpoch = input.currentCycleEvidence->currentInputStateEpoch,
                    .currentGamepadSessionId = input.currentCycleEvidence->currentGamepadSessionId,
                    .receiptFailure = input.currentCycleEvidence->receiptFailure,
                    .receipt = input.currentCycleEvidence->receipt,
                    .plan = currentCyclePlan,
                    .audit = commitAudit,
                    .sprintDecision = projection.sprintDecision,
                    .before = committedSensitive,
                    .after = RuntimeInputPublication::GetSingleton().GetCommitted(),
                    .commit = sensitiveCommit });
        }
        auto published = _presentationPublisher.GetPublished();
        if (output.outputApplySucceeded) {
            published = PublishGameplayPresentation(projection, input.outputTick, true);
        }

        if (output.outputApplySucceeded && sensitiveCommit.committed) {
            _lastProjectionFrame = projection;
            _channelArbitration = projection.nextArbitration;
        }

        auto runtimeHealthReasons = input.runtimeHealthReasons;
        if (input.kernel.state.healthDegraded &&
            runtimeHealthReasons == RuntimeHealthMask(RuntimeHealthReason::None)) {
            runtimeHealthReasons = AddRuntimeHealthReason(
                runtimeHealthReasons,
                RuntimeHealthReason::BoundaryMismatch);
        }
        runtimeHealthReasons = AddPromptScopeFrozenForDegradedStableFrame(runtimeHealthReasons);

        return DualPadRuntimeResult{
            .projectionFrame = projection,
            .output = std::move(output),
            .gameplayPresentation = published,
            .runtimeHealthReasons = runtimeHealthReasons,
            .runtimeHealthDebugReason = input.runtimeHealthDebugReason
        };
    }

    presentation::PublishedGameplayPresentation DualPadRuntime::PublishGameplayPresentation(
        const GameplayProjectionFrame& frame,
        std::uint64_t tick,
        bool outputApplySucceeded)
    {
        return _presentationPublisher.PublishAfterOutputApply(frame, tick, outputApplySucceeded);
    }

    presentation::PublishedGameplayPresentation DualPadRuntime::GetPublishedGameplayPresentation() const
    {
        return _presentationPublisher.GetPublished();
    }

    GameplayProjectionFrame DualPadRuntime::GetLastProjectionFrame() const
    {
        return _lastProjectionFrame;
    }

    RuntimeDebugSnapshot DualPadRuntime::GetLastDebugSnapshot() const
    {
        return _lastDebugSnapshot;
    }

    void DualPadRuntime::ResetMutableState()
    {
        _lastProjectionFrame = GameplayProjectionFrame{};
        _channelArbitration = ChannelArbitrationStateSet{};
        RuntimeInputPublication::GetSingleton().Reset();
        _lastDebugSnapshot = RuntimeDebugSnapshot{};
        _diagnosticsLogState = RuntimeDiagnosticsLogState{};
        _pendingRecovery = GameplayRecoveryInput{};
        _hasPendingRecovery = false;
        _interactionState.Reset();
        _presentationPublisher.ResetForTests();
        _presentationProjection.ResetForTests();
        presentation::CursorHandoffAckMailbox::GetSingleton().ResetForTests();
    }

    void DualPadRuntime::ResetForTests()
    {
        ResetMutableState();
    }
}
