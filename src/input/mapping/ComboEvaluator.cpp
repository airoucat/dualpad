#include "pch.h"
#include "input/mapping/ComboEvaluator.h"

#include "input/BindingManager.h"
#include "input/InputContext.h"

namespace dualpad::input
{
    namespace
    {
        constexpr std::uint32_t BitFromIndex(std::size_t index)
        {
            return (1u << index);
        }
    }

    void ComboEvaluator::Reset()
    {
        _pendingPresses.fill({});
        _latchedPairs.clear();
    }

    std::size_t ComboEvaluator::ButtonIndex(std::uint32_t bit)
    {
        return static_cast<std::size_t>(std::countr_zero(bit));
    }

    std::uint64_t ComboEvaluator::MakePairKey(std::uint32_t firstButton, std::uint32_t secondButton)
    {
        if (firstButton > secondButton) {
            std::swap(firstButton, secondButton);
        }

        return (static_cast<std::uint64_t>(firstButton) << 32) | secondButton;
    }

    bool ComboEvaluator::IsPairLatched(std::uint32_t firstButton, std::uint32_t secondButton) const
    {
        const auto pairKey = MakePairKey(firstButton, secondButton);
        return std::find(_latchedPairs.begin(), _latchedPairs.end(), pairKey) != _latchedPairs.end();
    }

    void ComboEvaluator::LatchPair(std::uint32_t firstButton, std::uint32_t secondButton)
    {
        const auto pairKey = MakePairKey(firstButton, secondButton);
        if (!IsPairLatched(firstButton, secondButton)) {
            _latchedPairs.push_back(pairKey);
        }
    }

    void ComboEvaluator::PurgeReleasedPairs(std::uint32_t currentMask)
    {
        _latchedPairs.erase(
            std::remove_if(
                _latchedPairs.begin(),
                _latchedPairs.end(),
                [&](std::uint64_t pairKey) {
                    const auto firstButton = static_cast<std::uint32_t>(pairKey >> 32);
                    const auto secondButton = static_cast<std::uint32_t>(pairKey & 0xFFFFFFFFu);
                    return (currentMask & firstButton) == 0 && (currentMask & secondButton) == 0;
                }),
            _latchedPairs.end());
    }

    void ComboEvaluator::EmitButtonPress(
        PadEventBuffer& outEvents,
        std::uint32_t bit,
        std::uint64_t timestampUs,
        std::uint32_t modifierMask) const
    {
        PadEvent event{};
        event.type = PadEventType::ButtonPress;
        event.triggerType = TriggerType::Button;
        event.code = bit;
        event.timestampUs = timestampUs;
        event.modifierMask = modifierMask;
        outEvents.Push(event);
    }

    void ComboEvaluator::EmitButtonRelease(
        PadEventBuffer& outEvents,
        std::uint32_t bit,
        std::uint64_t timestampUs,
        std::uint32_t modifierMask) const
    {
        PadEvent event{};
        event.type = PadEventType::ButtonRelease;
        event.triggerType = TriggerType::Button;
        event.code = bit;
        event.timestampUs = timestampUs;
        event.modifierMask = modifierMask;
        outEvents.Push(event);
    }

    void ComboEvaluator::EmitCombo(
        PadEventBuffer& outEvents,
        std::uint32_t firstButton,
        std::uint32_t secondButton,
        std::uint64_t timestampUs) const
    {
        if (firstButton > secondButton) {
            std::swap(firstButton, secondButton);
        }

        PadEvent event{};
        event.type = PadEventType::Combo;
        event.triggerType = TriggerType::Combo;
        event.code = secondButton;
        event.timestampUs = timestampUs;
        event.modifierMask = firstButton;
        outEvents.Push(event);
    }

