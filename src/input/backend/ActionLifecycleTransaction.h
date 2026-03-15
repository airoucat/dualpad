#pragma once

#include "input/InputContext.h"
#include "input/backend/ActionBackendPolicy.h"
#include "input/backend/FrameActionPlan.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace dualpad::input::backend
{
    struct LifecycleTransaction
    {
        std::string_view actionId{};
        ActionRoutingDecision routingDecision{};
        PlannedActionPhase phase{ PlannedActionPhase::None };
        std::uint32_t sourceCode{ 0 };
        std::uint64_t timestampUs{ 0 };
        float heldSeconds{ 0.0f };
        InputContext context{ InputContext::Gameplay };
        std::uint32_t contextEpoch{ 0 };
    };

    class LifecycleTransactionBuffer
    {
    public:
        static constexpr std::size_t kMaxTransactions = 32;

        void Clear()
        {
            _count = 0;
            _overflowed = false;
        }

        bool Push(const LifecycleTransaction& transaction)
        {
            if (_count >= _transactions.size()) {
                _overflowed = true;
                return false;
            }

            _transactions[_count++] = transaction;
            return true;
        }

        [[nodiscard]] bool Empty() const
        {
            return _count == 0;
        }

        [[nodiscard]] std::size_t Size() const
        {
            return _count;
        }

        [[nodiscard]] bool Overflowed() const
        {
            return _overflowed;
        }

        [[nodiscard]] const LifecycleTransaction* begin() const
        {
            return _transactions.data();
        }

        [[nodiscard]] const LifecycleTransaction* end() const
        {
            return _transactions.data() + _count;
        }

    private:
        std::array<LifecycleTransaction, kMaxTransactions> _transactions{};
        std::size_t _count{ 0 };
        bool _overflowed{ false };
    };
}
