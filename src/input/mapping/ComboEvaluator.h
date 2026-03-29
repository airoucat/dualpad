#pragma once

#include <array>
#include <vector>

#include "input/InputContext.h"
#include "input/mapping/PadEvent.h"
#include "input/state/PadState.h"

namespace dualpad::input
{
    class ComboEvaluator
    {
    public:
        void Reset();
        void Evaluate(
            const PadState& previous,
            const PadState& current,
            InputContext context,
            PadEventBuffer& outEvents);

    private:
        struct PendingPress
        {
            bool active{ false };
            bool dispatched{ false };
            bool consumedByCombo{ false };
            std::uint64_t pressedAtUs{ 0 };
            std::uint32_t modifierMaskAtPress{ 0 };
        };

        static constexpr std::uint64_t kComboWindowUs = 22'000;

        std::array<PendingPress, 32> _pendingPresses{};
        std::vector<std::uint64_t> _latchedPairs{};

        static std::size_t ButtonIndex(std::uint32_t bit);
        static std::uint64_t MakePairKey(std::uint32_t firstButton, std::uint32_t secondButton);

        bool IsPairLatched(std::uint32_t firstButton, std::uint32_t secondButton) const;
        void LatchPair(std::uint32_t firstButton, std::uint32_t secondButton);
        void PurgeReleasedPairs(std::uint32_t currentMask);
        void EmitButtonPress(PadEventBuffer& outEvents, std::uint32_t bit, std::uint64_t timestampUs, std::uint32_t modifierMask) const;
        void EmitButtonRelease(PadEventBuffer& outEvents, std::uint32_t bit, std::uint64_t timestampUs, std::uint32_t modifierMask) const;
        void EmitCombo(PadEventBuffer& outEvents, std::uint32_t firstButton, std::uint32_t secondButton, std::uint64_t timestampUs) const;
    };
}
