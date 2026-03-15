#pragma once

#include "input/backend/ActionLifecycleBackend.h"
#include "input/backend/FrameActionPlan.h"
#include "input/backend/INativeDigitalEmitter.h"
#include "input/backend/NativeControlCode.h"
#include "input/backend/PollCommitCoordinator.h"

#include <cstdint>
#include <mutex>

namespace dualpad::input::backend
{
    struct PollCommittedButtonState
    {
        std::uint32_t semanticDownMask{ 0 };
        std::uint32_t managedMask{ 0 };
        std::uint64_t pollSequence{ 0 };
    };

    class ButtonEventBackend final :
        public IActionLifecycleBackend,
        public INativeDigitalEmitter
    {
    public:
        static ButtonEventBackend& GetSingleton();

        void Reset() override;
        bool IsRouteActive() const override;
        bool CanHandleAction(std::string_view actionId) const override;
        bool TriggerAction(
            std::string_view actionId,
            ActionOutputContract contract,
            InputContext context) override;
        bool SubmitActionState(
            std::string_view actionId,
            ActionOutputContract contract,
            bool pressed,
            float heldSeconds,
            InputContext context) override;

        void BeginFrame(
            InputContext context,
            std::uint32_t contextEpoch,
            std::uint64_t nowUs = 0);

        bool ApplyPlannedAction(const PlannedAction& action);
        [[nodiscard]] PollCommittedButtonState CommitPollState();

        EmitResult Emit(const EmitRequest& request) override;

    private:
        ButtonEventBackend() = default;

        bool SubmitSyntheticActionLocked(
            std::string_view actionId,
            ActionOutputContract contract,
            PlannedActionPhase phase,
            float heldSeconds,
            InputContext context);
        static bool TranslatePlannedActionToCommitRequest(
            const PlannedAction& action,
            PollCommitRequest& outRequest);

        static std::uint64_t NowUs();
        static std::uint32_t ToSemanticPadBit(NativeControlCode code);
        static bool ShouldLogPollCommit();
        static bool IsGameplayGateOpen(InputContext context);
        static bool SlotIsDown(const PollCommitSlot& slot);
        static bool SlotIsManaged(const PollCommitSlot& slot);

        PollCommitCoordinator _pollCommit{};
        InputContext _frameContext{ InputContext::Gameplay };
        std::uint32_t _frameContextEpoch{ 0 };
        std::uint64_t _pollSequence{ 0 };
        mutable std::mutex _lock;
    };
}
