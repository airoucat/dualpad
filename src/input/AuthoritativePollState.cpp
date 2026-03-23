#include "pch.h"
#include "input/AuthoritativePollState.h"

#include "input/PadProfile.h"

#include <chrono>

namespace dualpad::input
{
    namespace
    {
        inline std::uint64_t NowMs()
        {
            using namespace std::chrono;
            return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
        }

        constexpr std::uint64_t kUnmanagedPulseWindowMs = 50;

        std::uint16_t ToXInputButtons(std::uint32_t mask)
        {
            std::uint16_t buttons = 0;
            const auto& bits = GetPadBits(GetActivePadProfile());

            if (mask & bits.cross) {
                buttons |= 0x1000;
            }
            if (mask & bits.circle) {
                buttons |= 0x2000;
            }
            if (mask & bits.square) {
                buttons |= 0x4000;
            }
            if (mask & bits.triangle) {
                buttons |= 0x8000;
            }
            if (mask & bits.l1) {
                buttons |= 0x0100;
            }
            if (mask & bits.r1) {
                buttons |= 0x0200;
            }
            if (mask & bits.l3) {
                buttons |= 0x0040;
            }
            if (mask & bits.r3) {
                buttons |= 0x0080;
            }
            if (mask & bits.options) {
                buttons |= 0x0010;
            }
            if (mask & bits.create) {
                buttons |= 0x0020;
            }
            if (mask & bits.dpadUp) {
                buttons |= 0x0001;
            }
            if (mask & bits.dpadDown) {
                buttons |= 0x0002;
            }
            if (mask & bits.dpadLeft) {
                buttons |= 0x0004;
            }
            if (mask & bits.dpadRight) {
                buttons |= 0x0008;
            }

            return buttons;
        }
    }

    AuthoritativePollState& AuthoritativePollState::GetSingleton()
    {
        static AuthoritativePollState instance;
        return instance;
    }

    void AuthoritativePollState::Reset()
    {
        _unmanagedHeldDown.store(0, std::memory_order_release);
        _unmanagedPulseDown.store(0, std::memory_order_release);
        _unmanagedPulseExpireMs.store(0, std::memory_order_release);

        _committedDownMask.store(0, std::memory_order_release);
        _committedPressedMask.store(0, std::memory_order_release);
        _committedReleasedMask.store(0, std::memory_order_release);
        _managedMask.store(0, std::memory_order_release);
        _unmanagedPressedMask.store(0, std::memory_order_release);
        _unmanagedReleasedMask.store(0, std::memory_order_release);
        _unmanagedPulseMask.store(0, std::memory_order_release);
        _contextValue.store(static_cast<std::uint32_t>(InputContext::Gameplay), std::memory_order_release);
        _contextEpoch.store(0, std::memory_order_release);
        _sourceTimestampUs.store(0, std::memory_order_release);
        _pollSequence.store(0, std::memory_order_release);

        _moveX.store(0.0f, std::memory_order_release);
        _moveY.store(0.0f, std::memory_order_release);
        _lookX.store(0.0f, std::memory_order_release);
        _lookY.store(0.0f, std::memory_order_release);
        _leftTrigger.store(0.0f, std::memory_order_release);
        _rightTrigger.store(0.0f, std::memory_order_release);
        _hasDigital.store(false, std::memory_order_release);
        _hasAnalog.store(false, std::memory_order_release);
        _overflowed.store(false, std::memory_order_release);
        _coalesced.store(false, std::memory_order_release);
    }

    void AuthoritativePollState::SetUnmanagedButton(std::uint32_t bit, bool down)
    {
        if (bit == 0) {
            return;
        }

        if (down) {
            _unmanagedHeldDown.fetch_or(bit, std::memory_order_acq_rel);
        } else {
            _unmanagedHeldDown.fetch_and(~bit, std::memory_order_acq_rel);
        }
    }

    void AuthoritativePollState::PulseUnmanagedButton(std::uint32_t bit)
    {
        if (bit == 0) {
            return;
        }

        _unmanagedPulseExpireMs.store(NowMs() + kUnmanagedPulseWindowMs, std::memory_order_release);
        _unmanagedPulseDown.fetch_or(bit, std::memory_order_acq_rel);
    }

    void AuthoritativePollState::PublishCommittedButtons(
        std::uint32_t committedDownMask,
        std::uint32_t committedPressedMask,
        std::uint32_t committedReleasedMask,
        std::uint32_t managedMask,
        std::uint64_t pollSequence,
        InputContext context,
        std::uint32_t contextEpoch)
    {
        _committedDownMask.store(committedDownMask, std::memory_order_release);
        _committedPressedMask.store(committedPressedMask, std::memory_order_release);
        _committedReleasedMask.store(committedReleasedMask, std::memory_order_release);
        _managedMask.store(managedMask, std::memory_order_release);
        _contextValue.store(static_cast<std::uint32_t>(context), std::memory_order_release);
        _contextEpoch.store(contextEpoch, std::memory_order_release);
        _pollSequence.store(pollSequence, std::memory_order_release);
        _hasDigital.store(true, std::memory_order_release);
    }

