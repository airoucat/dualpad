#pragma once

#include "input/InputContext.h"
#include "input_v2/gameplay/GameplayPresentationPublisher.h"
#include "input_v2/gameplay/GameplayProjectionFrame.h"
#include "input_v2/gameplay/PollOutputAdapter.h"

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
    };

    class DualPadRuntime
    {
    public:
        static DualPadRuntime& GetSingleton();
        static bool LiveCoordinatorPresentationAuthorityReachable();

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
        DualPadRuntimeResult ProcessGameplayFrameWithExecutor(
            const DualPadRuntimeInput& input,
            IPollOutputExecutor& executor);

        GameplayProjectionFrame _lastProjectionFrame{};
        GameplayPresentationPublisher _presentationPublisher{};
        PollOutputAdapter _pollOutputAdapter{};
    };
}
