#pragma once

#include "input/InputContext.h"
#include "input/backend/IPollCommitEmitter.h"
#include "input/backend/NativeControlCode.h"

#include <array>
#include <cstdint>

namespace dualpad::input::backend
{
    enum class PollCommitMode : std::uint8_t
    {
        None = 0,
        Pulse,
        Hold,
        Repeat
    };

    enum class PollCommitRequestKind : std::uint8_t
    {
        None = 0,
        Pulse,
        HoldSet,
        HoldClear,
        RepeatSet,
        RepeatClear,
        ForceCancel
    };

    enum class ExecState : std::uint8_t
    {
        Idle = 0,
        WaitingForGate,
        PulseDownVisible,
        HoldDownVisible,
        ReleaseGap,
        Cooldown
    };

    enum class PendingKind : std::uint8_t
    {
        None = 0,
        Pulse,
        HoldStart,
        RepeatStart,
        HoldEnd,
        ToggleFlip,
        ForceCancel
    };

    inline constexpr std::string_view ToString(PollCommitMode mode)
    {
        switch (mode) {
        case PollCommitMode::Pulse:
            return "Pulse";
        case PollCommitMode::Hold:
            return "Hold";
        case PollCommitMode::Repeat:
            return "Repeat";
        case PollCommitMode::None:
        default:
            return "None";
        }
    }

    inline constexpr std::string_view ToString(PollCommitRequestKind kind)
    {
        switch (kind) {
        case PollCommitRequestKind::Pulse:
            return "Pulse";
        case PollCommitRequestKind::HoldSet:
            return "HoldSet";
        case PollCommitRequestKind::HoldClear:
            return "HoldClear";
        case PollCommitRequestKind::RepeatSet:
            return "RepeatSet";
        case PollCommitRequestKind::RepeatClear:
            return "RepeatClear";
        case PollCommitRequestKind::ForceCancel:
            return "ForceCancel";
        case PollCommitRequestKind::None:
        default:
            return "None";
        }
    }

    inline constexpr std::string_view ToString(ExecState state)
    {
        switch (state) {
        case ExecState::WaitingForGate:
            return "WaitingForGate";
        case ExecState::PulseDownVisible:
            return "PulseDownVisible";
        case ExecState::HoldDownVisible:
            return "HoldDownVisible";
        case ExecState::ReleaseGap:
            return "ReleaseGap";
        case ExecState::Cooldown:
            return "Cooldown";
        case ExecState::Idle:
        default:
            return "Idle";
        }
    }

    inline constexpr std::string_view ToString(PendingKind kind)
    {
        switch (kind) {
        case PendingKind::Pulse:
            return "Pulse";
        case PendingKind::HoldStart:
            return "HoldStart";
        case PendingKind::RepeatStart:
            return "RepeatStart";
        case PendingKind::HoldEnd:
            return "HoldEnd";
        case PendingKind::ToggleFlip:
            return "ToggleFlip";
        case PendingKind::ForceCancel:
            return "ForceCancel";
        case PendingKind::None:
        default:
            return "None";
        }
    }

    struct PollCommitRequest
    {
        RE::BSFixedString actionId{};
        InputContext context{ InputContext::Gameplay };
        NativeControlCode outputCode{ NativeControlCode::None };
        PollCommitMode mode{ PollCommitMode::None };
        PollCommitRequestKind kind{ PollCommitRequestKind::None };
        bool gateAware{ false };
        std::uint32_t epoch{ 0 };
        std::uint64_t timestampUs{ 0 };
        std::uint32_t minDownMs{ 0 };
        std::uint32_t repeatDelayMs{ 0 };
        std::uint32_t repeatIntervalMs{ 0 };
    };

    struct InFlightToken
    {
        bool active{ false };
        std::uint32_t tokenId{ 0 };
        std::uint32_t epoch{ 0 };
        std::uint64_t downAtUs{ 0 };
        std::uint64_t earliestReleaseAtUs{ 0 };
        bool downSubmitted{ false };
        bool releaseSubmitted{ false };
    };

