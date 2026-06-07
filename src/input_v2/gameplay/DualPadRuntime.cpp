#include "pch.h"

#include "input_v2/gameplay/DualPadRuntime.h"

#include "input_v2/actions/CompiledActionGraphPublisher.h"
#include "input_v2/config/AtomicConfigReloader.h"
#include "input_v2/context/ContextResolver.h"
#include "input_v2/ingress/IngressRecovery.h"
#include "input_v2/presentation/SkyrimCompatibilitySurface.h"
#include "input_v2/prompt/PromptRuntimeOwner.h"

namespace dualpad::input_v2::gameplay
{
    namespace
    {
        bool ShouldClearProjectionStickyOwners(const GameplayRecoveryInput& recovery)
        {
            return recovery.softResyncRequested ||
                recovery.hardResetRequested ||
                recovery.sequenceGapObserved ||
                recovery.explicitResetRequested;
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
            target.softResyncRequested = target.softResyncRequested || source.softResyncRequested;
            target.hardResetRequested = target.hardResetRequested || source.hardResetRequested;
            target.sequenceGapObserved = target.sequenceGapObserved || source.sequenceGapObserved;
            target.explicitResetRequested = target.explicitResetRequested || source.explicitResetRequested;
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
            if (reasons == RuntimeHealthMask(RuntimeHealthReason::None)) {
                return reasons;
            }
            return AddRuntimeHealthReason(reasons, RuntimeHealthReason::PromptScopeFrozen);
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
        const auto hookInstall = presentation::SkyrimCompatibilitySurface::GetSingleton().GetInstallResult();
        if (presentation::IsHookInstallFailure(hookInstall)) {
            envelope.healthReasons = AddRuntimeHealthReason(
                envelope.healthReasons,
                RuntimeHealthReason::HookInstallFailed);
            envelope.debugReason = presentation::ToDebugString(hookInstall);
        }
        return envelope;
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

        return DualPadRuntimeInput{
            .kernel = kernel,
            .resolved = std::move(resolved),
            .policy = GameplayPolicy{
                .gameplayContext = contextSnapshot.hostMode == context::HostMode::Gameplay,
                .mouseLookActive = false,
                .keyboardMoveActive = false,
                .keyboardMouseCombatActive = false,
                .keyboardMouseDigitalActive = false,
                .keyboardPhysicalSustainedActive = false,
                .mousePhysicalSustainedActive = false
            },
            .recovery = recovery,
            .runtimeHealthReasons = runtimeHealthReasons,
            .outputTick = kernel.facts.monotonicUs,
            .legacyContext = contextSnapshot.legacyInputContext,
            .runtimeHealthDebugReason = envelope.debugReason
        };
    }

    DualPadRuntimeResult DualPadRuntime::ProcessTransitionFrame(const ingress::AssembledFactFrame& frame)
    {
        const auto recovery = ingress::ToGameplayRecoveryInput(frame);
        if (ShouldClearProjectionStickyOwners(recovery)) {
            _interactionState.Reset();
            _lastProjectionFrame = GameplayProjectionFrame{};
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

        const auto published = _presentationProjection.Project(
            frame.facts.sourceEvidence,
            envelope.config.context,
            result.gameplayPresentation);
        presentation::SkyrimCompatibilitySurface::GetSingleton().Commit(published);
        if (result.RuntimeHealthDegraded()) {
            return;
        }
        prompt::PromptRuntimeOwner::GetSingleton().PublishPresentationState(
            published,
            prompt::PromptRuntimeBaseline{
                .manifestEpoch = envelope.config.manifestEpoch,
                .configGeneration = envelope.config.configGeneration,
                .bundle = envelope.config.bundle,
                .graph = envelope.config.graph
            });
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
            .hookInstall = hookInstall
        });
        LogRuntimeDebugSnapshotTransition(_diagnosticsLogState, _lastDebugSnapshot);
    }

    DualPadRuntimeResult DualPadRuntime::ProcessGameplayFrameWithExecutor(
        const DualPadRuntimeInput& input,
        IPollOutputExecutor& executor)
    {
        if (HasRuntimeHealthReason(input.runtimeHealthReasons, RuntimeHealthReason::HookInstallFailed)) {
            const auto runtimeHealthReasons = AddPromptScopeFrozenForDegradedStableFrame(input.runtimeHealthReasons);
            return DualPadRuntimeResult{
                .projectionFrame = GameplayProjectionFrame{},
                .output = PollOutputApplyResult{},
                .gameplayPresentation = _presentationPublisher.GetPublished(),
                .runtimeHealthReasons = runtimeHealthReasons,
                .runtimeHealthDebugReason = input.runtimeHealthDebugReason
            };
        }

        const auto previous = ShouldClearProjectionStickyOwners(input.recovery) ?
            GameplayProjectionFrame{} :
            _lastProjectionFrame;
        auto projection = ResolveGameplayProjection(
            input.kernel,
            input.resolved,
            input.policy,
            previous,
            input.recovery);

        auto output = _pollOutputAdapter.Apply(projection, executor);
        auto published = _presentationPublisher.GetPublished();
        if (output.outputApplySucceeded) {
            published = PublishGameplayPresentation(projection, input.outputTick, true);
        }

        if (output.outputApplySucceeded) {
            _lastProjectionFrame = projection;
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

    const presentation::PublishedGameplayPresentation& DualPadRuntime::GetPublishedGameplayPresentation() const
    {
        return _presentationPublisher.GetPublished();
    }

    const GameplayProjectionFrame& DualPadRuntime::GetLastProjectionFrame() const
    {
        return _lastProjectionFrame;
    }

    const RuntimeDebugSnapshot& DualPadRuntime::GetLastDebugSnapshot() const
    {
        return _lastDebugSnapshot;
    }

    void DualPadRuntime::ResetForTests()
    {
        _lastProjectionFrame = GameplayProjectionFrame{};
        _lastDebugSnapshot = RuntimeDebugSnapshot{};
        _diagnosticsLogState = RuntimeDiagnosticsLogState{};
        _pendingRecovery = GameplayRecoveryInput{};
        _hasPendingRecovery = false;
        _interactionState.Reset();
        _presentationPublisher.ResetForTests();
        _presentationProjection.ResetForTests();
    }
}
