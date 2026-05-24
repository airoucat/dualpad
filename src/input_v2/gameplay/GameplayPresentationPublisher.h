#pragma once

#include "input_v2/gameplay/GameplayProjectionFrame.h"
#include "input_v2/presentation/PresentationProjection.h"

namespace dualpad::input_v2::gameplay
{
    class GameplayPresentationPublisher
    {
    public:
        static GameplayPresentationPublisher& GetRuntimePublisher();

        presentation::PublishedGameplayPresentation PublishAfterOutputApply(
            const GameplayProjectionFrame& frame,
            std::uint64_t tick,
            bool outputApplySucceeded);

        const presentation::PublishedGameplayPresentation& GetPublished() const;
        void ResetForTests();

    private:
        presentation::PublishedGameplayPresentation _published{};
    };
}
