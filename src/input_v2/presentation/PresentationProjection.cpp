#include "input_v2/presentation/PresentationProjection.h"

namespace dualpad::input_v2::presentation
{
    namespace
    {
        CursorOwner CursorFromEvidence(const SourceEvidenceSnapshot& evidence, PresentationOwner owner)
        {
            if (evidence.pointerSignal == PointerSignal::PointerActive ||
                evidence.pointerSignal == PointerSignal::HoverOnly) {
                return CursorOwner::Gamepad;
            }
            return owner == PresentationOwner::Gamepad ? CursorOwner::Gamepad : CursorOwner::KeyboardMouse;
        }

        NavigationOwner NavigationFromOwner(PresentationOwner owner)
        {
            return owner == PresentationOwner::Gamepad ? NavigationOwner::Gamepad : NavigationOwner::KeyboardMouse;
        }

        PointerIntent PointerIntentFromSignal(PointerSignal signal)
        {
            switch (signal) {
            case PointerSignal::HoverOnly:
                return PointerIntent::HoverOnly;
            case PointerSignal::PointerActive:
                return PointerIntent::PointerActive;
            case PointerSignal::None:
            default:
                return PointerIntent::None;
            }
        }

        bool HasKeyboardMouseEvidence(const SourceEvidenceSnapshot& evidence)
        {
            return evidence.keyboardEvidence ||
                evidence.mouseButtonEvidence ||
                evidence.mouseMoveEvidence;
        }
    }

    PresentationDirtyFlags operator|(PresentationDirtyFlags lhs, PresentationDirtyFlags rhs)
    {
        return static_cast<PresentationDirtyFlags>(
            static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs));
    }

    PresentationDirtyFlags& operator|=(PresentationDirtyFlags& lhs, PresentationDirtyFlags rhs)
    {
        lhs = lhs | rhs;
        return lhs;
    }

    bool HasDirtyFlag(PresentationDirtyFlags flags, PresentationDirtyFlags flag)
    {
        return (static_cast<std::uint8_t>(flags) & static_cast<std::uint8_t>(flag)) != 0;
    }

    PublishedPresentationState PresentationProjection::Project(
        const SourceEvidenceSnapshot& evidence,
        const context::ResolvedContextSnapshot& contextSnapshot,
        const PublishedGameplayPresentation& gameplay)
    {
        PublishedPresentationState next = _published;
        next.family = evidence.deviceFamilyEvidence.family;
        next.deviceFamilyRevision = evidence.deviceFamilyEvidence.deviceFamilyRevision;
        next.uiContextId = contextSnapshot.uiContextId;
        next.actionSetStack = contextSnapshot.actionSetStack;
        next.presentationPolicyId = contextSnapshot.presentationPolicyId;
        next.contextRevision = contextSnapshot.contextRevision;
        next.gameplayPresentationRevision = gameplay.gameplayPresentationRevision;
        next.pointerIntent = PointerIntentFromSignal(evidence.pointerSignal);

        const bool enteringMenu =
            _published.uiContextId == context::UiContextId::None &&
            contextSnapshot.hostMode == context::HostMode::Menu;

        if (contextSnapshot.hostMode == context::HostMode::Gameplay) {
            next.owner = gameplay.engineOwner;
            next.reason = PresentationDecisionReason::GameplayEngineOwner;
        } else if (enteringMenu) {
            next.owner = gameplay.menuEntryOwner;
            next.reason = PresentationDecisionReason::GameplayMenuEntryOwner;
        } else if (evidence.gamepadEvidence || evidence.gamepadLease) {
            next.owner = PresentationOwner::Gamepad;
            next.reason = PresentationDecisionReason::MenuSourceEvidence;
        } else if (HasKeyboardMouseEvidence(evidence)) {
            next.owner = PresentationOwner::KeyboardMouse;
            next.reason = PresentationDecisionReason::MenuSourceEvidence;
        } else {
            next.owner = _published.owner;
            next.reason = PresentationDecisionReason::MenuSourceEvidence;
        }

        next.navigationOwner = NavigationFromOwner(next.owner);
        next.cursorOwner = CursorFromEvidence(evidence, next.owner);

        PresentationDirtyFlags dirty = PresentationDirtyFlags::None;
        if (next.family != _published.family || next.deviceFamilyRevision != _published.deviceFamilyRevision) {
            dirty |= PresentationDirtyFlags::Family;
        }
        if (next.owner != _published.owner || next.navigationOwner != _published.navigationOwner) {
            dirty |= PresentationDirtyFlags::Owner;
        }
        if (next.cursorOwner != _published.cursorOwner || next.pointerIntent != _published.pointerIntent) {
            dirty |= PresentationDirtyFlags::Cursor;
        }
        if (next.uiContextId != _published.uiContextId || next.contextRevision != _published.contextRevision) {
            dirty |= PresentationDirtyFlags::Context;
        }
        if (next.actionSetStack != _published.actionSetStack) {
            dirty |= PresentationDirtyFlags::ActionSets;
        }
        if (next.presentationPolicyId != _published.presentationPolicyId) {
            dirty |= PresentationDirtyFlags::Policy;
        }

        next.dirty = dirty;
        if (dirty != PresentationDirtyFlags::None) {
            next.epoch = _published.epoch + 1;
        }

        _published = next;
        return _published;
    }

    const PublishedPresentationState& PresentationProjection::GetPublished() const
    {
        return _published;
    }

    void PresentationProjection::ResetForTests()
    {
        _published = {};
    }
}
