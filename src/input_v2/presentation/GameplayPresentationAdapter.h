#pragma once

#include "input_v2/presentation/PresentationProjection.h"

#include <cstdint>

namespace dualpad::input_v2::presentation
{
    struct GameplayPresentationAdapterInput
    {
        PresentationOwner engineOwner{ PresentationOwner::KeyboardMouse };
        PresentationOwner menuEntryOwner{ PresentationOwner::KeyboardMouse };
        GameplayPresentationReasonCode reason{ GameplayPresentationReasonCode::CoordinatorPublished };
        std::uint64_t publishedTick{ 0 };
    };

    class GameplayPresentationAdapter
    {
    public:
        PublishedGameplayPresentation PublishForTests(const GameplayPresentationAdapterInput& input);
        PublishedGameplayPresentation PublishCleanBaselineForTests(std::uint64_t tick);
        const PublishedGameplayPresentation& GetPublished() const;
        void ResetForTests();

    private:
        PublishedGameplayPresentation Publish(const GameplayPresentationAdapterInput& input, bool forceRevision);
        PublishedGameplayPresentation _published{};
    };
}
