#include "input_v2/presentation/GameplayPresentationAdapter.h"

namespace dualpad::input_v2::presentation
{
    PublishedGameplayPresentation GameplayPresentationAdapter::PublishForTests(
        const GameplayPresentationAdapterInput& input)
    {
        return Publish(input, false);
    }

    PublishedGameplayPresentation GameplayPresentationAdapter::PublishCleanBaselineForTests(std::uint64_t tick)
    {
        return Publish(
            GameplayPresentationAdapterInput{
                .engineOwner = _published.engineOwner,
                .menuEntryOwner = _published.menuEntryOwner,
                .reason = GameplayPresentationReasonCode::ExplicitResync,
                .publishedTick = tick
            },
            true);
    }

    const PublishedGameplayPresentation& GameplayPresentationAdapter::GetPublished() const
    {
        return _published;
    }

    void GameplayPresentationAdapter::ResetForTests()
    {
        _published = {};
    }

    PublishedGameplayPresentation GameplayPresentationAdapter::Publish(
        const GameplayPresentationAdapterInput& input,
        bool forceRevision)
    {
        const bool changed =
            forceRevision ||
            input.engineOwner != _published.engineOwner ||
            input.menuEntryOwner != _published.menuEntryOwner ||
            input.reason != _published.reason;

        _published.engineOwner = input.engineOwner;
        _published.menuEntryOwner = input.menuEntryOwner;
        _published.reason = input.reason;
        _published.publishedTick = input.publishedTick;
        if (changed) {
            ++_published.gameplayPresentationRevision;
        }
        return _published;
    }
}
