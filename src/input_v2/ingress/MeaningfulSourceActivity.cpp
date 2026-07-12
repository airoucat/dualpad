#include "pch.h"

#include "input_v2/ingress/MeaningfulSourceActivity.h"

#include "input_v2/actions/InteractionEngine.h"
#include "input_v2/context/ContextResolver.h"

#include <algorithm>
#include <cstdlib>

namespace dualpad::input_v2::ingress
{
    namespace
    {
        constexpr std::int32_t kPointerPromotionDistancePx = 10;
        constexpr std::uint64_t kPointerPromotionDelayMs = 120;

        bool IsMouseDelta(const MeaningfulSourceActivity& activity)
        {
            return activity.source == PhysicalInputSource::Mouse &&
                activity.kind == SourceActivityKind::MouseDelta;
        }

        bool IsMouseStrong(const MeaningfulSourceActivity& activity)
        {
            return activity.source == PhysicalInputSource::Mouse &&
                (activity.kind == SourceActivityKind::MouseButtonPress ||
                    activity.kind == SourceActivityKind::MouseWheel);
        }

        bool IsKeyboardStrong(const MeaningfulSourceActivity& activity)
        {
            return activity.source == PhysicalInputSource::Keyboard &&
                (activity.kind == SourceActivityKind::KeyboardPress ||
                    activity.kind == SourceActivityKind::KeyboardCharacter);
        }

        bool IsGamepadStrong(const MeaningfulSourceActivity& activity)
        {
            if (activity.source != PhysicalInputSource::Gamepad) {
                return false;
            }
            switch (activity.kind) {
            case SourceActivityKind::GamepadButtonPress:
            case SourceActivityKind::GamepadAnalogEnter:
            case SourceActivityKind::GamepadAnalogChange:
            case SourceActivityKind::GamepadTouchPress:
                return true;
            default:
                return false;
            }
        }

        RoutedSourceActivity RouteStrongActivity(
            const MeaningfulSourceActivity& activity,
            const context::ResolvedContextSnapshot& context)
        {
            const bool menu = context.hostMode == context::HostMode::Menu;
            const bool mouse = activity.source == PhysicalInputSource::Mouse;
            const bool gamepad = activity.source == PhysicalInputSource::Gamepad;
            return RoutedSourceActivity{
                .activity = activity,
                .domain = menu ?
                    (mouse ? SourceActivityDomain::MenuPointer : SourceActivityDomain::MenuNavigation) :
                    SourceActivityDomain::Gameplay,
                .qualifiesForPrompt = true,
                .strongForMenuOwner = menu,
                .qualifiesForCursor = menu && (mouse || gamepad)
            };
        }
    }

    RoutedSourceActivityFrame RouteSourceActivities(
        std::span<const MeaningfulSourceActivity> orderedActivities,
        const SourceActivityRoutingState& previous,
        const context::ResolvedContextSnapshot& context,
        const actions::ResolvedActionFrame& resolvedActions,
        std::uint64_t ownerNowMs)
    {
        (void)resolvedActions;
        RoutedSourceActivityFrame routed{ .next = previous };

        std::uint64_t newestCompetingStrongSeq = previous.lastCompetingStrongSeq;
        for (const auto& activity : orderedActivities) {
            if (IsKeyboardStrong(activity) || IsMouseStrong(activity) || IsGamepadStrong(activity)) {
                newestCompetingStrongSeq = std::max(newestCompetingStrongSeq, activity.ingressSeq);
            }
        }

        for (const auto& activity : orderedActivities) {
            if (IsMouseDelta(activity)) {
                if (activity.ingressSeq <= newestCompetingStrongSeq) {
                    continue;
                }
                if (!routed.next.pendingPointerActivity ||
                    routed.next.mouseWindowStartOwnerMs == 0) {
                    routed.next.mouseAccumulatedManhattan = 0;
                    routed.next.mouseWindowStartOwnerMs = ownerNowMs;
                    routed.next.pointerPromotionEmitted = false;
                }
                routed.next.mouseAccumulatedManhattan +=
                    std::abs(activity.deltaX) + std::abs(activity.deltaY);
                routed.next.pendingPointerActivity = activity;
                continue;
            }

            if (IsKeyboardStrong(activity) || IsMouseStrong(activity) || IsGamepadStrong(activity)) {
                routed.activities.push_back(RouteStrongActivity(activity, context));
            }
        }

        routed.next.lastCompetingStrongSeq = newestCompetingStrongSeq;
        if (routed.next.pendingPointerActivity &&
            newestCompetingStrongSeq > routed.next.pendingPointerActivity->ingressSeq) {
            routed.next.pendingPointerActivity.reset();
            routed.next.mouseAccumulatedManhattan = 0;
            routed.next.mouseWindowStartOwnerMs = 0;
            routed.next.pointerPromotionEmitted = false;
        }

        if (routed.next.pendingPointerActivity &&
            !routed.next.pointerPromotionEmitted &&
            routed.next.mouseAccumulatedManhattan >= kPointerPromotionDistancePx &&
            ownerNowMs >= routed.next.mouseWindowStartOwnerMs + kPointerPromotionDelayMs &&
            routed.next.pendingPointerActivity->ingressSeq > routed.next.lastCompetingStrongSeq) {
            auto promotion = RouteStrongActivity(*routed.next.pendingPointerActivity, context);
            promotion.strongForMenuOwner = false;
            promotion.qualifiesForCursor = context.hostMode == context::HostMode::Menu;
            promotion.qualifiedByOwnerTimer = true;
            routed.activities.push_back(std::move(promotion));
            routed.next.lastAcceptedPointerSeq =
                routed.next.pendingPointerActivity->ingressSeq;
            routed.next.pointerPromotionEmitted = true;
            routed.next.pendingPointerActivity.reset();
            routed.next.mouseAccumulatedManhattan = 0;
            routed.next.mouseWindowStartOwnerMs = 0;
        }

        return routed;
    }
}