    void AuthoritativePollState::PublishUnmanagedDigitalEdges(
        std::uint32_t unmanagedPressedMask,
        std::uint32_t unmanagedReleasedMask,
        std::uint32_t unmanagedPulseMask)
    {
        _unmanagedPressedMask.store(unmanagedPressedMask, std::memory_order_release);
        _unmanagedReleasedMask.store(unmanagedReleasedMask, std::memory_order_release);
        _unmanagedPulseMask.store(unmanagedPulseMask, std::memory_order_release);
        _hasDigital.store(true, std::memory_order_release);
    }

    void AuthoritativePollState::PublishFrameMetadata(
        std::uint64_t sourceTimestampUs,
        bool overflowed,
        bool coalesced)
    {
        _sourceTimestampUs.store(sourceTimestampUs, std::memory_order_release);
        _overflowed.store(overflowed, std::memory_order_release);
        _coalesced.store(coalesced, std::memory_order_release);
    }

    void AuthoritativePollState::PublishAnalogState(
        float moveX,
        float moveY,
        float lookX,
        float lookY,
        float leftTrigger,
        float rightTrigger)
    {
        _moveX.store(moveX, std::memory_order_release);
        _moveY.store(moveY, std::memory_order_release);
        _lookX.store(lookX, std::memory_order_release);
        _lookY.store(lookY, std::memory_order_release);
        _leftTrigger.store(leftTrigger, std::memory_order_release);
        _rightTrigger.store(rightTrigger, std::memory_order_release);
        _hasAnalog.store(true, std::memory_order_release);
    }

    AuthoritativePollFrame AuthoritativePollState::ReadSnapshot()
    {
        AuthoritativePollFrame frame{};

        const auto expireMs = _unmanagedPulseExpireMs.load(std::memory_order_acquire);
        if (expireMs > 0 && NowMs() >= expireMs) {
            auto expectedExpireMs = expireMs;
            if (_unmanagedPulseExpireMs.compare_exchange_strong(
                    expectedExpireMs,
                    0,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                _unmanagedPulseDown.store(0, std::memory_order_release);
            }
        }

        frame.unmanagedDownMask =
            _unmanagedHeldDown.load(std::memory_order_acquire) |
            _unmanagedPulseDown.load(std::memory_order_acquire);
        frame.unmanagedPressedMask = _unmanagedPressedMask.load(std::memory_order_acquire);
        frame.unmanagedReleasedMask = _unmanagedReleasedMask.load(std::memory_order_acquire);
        frame.unmanagedPulseMask = _unmanagedPulseMask.load(std::memory_order_acquire);
        frame.committedDownMask = _committedDownMask.load(std::memory_order_acquire);
        frame.committedPressedMask = _committedPressedMask.load(std::memory_order_acquire);
        frame.committedReleasedMask = _committedReleasedMask.load(std::memory_order_acquire);
        frame.managedMask = _managedMask.load(std::memory_order_acquire);
        frame.context = static_cast<InputContext>(_contextValue.load(std::memory_order_acquire));
        frame.contextEpoch = _contextEpoch.load(std::memory_order_acquire);
        frame.sourceTimestampUs = _sourceTimestampUs.load(std::memory_order_acquire);
        frame.pollSequence = _pollSequence.load(std::memory_order_acquire);
        frame.hasDigital = _hasDigital.load(std::memory_order_acquire);
        frame.downMask = (frame.unmanagedDownMask & ~frame.managedMask) | frame.committedDownMask;
        frame.pressedMask = (frame.unmanagedPressedMask & ~frame.managedMask) | frame.committedPressedMask;
        frame.releasedMask = (frame.unmanagedReleasedMask & ~frame.managedMask) | frame.committedReleasedMask;
        frame.pulseMask = frame.unmanagedPulseMask & ~frame.managedMask;
        frame.xinputButtons = ToXInputButtons(frame.downMask);
        frame.xinputPressedButtons = ToXInputButtons(frame.pressedMask);
        frame.xinputReleasedButtons = ToXInputButtons(frame.releasedMask);
        frame.xinputPulseButtons = ToXInputButtons(frame.pulseMask);

        frame.hasAnalog = _hasAnalog.load(std::memory_order_acquire);
        if (frame.hasAnalog) {
            frame.moveX = _moveX.load(std::memory_order_acquire);
            frame.moveY = _moveY.load(std::memory_order_acquire);
            frame.lookX = _lookX.load(std::memory_order_acquire);
            frame.lookY = _lookY.load(std::memory_order_acquire);
            frame.leftTrigger = _leftTrigger.load(std::memory_order_acquire);
            frame.rightTrigger = _rightTrigger.load(std::memory_order_acquire);
        }

        frame.overflowed = _overflowed.load(std::memory_order_acquire);
        frame.coalesced = _coalesced.load(std::memory_order_acquire);

        return frame;
    }
}
