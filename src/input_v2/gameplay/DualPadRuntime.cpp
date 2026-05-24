#include "pch.h"

#include "input_v2/gameplay/DualPadRuntime.h"

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
        _presentationPublisher.ResetForTests();
    }
}