    void ComboEvaluator::Evaluate(
        const PadState& previous,
        const PadState& current,
        InputContext context,
        PadEventBuffer& outEvents)
    {
        const auto& bindingManager = BindingManager::GetSingleton();
        const auto comboParticipantMask = bindingManager.GetComboParticipantMask(context);

        const auto previousMask = previous.buttons.digitalMask;
        const auto currentMask = current.buttons.digitalMask;
        const auto pressedMask = currentMask & ~previousMask;
        const auto releasedMask = previousMask & ~currentMask;
        const auto participantPressedMask = pressedMask & comboParticipantMask;

        for (int bitIndex = 0; bitIndex < 32; ++bitIndex) {
            const auto bit = BitFromIndex(static_cast<std::size_t>(bitIndex));
            if ((pressedMask & bit) == 0) {
                continue;
            }

            if ((comboParticipantMask & bit) == 0) {
                EmitButtonPress(outEvents, bit, current.timestampUs, currentMask & ~bit);
                continue;
            }

            auto& pending = _pendingPresses[bitIndex];
            pending.active = true;
            pending.dispatched = false;
            pending.consumedByCombo = false;
            pending.pressedAtUs = current.timestampUs;
            pending.modifierMaskAtPress = currentMask & ~bit;
        }

        for (int bitIndex = 0; bitIndex < 32; ++bitIndex) {
            const auto bit = BitFromIndex(static_cast<std::size_t>(bitIndex));
            if ((participantPressedMask & bit) == 0) {
                continue;
            }

            const auto& pending = _pendingPresses[bitIndex];
            if (!pending.active || pending.dispatched || pending.consumedByCombo) {
                continue;
            }

            std::uint32_t bestPartner = 0;
            std::uint64_t bestPartnerPressedAtUs = 0;
            const auto candidatePartners = (currentMask & comboParticipantMask) & ~bit;

            for (int partnerIndex = 0; partnerIndex < 32; ++partnerIndex) {
                const auto partnerBit = BitFromIndex(static_cast<std::size_t>(partnerIndex));
                if ((candidatePartners & partnerBit) == 0) {
                    continue;
                }

                if (!bindingManager.HasConfiguredComboPair(context, bit, partnerBit)) {
                    continue;
                }

                if (IsPairLatched(bit, partnerBit)) {
                    continue;
                }

                const auto& partnerPending = _pendingPresses[partnerIndex];
                if (!partnerPending.active || partnerPending.dispatched || partnerPending.consumedByCombo) {
                    continue;
                }

                const auto newerPress = std::max(pending.pressedAtUs, partnerPending.pressedAtUs);
                const auto olderPress = std::min(pending.pressedAtUs, partnerPending.pressedAtUs);
                if (newerPress < olderPress || (newerPress - olderPress) > kComboWindowUs) {
                    continue;
                }

                if (partnerPending.pressedAtUs >= bestPartnerPressedAtUs) {
                    bestPartner = partnerBit;
                    bestPartnerPressedAtUs = partnerPending.pressedAtUs;
                }
            }

            if (bestPartner == 0) {
                continue;
            }

            EmitCombo(outEvents, bit, bestPartner, current.timestampUs);
            _pendingPresses[bitIndex].consumedByCombo = true;
            _pendingPresses[ButtonIndex(bestPartner)].consumedByCombo = true;
            LatchPair(bit, bestPartner);
        }

        for (int bitIndex = 0; bitIndex < 32; ++bitIndex) {
            const auto bit = BitFromIndex(static_cast<std::size_t>(bitIndex));
            auto& pending = _pendingPresses[bitIndex];
            if (!pending.active || pending.dispatched || pending.consumedByCombo) {
                continue;
            }

            if ((currentMask & bit) == 0) {
                continue;
            }

            if (current.timestampUs < pending.pressedAtUs ||
                (current.timestampUs - pending.pressedAtUs) < kComboWindowUs) {
                continue;
            }

            EmitButtonPress(outEvents, bit, current.timestampUs, pending.modifierMaskAtPress);
            pending.dispatched = true;
        }

        for (int bitIndex = 0; bitIndex < 32; ++bitIndex) {
            const auto bit = BitFromIndex(static_cast<std::size_t>(bitIndex));
            if ((releasedMask & bit) == 0) {
                continue;
            }

            auto& pending = _pendingPresses[bitIndex];
            if (!pending.active) {
                EmitButtonRelease(outEvents, bit, current.timestampUs, previousMask & ~bit);
                continue;
            }

            if (!pending.dispatched && !pending.consumedByCombo) {
                EmitButtonPress(outEvents, bit, current.timestampUs, pending.modifierMaskAtPress);
                EmitButtonRelease(outEvents, bit, current.timestampUs, previousMask & ~bit);
            }
            else if (pending.dispatched) {
                EmitButtonRelease(outEvents, bit, current.timestampUs, previousMask & ~bit);
            }

            pending = {};
        }

        PurgeReleasedPairs(currentMask);
    }
}
