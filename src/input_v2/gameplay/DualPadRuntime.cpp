#include "pch.h"

#include "input_v2/gameplay/DualPadRuntime.h"

#include "input_v2/actions/CompiledActionGraphPublisher.h"
#include "input_v2/context/ContextResolver.h"
#include "input_v2/ingress/IngressRecovery.h"

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

    DualPadRuntimeResult DualPadRuntime::ProcessAssembledFrame(const ingress::AssembledFactFrame& frame)
    {
        auto recovery = frame.kind == ingress::AssembledFrameKind::Transition ?
            ingress::ToGameplayRecoveryInput(frame) :
            GameplayRecoveryInput{ .cleanFrame = true };

        if (recovery.hardResetRequested) {
            _interactionState.Reset();
        }

        auto kernel = ingress::BuildKernelFrame(frame);
        auto resolved = actions::ResolvedActionFrame{
            .manifestEpoch = kernel.facts.manifestEpoch,
            .contextRevision = kernel.facts.contextRevision
        };

        if (frame.kind == ingress::AssembledFrameKind::Stable &&
            ingress::ShouldDispatchToInteractionEngine(frame)) {
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
        return ProcessGameplayFrame(DualPadRuntimeInput{
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
        });
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
        _interactionState.Reset();
        _presentationPublisher.ResetForTests();
    }
}
