#pragma once

#include "input/mapping/PadEvent.h"
#include "input/state/PadState.h"

#include <array>
#include <cstdint>

namespace dualpad::input
{
    struct TapHoldConfig
    {
        std::uint64_t tapThresholdUs{ 220000 };
        std::uint64_t holdThresholdUs{ 350000 };
    };

    class TapHoldEvaluator
    {
    public:
        void SetConfig(const TapHoldConfig& config);
        void Reset();
        void Evaluate(const PadState& previous, const PadState& current, PadEventBuffer& outEvents);

    private:
        TapHoldConfig _config{};
        std::array<std::uint64_t, 32> _pressStartUs{};
        std::array<bool, 32> _holdEmitted{};
    };
}
