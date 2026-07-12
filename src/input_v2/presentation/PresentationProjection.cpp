#include "pch.h"

#include "input_v2/presentation/PresentationProjection.h"

#include "input_v2/actions/InteractionEngine.h"
#include "input_v2/presentation/CursorHandoffCoordinator.h"

#include <algorithm>
#include <vector>

namespace dualpad::input_v2::presentation
{
    namespace
    {
        NavigationOwner NavigationFromOwner(PresentationOwner owner)
        {
            return owner == PresentationOwner::Gamepad ?
                NavigationOwner::Gamepad : NavigationOwner::KeyboardMouse;
        }

        DeviceFamily FamilyFromActivity(const ingress::RoutedSourceActivity& routed)
        {
            return routed.activity.source == ingress::PhysicalInputSource::Gamepad ?
                DeviceFamily::Gamepad : DeviceFamily::KeyboardMouse;
        }

        PresentationOwner OwnerFromActivity(const ingress::RoutedSourceActivity& routed)
        {
            return routed.activity.source == ingress::PhysicalInputSource::Gamepad ?
                PresentationOwner::Gamepad : PresentationOwner::KeyboardMouse;
        }

        CursorOwner CursorFromActivity(const ingress::RoutedSourceActivity& routed)
        {
            return routed.activity.source == ingress::PhysicalInputSource::Gamepad ?
                CursorOwner::Gamepad : CursorOwner::KeyboardMouse;
        }

        const ingress::RoutedSourceActivity* NewestQualified(
            std::span<const ingress::RoutedSourceActivity> activities,
            bool ingress::RoutedSourceActivity::*qualifier)
        {
            const ingress::RoutedSourceActivity* newest = nullptr;
            for (const auto& activity : activities) {
                if (!(activity.*qualifier)) {
                    continue;
                }
                if (!newest || activity.activity.ingressSeq > newest->activity.ingressSeq) {
                    newest = &activity;
                }
            }
            return newest;
        }

        PublishedPresentationState NormalizePrevious(const PublishedPresentationState& previous)
        {
            auto normalized = previous;
            if (normalized.presentationEpoch == 0 && normalized.epoch != 0) {
                normalized.presentationEpoch = normalized.epoch;
                normalized.prompt.family = normalized.family;
                normalized.prompt.revision = normalized.deviceFamilyRevision;
                normalized.menu.owner = normalized.owner;
                normalized.menu.navigationOwner = normalized.navigationOwner;
                normalized.menu.contextRevision = normalized.contextRevision;
                normalized.menu.epoch = normalized.epoch;
                normalized.cursor.requestedOwner = normalized.cursorOwner;
                normalized.cursor.committedOwner = normalized.cursorOwner;
                normalized.cursor.contextRevision = normalized.contextRevision;
                normalized.cursor.epoch = normalized.epoch;
            }
            return normalized;
        }

        MenuRefreshEligibility DeriveMenuRefreshEligibility(
            const context::ResolvedContextSnapshot& contextSnapshot)
        {
            if (contextSnapshot.hostMode != context::HostMode::Menu) {
                return MenuRefreshEligibility::NotMenu;
            }
            switch (contextSnapshot.menuObserverCompleteness) {
            case menu::ObserverCompleteness::Unavailable:
                return MenuRefreshEligibility::ObserverUnavailable;
            case menu::ObserverCompleteness::Partial:
                return MenuRefreshEligibility::ObserverPartial;
            case menu::ObserverCompleteness::Complete:
                break;
            }
            if (contextSnapshot.identityQuality == menu::MenuIdentityQuality::DegradedIdentity) {
                return MenuRefreshEligibility::IdentityDegraded;
            }
            if (!contextSnapshot.topMenuInstanceId.has_value()) {
                return MenuRefreshEligibility::NoStableTarget;
            }
            return MenuRefreshEligibility::EligibleStableMenu;
        }

