#pragma once

#include "input/backend/FrameActionPlan.h"
#include "input/backend/IPollCommitEmitter.h"
#include "input/backend/NativeControlCode.h"
#include "input/backend/PollCommitCoordinator.h"

#include <cstdint>
#include <mutex>

namespace dualpad::input::backend
{
    struct CommittedButtonState
    {
        std::uint32_t buttonDownMask{ 0 };
        std::uint32_t buttonPressedMask{ 0 };
        std::uint32_t buttonReleasedMask{ 0 };
        std::uint32_t managedMask{ 0 };
        InputContext context{ InputContext::Gameplay };
        std::uint32_t contextEpoch{ 0 };
        std::uint64_t pollSequence{ 0 };
    };

    class NativeButtonCommitBackend final : public IPollCommitEmitter
    {
    public:
        static NativeButtonCommitBackend& GetSingleton();

        void Reset();
        bool IsRouteActive() const;
        bool CanHandleAction(std::string_view actionId) const;

        void BeginFrame(
            InputContext context,
            std::uint32_t contextEpoch,
            std::uint64_t nowUs = 0);

        bool ApplyPlannedAction(const PlannedAction& action);
        [[nodiscard]] CommittedButtonState CommitPollState();

        EmitResult Emit(const EmitRequest& request) override;

    private:
        NativeButtonCommitBackend() = default;

        static bool TranslatePlannedActionToCommitRequest(
            const PlannedAction& action,
            PollCommitRequest& outRequest);

        static std::uint64_t NowUs();
        static std::uint32_t ToVirtualPadBit(NativeControlCode code);
        static bool ShouldLogPollCommit();
        static bool IsGameplayGateOpen(InputContext context);
        static bool SlotIsDown(const PollCommitSlot& slot);
        static bool SlotIsManaged(const PollCommitSlot& slot);

        PollCommitCoordinator _pollCommit{};
        InputContext _frameContext{ InputContext::Gameplay };
        std::uint32_t _frameContextEpoch{ 0 };
        std::uint64_t _pollSequence{ 0 };
        std::uint32_t _lastCommittedButtonDownMask{ 0 };
        mutable std::mutex _lock;
    };
}
