#include "pch.h"
#include "input/backend/NativeDigitalLifecycleCoordinator.h"

#include <algorithm>
#include <bit>
#include <limits>

namespace dualpad::input::backend
{
    namespace
    {
        std::size_t ResolveSlotIndex(NativeControlCode control)
        {
            const auto mask = ToVirtualButtonMask(control);
            if (mask == 0 || !std::has_single_bit(mask)) {
                return std::numeric_limits<std::size_t>::max();
            }

            return static_cast<std::size_t>(std::countr_zero(mask));
        }

        void NotePressEdge(PollWindowButtonAggregate& aggregate, std::uint64_t timestampUs)
        {
            aggregate.sawPressEdge = true;
            if (aggregate.firstPressUs == 0 || (timestampUs != 0 && timestampUs < aggregate.firstPressUs)) {
                aggregate.firstPressUs = timestampUs;
            }
        }

        void NoteReleaseEdge(PollWindowButtonAggregate& aggregate, std::uint64_t timestampUs)
        {
            aggregate.sawReleaseEdge = true;
            if (timestampUs > aggregate.lastReleaseUs) {
                aggregate.lastReleaseUs = timestampUs;
            }
        }
    }

    void NativeDigitalLifecycleCoordinator::Reset()
    {
        _slots = {};
        _committedDownMask = 0;
        _allowance = {};
    }

    void NativeDigitalLifecycleCoordinator::SetPollAllowance(const PollInputAllowanceSnapshot& allowance)
    {
        _allowance = allowance;
    }

    void NativeDigitalLifecycleCoordinator::NoteButtonAction(
        NativeControlCode control,
        PlannedActionPhase phase,
        std::uint64_t timestampUs,
        const NativeButtonLifecycleHint& policy)
    {
        const auto index = ResolveSlotIndex(control);
        if (index >= _slots.size() || !IsDigitalNativeControl(control)) {
            return;
        }

        auto& slot = _slots[index];
        slot.control = control;
        if (policy.policy != ButtonCommitPolicy::None) {
            slot.policy = policy;
        }

        switch (phase) {
        case PlannedActionPhase::Press:
            NotePressEdge(slot.window, timestampUs);
            slot.window.finalDown = true;
            slot.physicalIntentDown = true;
            slot.intentDown = true;
            slot.pendingPress = true;
            slot.pendingRelease = false;
            slot.deferredPolls = 0;
            break;

        case PlannedActionPhase::Hold:
            slot.window.finalDown = true;
            slot.physicalIntentDown = true;
            slot.intentDown = true;
            slot.pendingPress = slot.pendingPress || !slot.committedDown;
            break;

        case PlannedActionPhase::Release:
            NoteReleaseEdge(slot.window, timestampUs);
            slot.window.finalDown = false;
            slot.physicalIntentDown = false;
            slot.pendingRelease = true;
            if (slot.policy.policy == ButtonCommitPolicy::HoldOwner || slot.committedDown) {
                slot.intentDown = false;
            }
            break;

        case PlannedActionPhase::Pulse:
            NotePressEdge(slot.window, timestampUs);
            NoteReleaseEdge(slot.window, timestampUs);
            slot.window.finalDown = false;
            slot.physicalIntentDown = false;
            slot.intentDown = true;
            slot.pendingPress = true;
            slot.pendingRelease = true;
            slot.deferredPolls = 0;
            break;

        case PlannedActionPhase::None:
        case PlannedActionPhase::Value:
        default:
            break;
        }
    }