        CursorPositionSyncPolicy SyncPolicyFor(
            CursorOwner from,
            CursorOwner to,
            const PresentationProjectionInput& input)
        {
            if (from == CursorOwner::KeyboardMouse && to == CursorOwner::Gamepad) {
                return input.keyboardMouseToGamepadSync;
            }
            return input.gamepadToKeyboardMouseSync;
        }

        PointerIntent PointerIntentFor(
            const ingress::RoutedSourceActivity* cursorActivity,
            PointerIntent previous)
        {
            if (!cursorActivity) {
                return previous;
            }
            if (cursorActivity->activity.source == ingress::PhysicalInputSource::Gamepad) {
                return PointerIntent::None;
            }
            return cursorActivity->activity.kind == ingress::SourceActivityKind::MouseDelta ?
                PointerIntent::HoverOnly : PointerIntent::PointerActive;
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

    PresentationProjectionDecision ProjectPresentation(
        const PresentationProjectionInput& input) noexcept
    {
        const auto previous = NormalizePrevious(input.previous);
        PresentationProjectionDecision decision{ .state = previous };
        auto& next = decision.state;
        const bool recoveryFrozen = input.resetReasons != 0;

        const auto* promptActivity = recoveryFrozen ? nullptr :
            NewestQualified(input.routedActivities, &ingress::RoutedSourceActivity::qualifiesForPrompt);
        const auto* menuActivity = recoveryFrozen ? nullptr :
            NewestQualified(input.routedActivities, &ingress::RoutedSourceActivity::strongForMenuOwner);
        const auto* cursorActivity = recoveryFrozen ? nullptr :
            NewestQualified(input.routedActivities, &ingress::RoutedSourceActivity::qualifiesForCursor);

        if (recoveryFrozen) {
            next.prompt.reason = PromptFamilyDecisionReason::RecoveryFrozen;
            next.menu.reason = MenuOwnerDecisionReason::RecoveryFrozen;
            next.cursor.reason = CursorOwnerDecisionReason::RecoveryFrozen;
        } else if (promptActivity &&
            promptActivity->activity.ingressSeq > next.prompt.acceptedActivitySeq) {
            const auto family = FamilyFromActivity(*promptActivity);
            if (family != next.prompt.family) {
                ++next.prompt.revision;
            }
            next.prompt.family = family;
            next.prompt.acceptedActivitySeq = promptActivity->activity.ingressSeq;
            next.prompt.reason = family == DeviceFamily::Gamepad ?
                PromptFamilyDecisionReason::GamepadMeaningfulActivity :
                PromptFamilyDecisionReason::KeyboardMouseMeaningfulActivity;
        }

        const bool enteringMenu = previous.uiContextId == context::UiContextId::None &&
            input.context.hostMode == context::HostMode::Menu;
        if (!recoveryFrozen && menuActivity &&
            menuActivity->activity.ingressSeq > next.menu.acceptedActivitySeq) {
            next.menu.owner = OwnerFromActivity(*menuActivity);
            next.menu.navigationOwner = NavigationFromOwner(next.menu.owner);
            next.menu.acceptedActivitySeq = menuActivity->activity.ingressSeq;
            next.menu.reason = next.menu.owner == PresentationOwner::Gamepad ?
                MenuOwnerDecisionReason::GamepadStrongActivity :
                MenuOwnerDecisionReason::KeyboardMouseStrongActivity;
        } else if (!recoveryFrozen && enteringMenu) {
            next.menu.owner = input.gameplayMenuEntryOwner;
            next.menu.navigationOwner = NavigationFromOwner(next.menu.owner);
            next.menu.reason = MenuOwnerDecisionReason::GameplayMenuEntryOwner;
        }
        next.menu.contextRevision = input.context.contextRevision;

        if (previous.cursor.pendingToken != 0 && input.cursorAck) {
            const auto pendingPlan = BuildCursorHandoffPlan(
                previous.cursor.committedOwner,
                previous.cursor.requestedOwner,
                previous.cursor.pendingToken,
                previous.cursor.contextRevision,
                previous.cursor.epoch,
                input.context);
            if (CursorHandoffAckMatches(pendingPlan, *input.cursorAck)) {
                next.cursor.committedOwner = previous.cursor.requestedOwner;
                next.cursor.pendingToken = 0;
                next.cursor.positionSyncRequired = false;
                next.cursor.reason = CursorOwnerDecisionReason::HandoffCommitted;
            }
        }

        if (!recoveryFrozen && cursorActivity &&
            cursorActivity->activity.ingressSeq > next.cursor.acceptedActivitySeq) {
            const auto requested = CursorFromActivity(*cursorActivity);
            next.cursor.requestedOwner = requested;
            next.cursor.acceptedActivitySeq = cursorActivity->activity.ingressSeq;
            next.cursor.contextRevision = input.context.contextRevision;
            if (requested == next.cursor.committedOwner) {
                next.cursor.pendingToken = 0;
                next.cursor.positionSyncRequired = false;
                next.cursor.reason = cursorActivity->qualifiedByOwnerTimer ?
                    CursorOwnerDecisionReason::MousePointerThreshold :
                    CursorOwnerDecisionReason::CarryPrevious;
            } else {
                const auto policy = SyncPolicyFor(next.cursor.committedOwner, requested, input);
                if (policy == CursorPositionSyncPolicy::NotRequired) {
                    next.cursor.committedOwner = requested;
                    next.cursor.pendingToken = 0;
                    next.cursor.positionSyncRequired = false;
                    next.cursor.reason = cursorActivity->activity.source == ingress::PhysicalInputSource::Mouse ?
                        CursorOwnerDecisionReason::MousePointerThreshold :
                        CursorOwnerDecisionReason::MenuOwnerFallback;
                } else {
                    next.cursor.positionSyncRequired = true;
                    if (next.cursor.pendingToken == 0 || previous.cursor.requestedOwner != requested) {
                        next.cursor.pendingToken = input.ownerTickToken != 0 ?
                            input.ownerTickToken : input.inputStateEpoch;
                        next.cursor.epoch = previous.presentationEpoch + 1;
                    }
                    next.cursor.reason = CursorOwnerDecisionReason::HandoffPending;
                }
            }
        } else if (!recoveryFrozen && next.cursor.pendingToken != 0) {
            next.cursor.reason = CursorOwnerDecisionReason::HandoffPending;
        }

        const bool promptChanged = next.prompt != previous.prompt;
        const bool menuChanged = next.menu.owner != previous.menu.owner ||
            next.menu.navigationOwner != previous.menu.navigationOwner ||
            next.menu.acceptedActivitySeq != previous.menu.acceptedActivitySeq ||
            next.menu.contextRevision != previous.menu.contextRevision;
        const bool cursorChanged = next.cursor != previous.cursor;

        next.family = next.prompt.family;
        next.deviceFamilyRevision = next.prompt.revision;
        next.owner = next.menu.owner;
        next.navigationOwner = next.menu.navigationOwner;
        next.cursorOwner = next.cursor.committedOwner;
        next.pointerIntent = PointerIntentFor(cursorActivity, previous.pointerIntent);
        next.uiContextId = input.context.uiContextId;
        next.menuRefreshEligibility = DeriveMenuRefreshEligibility(input.context);
        next.targetMenuName = input.context.topMenuName;
        next.targetMenuInstanceId = input.context.topMenuInstanceId.value_or(0);
        next.targetMenuPtr = input.context.topMenuPtr;
        next.targetMenuMoviePtr = input.context.topMenuMoviePtr;
        next.menuStackRevision = input.context.menuStackRevision;
        next.actionSetStack = input.context.actionSetStack;
        next.presentationPolicyId = input.context.presentationPolicyId;
        next.contextRevision = input.context.contextRevision;
        next.reason = PresentationDecisionReason::MenuSourceEvidence;

        PresentationDirtyFlags dirty = PresentationDirtyFlags::None;
        if (promptChanged) {
            dirty |= PresentationDirtyFlags::Family;
        }
        if (menuChanged) {
            dirty |= PresentationDirtyFlags::Owner;
        }
        if (cursorChanged || next.pointerIntent != previous.pointerIntent) {
            dirty |= PresentationDirtyFlags::Cursor;
        }
        if (next.uiContextId != previous.uiContextId ||
            next.contextRevision != previous.contextRevision ||
            next.menuRefreshEligibility != previous.menuRefreshEligibility ||
            next.targetMenuName != previous.targetMenuName ||
            next.targetMenuInstanceId != previous.targetMenuInstanceId ||
            next.targetMenuPtr != previous.targetMenuPtr ||
            next.targetMenuMoviePtr != previous.targetMenuMoviePtr ||
            next.menuStackRevision != previous.menuStackRevision) {
            dirty |= PresentationDirtyFlags::Context;
        }
        if (next.actionSetStack != previous.actionSetStack) {
            dirty |= PresentationDirtyFlags::ActionSets;
        }
        if (next.presentationPolicyId != previous.presentationPolicyId) {
            dirty |= PresentationDirtyFlags::Policy;
        }
        next.dirty = dirty;
        if (dirty != PresentationDirtyFlags::None) {
            next.presentationEpoch = previous.presentationEpoch + 1;
        }
        next.epoch = next.presentationEpoch;
        if (menuChanged) {
            next.menu.epoch = next.presentationEpoch;
        }
        if (cursorChanged && next.cursor.epoch == 0) {
            next.cursor.epoch = next.presentationEpoch;
        }

        if (next.cursor.pendingToken != 0) {
            decision.cursorPlan = BuildCursorHandoffPlan(
                next.cursor.committedOwner,
                next.cursor.requestedOwner,
                next.cursor.pendingToken,
                next.cursor.contextRevision,
                next.cursor.epoch,
                input.context);
        }
        decision.refreshTargetMenu = menuChanged ||
            HasDirtyFlag(dirty, PresentationDirtyFlags::Context);
        return decision;
    }

    PublishedPresentationState PresentationProjection::Project(
        const SourceEvidenceSnapshot& evidence,
        const context::ResolvedContextSnapshot& contextSnapshot,
        const PublishedGameplayPresentation& gameplay,
        std::uint64_t ownerNowMs)
    {
        const actions::ResolvedActionFrame resolved{};
        std::vector<ingress::MeaningfulSourceActivity> activities;
        const bool evidenceChanged = !_lastCompatEvidence ||
            evidence.keyboardEvidence != _lastCompatEvidence->keyboardEvidence ||
            evidence.mouseButtonEvidence != _lastCompatEvidence->mouseButtonEvidence ||
            evidence.mouseMoveEvidence != _lastCompatEvidence->mouseMoveEvidence ||
            evidence.gamepadEvidence != _lastCompatEvidence->gamepadEvidence ||
            evidence.syntheticKeyboardWindow != _lastCompatEvidence->syntheticKeyboardWindow ||
            evidence.pointerSignal != _lastCompatEvidence->pointerSignal ||
            evidence.deviceFamilyEvidence != _lastCompatEvidence->deviceFamilyEvidence;
        const auto observedSeq = evidence.collectedTick != 0 ?
            evidence.collectedTick : evidence.deviceFamilyEvidence.deviceFamilyRevision;
        if (evidenceChanged) {
            _compatSyntheticSeq = std::max(_compatSyntheticSeq + 1, observedSeq);
        }
        const auto seq = _compatSyntheticSeq;
        if (evidenceChanged && evidence.keyboardEvidence) {
            activities.push_back(ingress::MeaningfulSourceActivity{
                { ingress::PhysicalInputSource::Keyboard, ingress::SourceActivityKind::KeyboardPress },
                seq, 0, 0, contextSnapshot.contextRevision });
        }
        if (evidenceChanged && evidence.mouseButtonEvidence) {
            activities.push_back(ingress::MeaningfulSourceActivity{
                { ingress::PhysicalInputSource::Mouse, ingress::SourceActivityKind::MouseButtonPress },
                seq, 0, 0, contextSnapshot.contextRevision });
        }
        if (evidenceChanged && evidence.mouseMoveEvidence) {
            activities.push_back(ingress::MeaningfulSourceActivity{
                { ingress::PhysicalInputSource::Mouse, ingress::SourceActivityKind::MouseDelta,
                    0, 10, 0, evidence.collectedTick * 1000 },
                seq, 0, 0, contextSnapshot.contextRevision });
        }
        if (evidenceChanged && evidence.gamepadEvidence) {
            activities.push_back(ingress::MeaningfulSourceActivity{
                { ingress::PhysicalInputSource::Gamepad, ingress::SourceActivityKind::GamepadButtonPress },
                seq, 0, 0, contextSnapshot.contextRevision });
        }
        _lastCompatEvidence = evidence;
        auto projected = ProjectOrdered(
            evidence,
            contextSnapshot,
            gameplay,
            activities,
            resolved,
            ownerNowMs != 0 ? ownerNowMs : evidence.collectedTick,
            0,
            evidence.collectedTick);

        if (_published.presentationEpoch == 1 &&
            projected.prompt.acceptedActivitySeq == 0 &&
            evidence.deviceFamilyEvidence.source != DeviceFamilyEvidenceSource::None) {
            projected.prompt.family = evidence.deviceFamilyEvidence.family;
            projected.prompt.revision = evidence.deviceFamilyEvidence.deviceFamilyRevision;
            projected.prompt.reason = PromptFamilyDecisionReason::ExplicitResync;
            projected.family = projected.prompt.family;
            projected.deviceFamilyRevision = projected.prompt.revision;
            projected.dirty |= PresentationDirtyFlags::Family;
            _published = projected;
        }
        return _published;
    }

    PublishedPresentationState PresentationProjection::ProjectOrdered(
        const SourceEvidenceSnapshot& evidence,
        const context::ResolvedContextSnapshot& contextSnapshot,
        const PublishedGameplayPresentation& gameplay,
        std::span<const ingress::MeaningfulSourceActivity> orderedActivities,
        const actions::ResolvedActionFrame& resolvedActions,
        std::uint64_t ownerNowMs,
        std::uint64_t inputStateEpoch,
        std::uint64_t ownerTickToken,
        std::optional<CursorHandoffAck> cursorAck)
    {
        const auto routed = ingress::RouteSourceActivities(
            orderedActivities,
            _published.activityRouting,
            contextSnapshot,
            resolvedActions,
            ownerNowMs);
        auto projected = ProjectPresentation(PresentationProjectionInput{
            .previous = _published,
            .context = contextSnapshot,
            .gameplayMenuEntryOwner = gameplay.menuEntryOwner,
            .routedActivities = routed.activities,
            .cursorAck = std::move(cursorAck),
            .inputStateEpoch = inputStateEpoch,
            .ownerTickToken = ownerTickToken
        });
        projected.state.activityRouting = routed.next;
        projected.state.gameplayPresentationRevision = gameplay.gameplayPresentationRevision;
        if (contextSnapshot.hostMode == context::HostMode::Gameplay) {
            projected.state.owner = gameplay.engineOwner;
            projected.state.reason = PresentationDecisionReason::GameplayEngineOwner;
        }
        if (evidence.pointerSignal == PointerSignal::PointerActive) {
            projected.state.pointerIntent = PointerIntent::PointerActive;
        } else if (evidence.pointerSignal == PointerSignal::HoverOnly) {
            projected.state.pointerIntent = PointerIntent::HoverOnly;
        }
        _pendingCursorPlan = projected.cursorPlan;
        _published = std::move(projected.state);
        return _published;
    }

    const PublishedPresentationState& PresentationProjection::GetPublished() const
    {
        return _published;
    }

    const std::optional<CursorHandoffPlan>& PresentationProjection::GetPendingCursorPlan() const
    {
        return _pendingCursorPlan;
    }

    void PresentationProjection::ResetForTests()
    {
        _published = {};
        _compatSyntheticSeq = 0;
        _lastCompatEvidence.reset();
        _pendingCursorPlan.reset();
    }
}
