#pragma once

#include "input/backend/FrameActionPlan.h"
#include "input/backend/NativeControlCode.h"
#include "input/backend/PollInputAllowance.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace dualpad::input::backend
{
    struct PollWindowButtonAggregate
    {
        bool finalDown{ false };
        bool sawPressEdge{ false };
        bool sawReleaseEdge{ false };
        std::uint64_t firstPressUs{ 0 };
        std::uint64_t lastReleaseUs{ 0 };
    };

    struct ButtonCommitSlot
    {
        NativeControlCode control{ NativeControlCode::None };
        NativeButtonLifecycleHint policy{};

        // Keep logical intent separate from committed game state so one
        // accepted press cannot collapse back to up before Skyrim sees it.
        bool intentDown{ false };
        bool physicalIntentDown{ false };
        bool committedDown{ false };
        bool pendingPress{ false };
        bool pendingRelease{ false };
        std::uint8_t visiblePollsRemaining{ 0 };
        std::uint8_t deferredPolls{ 0 };
        std::uint64_t committedPressedAtUs{ 0 };
        std::uint64_t earliestReleaseAtUs{ 0 };

        // Preserve the first edge across a whole Poll window instead of only
        // remembering the final state; otherwise short taps still disappear.
        PollWindowButtonAggregate window{};
    };

    struct NativePollCommitLogEntry
    {
        NativeControlCode control{ NativeControlCode::None };
        ButtonCommitPolicy policy{ ButtonCommitPolicy::None };
        ButtonCommitGateClass gateClass{ ButtonCommitGateClass::None };
        bool gateAllowed{ true };
        bool previousCommittedDown{ false };
        bool nextCommittedDown{ false };
        bool sawPressEdgeInWindow{ false };
        bool sawReleaseEdgeInWindow{ false };
        bool finalIntentDown{ false };
        std::uint8_t visiblePollsRemaining{ 0 };
        std::uint8_t deferredPolls{ 0 };
        std::uint64_t firstPressUs{ 0 };
        std::uint64_t lastReleaseUs{ 0 };
    };

    struct DigitalCommitFrame
    {
        std::uint32_t previousDownMask{ 0 };
        std::uint32_t nextDownMask{ 0 };
        PollInputAllowanceSnapshot allowance{};
        std::array<NativePollCommitLogEntry, 16> logEntries{};
        std::size_t logEntryCount{ 0 };
    };

    class NativeDigitalLifecycleCoordinator
    {
    public:
        void Reset();
        void SetPollAllowance(const PollInputAllowanceSnapshot& allowance);
        void NoteButtonAction(
            NativeControlCode control,
            PlannedActionPhase phase,
            std::uint64_t timestampUs,
            const NativeButtonLifecycleHint& policy);
        DigitalCommitFrame Commit(std::uint64_t pollTimestampUs);

        [[nodiscard]] std::uint32_t GetCommittedDownMask() const { return _committedDownMask; }
        [[nodiscard]] const std::array<ButtonCommitSlot, 16>& GetSlots() const { return _slots; }

    private:
        std::array<ButtonCommitSlot, 16> _slots{};
        std::uint32_t _committedDownMask{ 0 };
        PollInputAllowanceSnapshot _allowance{};
    };
}