    DigitalCommitFrame NativeDigitalLifecycleCoordinator::Commit(std::uint64_t pollTimestampUs)
    {
        DigitalCommitFrame frame{};
        frame.previousDownMask = _committedDownMask;
        frame.allowance = _allowance;

        for (const auto& slot : _slots) {
            if (slot.committedDown) {
                frame.previousDownMask |= ToVirtualButtonMask(slot.control);
            }
        }

        for (auto& slot : _slots) {
            if (slot.control == NativeControlCode::None) {
                continue;
            }

            const auto mask = ToVirtualButtonMask(slot.control);
            if (mask == 0) {
                continue;
            }

            const bool previousCommittedDown = slot.committedDown;
            bool nextCommittedDown = previousCommittedDown;
            const bool gateAllowed = _allowance.IsAllowed(slot.policy.gateClass);
            // For gated first-press commits we must preserve the edge until the
            // gate actually opens; consuming the press early while blocked turns
            // the later allowed Polls into a stale hold with no fresh up->down.
            const bool commitAllowed = gateAllowed;

            if (!previousCommittedDown) {
                if (commitAllowed && (slot.pendingPress || slot.intentDown)) {
                    nextCommittedDown = true;
                    slot.committedDown = true;
                    slot.committedPressedAtUs =
                        pollTimestampUs != 0 ? pollTimestampUs : slot.window.firstPressUs;
                    slot.earliestReleaseAtUs =
                        slot.policy.minDownUs != 0 && slot.committedPressedAtUs != 0 ?
                        (slot.committedPressedAtUs + slot.policy.minDownUs) :
                        0;
                    slot.visiblePollsRemaining =
                        std::max<std::uint8_t>(slot.policy.minVisiblePolls, 1);
                    slot.pendingPress = false;
                    slot.deferredPolls = 0;

                    if (slot.policy.policy != ButtonCommitPolicy::HoldOwner &&
                        !slot.physicalIntentDown) {
                        slot.intentDown = false;
                    }
                } else if ((slot.pendingPress || slot.intentDown) && !gateAllowed) {
                    if (slot.deferredPolls < std::numeric_limits<std::uint8_t>::max()) {
                        ++slot.deferredPolls;
                    }
                }
            } else if (slot.intentDown || slot.physicalIntentDown) {
                nextCommittedDown = true;
            } else {
                const bool releaseAllowed =
                    slot.pendingRelease &&
                    slot.visiblePollsRemaining == 0 &&
                    commitAllowed &&
                    (slot.earliestReleaseAtUs == 0 ||
                        pollTimestampUs == 0 ||
                        pollTimestampUs >= slot.earliestReleaseAtUs);

                if (releaseAllowed) {
                    nextCommittedDown = false;
                    slot.committedDown = false;
                    slot.pendingRelease = false;
                    slot.deferredPolls = 0;
                    slot.committedPressedAtUs = 0;
                    slot.earliestReleaseAtUs = 0;
                } else {
                    nextCommittedDown = true;
                }
            }

            if (nextCommittedDown) {
                frame.nextDownMask |= mask;
                slot.committedDown = true;
                if (slot.visiblePollsRemaining > 0) {
                    --slot.visiblePollsRemaining;
                }
            } else {
                const bool awaitingDeferredFirstCommit =
                    !previousCommittedDown &&
                    (slot.intentDown || slot.physicalIntentDown);
                slot.visiblePollsRemaining = 0;
                if (!awaitingDeferredFirstCommit) {
                    slot.pendingPress = false;
                    slot.deferredPolls = 0;
                }
                if (!slot.intentDown) {
                    slot.pendingRelease = false;
                }
            }

            const bool shouldLog =
                slot.window.sawPressEdge ||
                slot.window.sawReleaseEdge ||
                previousCommittedDown ||
                nextCommittedDown ||
                slot.pendingPress ||
                slot.pendingRelease;
            if (shouldLog && frame.logEntryCount < frame.logEntries.size()) {
                frame.logEntries[frame.logEntryCount++] = {
                    .control = slot.control,
                    .policy = slot.policy.policy,
                    .gateClass = slot.policy.gateClass,
                    .gateAllowed = gateAllowed,
                    .previousCommittedDown = previousCommittedDown,
                    .nextCommittedDown = nextCommittedDown,
                    .sawPressEdgeInWindow = slot.window.sawPressEdge,
                    .sawReleaseEdgeInWindow = slot.window.sawReleaseEdge,
                    .finalIntentDown = slot.intentDown,
                    .visiblePollsRemaining = slot.visiblePollsRemaining,
                    .deferredPolls = slot.deferredPolls,
                    .firstPressUs = slot.window.firstPressUs,
                    .lastReleaseUs = slot.window.lastReleaseUs
                };
            }

            slot.window = {};
        }

        _committedDownMask = frame.nextDownMask;
        return frame;
    }
}
