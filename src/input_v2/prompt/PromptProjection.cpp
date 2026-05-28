#include "pch.h"

#include "input_v2/prompt/PromptProjection.h"

namespace dualpad::input_v2::prompt
{
    PublishedPromptScope PromptProjection::BuildPromptScope(
        const presentation::PublishedPresentationState& presentation,
        std::uint64_t manifestEpoch)
    {
        PublishedPromptScope next{};
        next.manifestEpoch = manifestEpoch;
        next.promptScopeRevision = _published.promptScopeRevision;

        const bool presentationReady =
            manifestEpoch != 0 &&
            presentation.epoch != 0 &&
            !presentation.actionSetStack.scopeAnchorIds.empty();

        if (!presentationReady) {
            next.state = PromptScopeState::Unavailable;
            if (_published.state != PromptScopeState::Unavailable ||
                _published.manifestEpoch != manifestEpoch) {
                next.promptScopeRevision = _published.promptScopeRevision + 1;
            }
            _published = next;
            return _published;
        }

        next.state = PromptScopeState::Ready;
        next.family = presentation.family;
        next.uiContextId = presentation.uiContextId;
        next.actionSetStack = presentation.actionSetStack;

        const bool changed =
            _published.state != PromptScopeState::Ready ||
            _published.family != next.family ||
            _published.uiContextId != next.uiContextId ||
            _published.actionSetStack != next.actionSetStack ||
            _published.manifestEpoch != next.manifestEpoch;

        if (changed) {
            next.promptScopeRevision = _published.promptScopeRevision + 1;
        }

        _published = next;
        return _published;
    }

    const PublishedPromptScope& PromptProjection::GetPublishedPromptScope() const
    {
        return _published;
    }

    void PromptProjection::ResetForTests()
    {
        _published = {};
    }
}
