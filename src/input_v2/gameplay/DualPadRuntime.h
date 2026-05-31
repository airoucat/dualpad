#pragma once

#include "input_v2/compat/LegacyInputContextCompat.h"
#include "input_v2/actions/InteractionEngine.h"
#include "input_v2/gameplay/GameplayPresentationPublisher.h"
#include "input_v2/gameplay/GameplayProjectionFrame.h"
#include "input_v2/gameplay/PollOutputAdapter.h"
#include "input_v2/ingress/FrameAssembler.h"
#include "input_v2/presentation/PresentationProjection.h"

#include <cstdint>

namespace dualpad::input_v2::gameplay
{
    struct DualPadRuntimeInput
    {
        actions::KernelFrame kernel{};
        actions::ResolvedActionFrame resolved{};
        GameplayPolicy policy{};
        GameplayRecoveryInput recovery{};
        std::uint64_t outputTick{ 0 };
        dualpad::input::InputContext legacyContext{ dualpad::input::InputContext::Gameplay };
    };

    struct DualPadRuntimeResult
    {
        GameplayProjectionFrame projectionFrame{};
        PollOutputApplyResult output{};
        presentation::PublishedGameplayPresentation gameplayPresentation{};
        bool runtimeHealthDegraded{ false };
    };

    class DualPadRuntime
    {
    public:
        static DualPadRuntime& GetSingleton();
        static bool LiveCoordinatorPresentationAuthorityReachable();

        DualPadRuntimeResult ProcessAssembledFrame(const ingress::AssembledFactFrame& frame);
        DualPadRuntimeResult ProcessAssembledFrameForTests(
            const ingress::AssembledFactFrame& frame,
            IPollOutputExecutor& executor);
        DualPadRuntimeResult ProcessGameplayFrame(const DualPadRuntimeInput& input);
        DualPadRuntimeResult ProcessGameplayFrameForTests(
            const DualPadRuntimeInput& input,
            IPollOutputExecutor& executor);

        presentation::PublishedGameplayPresentation PublishGameplayPresentation(
            const GameplayProjectionFrame& frame,
            std::uint64_t tick,
            bool outputApplySucceeded);

        const presentation::PublishedGameplayPresentation& GetPublishedGameplayPresentation() const;
        const GameplayProjectionFrame& GetLastProjectionFrame() const;
        void ResetForTests();

    private:
        DualPadRuntimeInput BuildStableRuntimeInput(const ingress::AssembledFactFrame& frame);
        DualPadRuntimeResult ProcessTransitionFrame(const ingress::AssembledFactFrame& frame);
        void PublishStablePresentationSurface(
            const ingress::AssembledFactFrame& frame,
            const DualPadRuntimeResult& result);

        DualPadRuntimeResult ProcessGameplayFrameWithExecutor(
            const DualPadRuntimeInput& input,
            IPollOutputExecutor& executor);

        GameplayProjectionFrame _lastProjectionFrame{};
        GameplayRecoveryInput _pendingRecovery{};
        bool _hasPendingRecovery{ false };
        actions::InteractionStateStore _interactionState{};
        actions::InteractionEngine _interactionEngine{};
        GameplayPresentationPublisher _presentationPublisher{};
        presentation::PresentationProjection _presentationProjection{};
        PollOutputAdapter _pollOutputAdapter{};
    };
}
