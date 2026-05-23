#pragma once

#include "input/injection/GameplayOwnershipCoordinator.h"
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
        PublishedGameplayPresentation PublishFromCoordinator(
            const dualpad::input::GameplayOwnershipCoordinator& coordinator,
            std::uint64_t tick)
        {
            const auto state = coordinator.GetPublishedGameplayPresentationState();
            return Publish(
                GameplayPresentationAdapterInput{
                    .engineOwner = ConvertOwner(state.engineOwner),
                    .menuEntryOwner = ConvertOwner(state.menuEntryOwner),
                    .reason = GameplayPresentationReasonCode::CoordinatorPublished,
                    .publishedTick = tick
                },
                false);
        }
        PublishedGameplayPresentation PublishForTests(const GameplayPresentationAdapterInput& input);
        PublishedGameplayPresentation PublishCleanBaselineForTests(std::uint64_t tick);
        const PublishedGameplayPresentation& GetPublished() const;
        void ResetForTests();

    private:
        static PresentationOwner ConvertOwner(dualpad::input::GameplayOwnershipCoordinator::ChannelOwner owner)
        {
            return owner == dualpad::input::GameplayOwnershipCoordinator::ChannelOwner::Gamepad ?
                PresentationOwner::Gamepad :
                PresentationOwner::KeyboardMouse;
        }

        PublishedGameplayPresentation Publish(const GameplayPresentationAdapterInput& input, bool forceRevision);
        PublishedGameplayPresentation _published{};
    };
}
