#include "pch.h"
#include "input/injection/CompatibilityInputInjector.h"

#include "input/SyntheticPadState.h"

namespace logger = SKSE::log;

namespace dualpad::input
{
    void CompatibilityInputInjector::Reset()
    {
        _virtualHeldDown = 0;
        _submittedVirtualHeldDown = 0;
        SyntheticPadState::GetSingleton().Reset();
    }

    void CompatibilityInputInjector::SubmitFrame(
        const SyntheticPadFrame& frame,
        std::uint32_t handledButtons)
    {
        SubmitLegacyDigitalFallback(frame, handledButtons);
        SubmitAnalogState(frame);
    }

    void CompatibilityInputInjector::SubmitLegacyDigitalFallback(
        const SyntheticPadFrame& frame,
        std::uint32_t handledButtons)
    {
        if (handledButtons != 0) {
            // Hold/Tap bindings may resolve after the original press already
            // entered the compatibility mask. Clear them explicitly so the
            // mapped action does not keep leaking through to Skyrim.
            SyntheticPadState::GetSingleton().SetButton(handledButtons, false);
        }

        const auto filteredPulse = frame.pulseMask & ~handledButtons;
        const auto filteredPressed = frame.pressedMask & ~handledButtons;
        const auto filteredReleased = frame.releasedMask & ~handledButtons;

        if (filteredPulse != 0) {
            SyntheticPadState::GetSingleton().PulseButton(filteredPulse);
            logger::info(
                "[DualPad][CompatibilityInjector] Pulsed raw compatibility buttons 0x{:08X} for transient press-release",
                filteredPulse);
        }

        if (filteredPressed != 0) {
            SyntheticPadState::GetSingleton().SetButton(filteredPressed, true);
        }

        if (filteredReleased != 0) {
            SyntheticPadState::GetSingleton().SetButton(filteredReleased, false);
        }

        const auto virtualReleased = _submittedVirtualHeldDown & ~_virtualHeldDown;
        if (virtualReleased != 0) {
            SyntheticPadState::GetSingleton().SetButton(virtualReleased, false);
        }

        if (_virtualHeldDown != 0) {
            SyntheticPadState::GetSingleton().SetButton(_virtualHeldDown, true);
        }

        _submittedVirtualHeldDown = _virtualHeldDown;
    }

    void CompatibilityInputInjector::SubmitAnalogState(const SyntheticPadFrame& frame)
    {
        SyntheticPadState::GetSingleton().SetAxis(
            frame.leftStickX.value,
            frame.leftStickY.value,
            frame.rightStickX.value,
            frame.rightStickY.value,
            frame.leftTrigger.value,
            frame.rightTrigger.value);
    }

    void CompatibilityInputInjector::PulseButton(
        std::uint32_t bit,
        std::string_view reason)
    {
        if (!bit) {
            return;
        }

        SyntheticPadState::GetSingleton().PulseButton(bit);
        logger::info(
            "[DualPad][CompatibilityInjector] Pulsed virtual button 0x{:08X} for {}",
            bit,
            reason);
    }

    void CompatibilityInputInjector::SetButtonState(
        std::uint32_t bit,
        bool down,
        std::string_view reason)
    {
        (void)reason;

        if (!bit) {
            return;
        }

        if (down) {
            _virtualHeldDown |= bit;
        }
        else {
            _virtualHeldDown &= ~bit;
        }
    }
}