    struct PendingIntent
    {
        PendingKind kind{ PendingKind::None };
        std::uint32_t epoch{ 0 };
        std::uint64_t queuedAtUs{ 0 };
        bool pendingNextPulse{ false };
        std::uint32_t suppressedPulseCount{ 0 };
    };

    struct PollCommitSlot
    {
        RE::BSFixedString actionId{};
        InputContext context{ InputContext::Gameplay };
        NativeControlCode outputCode{ NativeControlCode::None };
        PollCommitMode mode{ PollCommitMode::None };
        ExecState state{ ExecState::Idle };
        bool gateAware{ false };
        std::uint32_t epoch{ 0 };
        std::uint32_t minDownMs{ 0 };
        std::uint32_t repeatDelayMs{ 0 };
        std::uint32_t repeatIntervalMs{ 0 };
        InFlightToken token{};
        PendingIntent pending{};
        bool desiredHeld{ false };
        bool toggledOn{ false };
        std::uint64_t lastTransitionUs{ 0 };
        std::uint64_t nextRepeatAtUs{ 0 };
        std::uint32_t emittedDownCount{ 0 };
        std::uint32_t emittedUpCount{ 0 };
        std::uint32_t coalescedPulseCount{ 0 };
        std::uint32_t droppedPulseCount{ 0 };
        std::uint32_t cancelledCount{ 0 };
    };

    class PollCommitCoordinator
    {
    public:
        static constexpr std::size_t kMaxSlots = 64;

        void Reset();

        void BeginFrame(
            InputContext context,
            std::uint32_t contextEpoch,
            std::uint64_t nowUs);

        bool QueueRequest(const PollCommitRequest& request);

        void Tick(
            std::uint64_t nowUs,
            bool gameplayGateOpen);

        void Flush(IPollCommitEmitter& emitter, std::uint64_t nowUs);

        void DumpState() const;

        [[nodiscard]] const std::array<PollCommitSlot, kMaxSlots>& Slots() const;

    private:
        std::array<PollCommitSlot, kMaxSlots> _slots{};
        InputContext _currentContext{ InputContext::Gameplay };
        std::uint32_t _currentEpoch{ 0 };
        std::uint64_t _nowUs{ 0 };
        std::uint32_t _nextTokenId{ 1 };
        bool _lastGameplayGateOpen{ true };

        PollCommitSlot* FindOrCreateSlot(RE::BSFixedString actionId);
        PollCommitSlot* FindSlot(RE::BSFixedString actionId);

        void QueuePulse(PollCommitSlot& slot, const PollCommitRequest& request);
        void QueueHoldSet(PollCommitSlot& slot, const PollCommitRequest& request);
        void QueueHoldClear(PollCommitSlot& slot);
        void QueueRepeatSet(PollCommitSlot& slot, const PollCommitRequest& request);
        void QueueRepeatClear(PollCommitSlot& slot);
        void QueueForceCancel(PollCommitSlot& slot);

        void TickSlot(PollCommitSlot& slot, std::uint64_t nowUs, bool gateOpen);
        void TickPulseSlot(PollCommitSlot& slot, std::uint64_t nowUs, bool gateOpen);
        void TickHoldSlot(PollCommitSlot& slot, std::uint64_t nowUs, bool gateOpen);
        void TickRepeatSlot(PollCommitSlot& slot, std::uint64_t nowUs, bool gateOpen);

        void InvalidateStaleState(PollCommitSlot& slot);
        bool CanStartNewTransaction(const PollCommitSlot& slot) const;

        void StartPulseTransaction(PollCommitSlot& slot, std::uint64_t nowUs);
        void StartHoldTransaction(PollCommitSlot& slot, std::uint64_t nowUs);
        void StartRepeatTransaction(PollCommitSlot& slot, std::uint64_t nowUs);
        void CompleteRelease(PollCommitSlot& slot, std::uint64_t nowUs);
        void ClearToken(PollCommitSlot& slot);
        void TransitionState(PollCommitSlot& slot, ExecState newState, std::uint64_t nowUs);
        bool ShouldOpenGateForSlot(const PollCommitSlot& slot, bool gameplayGateOpen) const;
        bool HasManagedState(const PollCommitSlot& slot) const;
    };
}
