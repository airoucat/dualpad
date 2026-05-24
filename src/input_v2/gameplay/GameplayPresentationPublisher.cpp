#include "pch.h"

#include "input_v2/gameplay/GameplayPresentationPublisher.h"

namespace dualpad::input_v2::gameplay
{
    GameplayPresentationPublisher& GameplayPresentationPublisher::GetRuntimePublisher()
    {
        static GameplayPresentationPublisher publisher;
        return publisher;
    }

    presentation::PublishedGameplayPresentation GameplayPresentationPublisher::PublishAfterOutputApply(
        const GameplayProjectionFrame& frame,
        std::uint64_t tick,
        bool outputApplySucceeded)
    {
        if (!outputApplySucceeded) {
            return _published;
        }

        const bool forceRecoveryRepublish =
            frame.recoveryPlan.mode == RecoveryMode::HardResetOutputs &&
            frame.recoveryPlan.commitCleanRecoveryBaselineAfterApply;
        const bool changed =
            _published.engineOwner != frame.presentationPlan.engineOwner ||
            _published.menuEntryOwner != frame.presentationPlan.menuEntryOwner ||
            _published.reason != frame.presentationPlan.reason ||
            forceRecoveryRepublish;

        if (changed) {
            ++_published.gameplayPresentationRevision;
        }

        _published.engineOwner = frame.presentationPlan.engineOwner;
        _published.menuEntryOwner = frame.presentationPlan.menuEntryOwner;
        _published.reason = frame.presentationPlan.reason;
        _published.publishedTick = tick;
        return _published;
    }

    const presentation::PublishedGameplayPresentation& GameplayPresentationPublisher::GetPublished() const
    {
        return _published;
    }

    void GameplayPresentationPublisher::ResetForTests()
    {
        _published = presentation::PublishedGameplayPresentation{};
    }
}
