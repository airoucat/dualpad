#include "pch.h"

#include "input_v2/gameplay/DualPadRuntime.h"

#include "input_v2/actions/CompiledActionGraphPublisher.h"
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
            return ProcessTransitionFrame(frame);
        }

        auto input = BuildStableRuntimeInput(frame);
        auto result = ProcessGameplayFrameWithExecutor(input, executor);
        PublishStablePresentationSurface(frame, result);
        return result;
    }

    DualPadRuntimeInput DualPadRuntime::BuildStableRuntimeInput(const ingress::AssembledFactFrame& frame)
    {
        auto kernel = ingress::BuildKernelFrame(frame);
        auto resolved = actions::ResolvedActionFrame{
            .manifestEpoch = kernel.facts.manifestEpoch,
            .contextRevision = kernel.facts.contextRevision
        };

        if (ingress::ShouldDispatchToInteractionEngine(frame)) {
            if (const auto graph = actions::CompiledActionGraphPublisher::GetRuntimeOwner().GetActiveGraph()) {
                const auto& contextSnapshot = context::ContextResolver::GetSingleton().GetPublishedSnapshot();
                resolved = _interactionEngine.Resolve(
                    *graph,
                    contextSnapshot.actionSetStack,
                    kernel,
                    _interactionState);
                resolved.manifestEpoch = kernel.facts.manifestEpoch;
                resolved.contextRevision = kernel.facts.contextRevision;
            }
        }

        const auto& contextSnapshot = context::ContextResolver::GetSingleton().GetPublishedSnapshot();
        GameplayRecoveryInput recovery{ .cleanFrame = true };
        if (_hasPendingRecovery) {
            MergeRecovery(recovery, _pendingRecovery);
            _pendingRecovery = GameplayRecoveryInput{};
            _hasPendingRecovery = false;
        }

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
            .outputTick = kernel.facts.monotonicUs,
            .legacyContext = contextSnapshot.legacyInputContext
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
            .gameplayPresentation = _presentationPublisher.GetPublished()
        };
    }

    void DualPadRuntime::PublishStablePresentationSurface(
        const ingress::AssembledFactFrame& frame,
        const DualPadRuntimeResult& result)
    {
        if (frame.kind != ingress::AssembledFrameKind::Stable || !result.output.outputApplySucceeded) {
            return;
        }

        const auto& contextSnapshot = context::ContextResolver::GetSingleton().GetPublishedSnapshot();
        const auto published = _presentationProjection.Project(
            frame.facts.sourceEvidence,
            contextSnapshot,
            result.gameplayPresentation);
        presentation::SkyrimCompatibilitySurface::GetSingleton().Commit(published);
        prompt::PromptRuntimeOwner::GetSingleton().PublishPresentationState(published);
    }

    DualPadRuntimeResult DualPadRuntime::ProcessGameplayFrameWithExecutor(
        const DualPadRuntimeInput& input,
        IPollOutputExecutor& executor)
    {
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

        return DualPadRuntimeResult{
            .projectionFrame = projection,
            .output = std::move(output),
            .gameplayPresentation = published
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

    void DualPadRuntime::ResetForTests()
    {
        _lastProjectionFrame = GameplayProjectionFrame{};
        _pendingRecovery = GameplayRecoveryInput{};
        _hasPendingRecovery = false;
        _interactionState.Reset();
        _presentationPublisher.ResetForTests();
        _presentationProjection.ResetForTests();
    }
}
