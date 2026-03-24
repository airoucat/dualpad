#pragma once

#include "input/InputContext.h"

#include <atomic>
#include <cstdint>

namespace dualpad::input
{
    struct AuthoritativePollFrame
    {
        std::uint32_t downMask{ 0 };
        std::uint32_t pressedMask{ 0 };
        std::uint32_t releasedMask{ 0 };
        std::uint32_t pulseMask{ 0 };

        std::uint32_t unmanagedDownMask{ 0 };
        std::uint32_t unmanagedPressedMask{ 0 };
        std::uint32_t unmanagedReleasedMask{ 0 };
        std::uint32_t unmanagedPulseMask{ 0 };

        std::uint32_t managedMask{ 0 };
        std::uint32_t committedDownMask{ 0 };
        std::uint32_t committedPressedMask{ 0 };
        std::uint32_t committedReleasedMask{ 0 };

        InputContext context{ InputContext::Gameplay };
        std::uint32_t contextEpoch{ 0 };
        std::uint64_t sourceTimestampUs{ 0 };
        std::uint64_t pollSequence{ 0 };

        float moveX{ 0.0f };
        float moveY{ 0.0f };
        float lookX{ 0.0f };
        float lookY{ 0.0f };
        float leftTrigger{ 0.0f };
        float rightTrigger{ 0.0f };

        bool hasDigital{ false };
        bool hasAnalog{ false };
        bool overflowed{ false };
        bool coalesced{ false };
    };

    class AuthoritativePollState
    {
    public:
        static AuthoritativePollState& GetSingleton();

        void Reset();

        void SetUnmanagedButton(std::uint32_t bit, bool down);
        void PulseUnmanagedButton(std::uint32_t bit);
        void PublishCommittedButtons(
            std::uint32_t committedDownMask,
            std::uint32_t committedPressedMask,
            std::uint32_t committedReleasedMask,
            std::uint32_t managedMask,
            std::uint64_t pollSequence,
            InputContext context,
            std::uint32_t contextEpoch);
        void PublishUnmanagedDigitalEdges(
            std::uint32_t unmanagedPressedMask,
            std::uint32_t unmanagedReleasedMask,
            std::uint32_t unmanagedPulseMask);
        void PublishFrameMetadata(
            std::uint64_t sourceTimestampUs,
            bool overflowed,
            bool coalesced);
        void PublishAnalogState(
            float moveX,
            float moveY,
            float lookX,
            float lookY,
            float leftTrigger,
            float rightTrigger);

        [[nodiscard]] AuthoritativePollFrame ReadSnapshot();

    private:
        std::atomic<std::uint32_t> _unmanagedHeldDown{ 0 };
        std::atomic<std::uint32_t> _unmanagedPulseDown{ 0 };
        std::atomic<std::uint64_t> _unmanagedPulseExpireMs{ 0 };

        std::atomic<std::uint32_t> _committedDownMask{ 0 };
        std::atomic<std::uint32_t> _committedPressedMask{ 0 };
        std::atomic<std::uint32_t> _committedReleasedMask{ 0 };
        std::atomic<std::uint32_t> _managedMask{ 0 };
        std::atomic<std::uint32_t> _unmanagedPressedMask{ 0 };
        std::atomic<std::uint32_t> _unmanagedReleasedMask{ 0 };
        std::atomic<std::uint32_t> _unmanagedPulseMask{ 0 };
        std::atomic<std::uint32_t> _contextValue{ static_cast<std::uint32_t>(InputContext::Gameplay) };
        std::atomic<std::uint32_t> _contextEpoch{ 0 };
        std::atomic<std::uint64_t> _sourceTimestampUs{ 0 };
        std::atomic<std::uint64_t> _pollSequence{ 0 };

        std::atomic<float> _moveX{ 0.0f };
        std::atomic<float> _moveY{ 0.0f };
        std::atomic<float> _lookX{ 0.0f };
        std::atomic<float> _lookY{ 0.0f };
        std::atomic<float> _leftTrigger{ 0.0f };
        std::atomic<float> _rightTrigger{ 0.0f };
        std::atomic_bool _hasDigital{ false };
        std::atomic_bool _hasAnalog{ false };
        std::atomic_bool _overflowed{ false };
        std::atomic_bool _coalesced{ false };
    };
}
