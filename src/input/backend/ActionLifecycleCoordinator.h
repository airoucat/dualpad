#pragma once

#include "input/backend/ActionLifecycleTransaction.h"
#include "input/InputContext.h"
#include "input/backend/ActionBackendPolicy.h"
#include "input/backend/FrameActionPlan.h"
#include "input/injection/SyntheticPadFrame.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace dualpad::input::backend
{
    class ActionLifecycleCoordinator
    {
    public:
        void Reset();

        bool RegisterOwningAction(
            std::uint32_t sourceCode,
            std::string_view actionId,
            const ActionRoutingDecision& routingDecision);

        bool ReleaseOwningAction(
            std::uint32_t sourceCode,
            std::uint64_t timestampUs,
            InputContext context,
            std::uint32_t contextEpoch,
            FrameActionPlan& outPlan);

        // Materialize planner-owned lifecycle actions into this frame's plan.
        // The returned mask contains source bits whose ownership ended here and
        // should therefore be unblocked from legacy physical fallback.
        [[nodiscard]] std::uint32_t PlanFrame(
            const SyntheticPadFrame& frame,
            InputContext context,
            FrameActionPlan& outPlan);

    private:
        struct ActiveSourceAction
        {
            bool active{ false };
            std::string actionId{};
            ActionRoutingDecision routingDecision{};
        };

        static constexpr std::size_t kSourceSlotCount = 32;

        static std::size_t BitIndex(std::uint32_t sourceCode);
        static bool BuildLifecycleTransaction(
            const ActiveSourceAction& activeAction,
            const SyntheticButtonState& button,
            std::uint32_t sourceCode,
            std::uint64_t timestampUs,
            InputContext context,
            std::uint32_t contextEpoch,
            LifecycleTransactionBuffer& outTransactions);
        static PlannedAction BuildLifecycleAction(
            const LifecycleTransaction& transaction);

        std::array<ActiveSourceAction, kSourceSlotCount> _activeActions{};
    };
}
