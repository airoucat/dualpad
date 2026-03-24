#include "pch.h"
#include "input/injection/UnmanagedDigitalPublisher.h"

#include "input/AuthoritativePollState.h"
#include "input/RuntimeConfig.h"

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        bool ShouldLogRawDigital()
        {
            const auto& config = RuntimeConfig::GetSingleton();
            return config.LogMappingEvents() || config.LogNativeInjection();
        }
    }

    void PublishUnmanagedDigitalState(const SyntheticPadFrame& frame, std::uint32_t handledButtons)
    {
        auto& authoritativeState = AuthoritativePollState::GetSingleton();

        if (handledButtons != 0) {
            authoritativeState.SetUnmanagedButton(handledButtons, false);
        }

        const auto filteredPulse = frame.pulseMask & ~handledButtons;
        const auto filteredPressed = frame.pressedMask & ~handledButtons;
        const auto filteredReleased = frame.releasedMask & ~handledButtons;

        authoritativeState.PublishUnmanagedDigitalEdges(
            filteredPressed,
            filteredReleased,
            filteredPulse);

        if (filteredPulse != 0) {
            authoritativeState.PulseUnmanagedButton(filteredPulse);
            if (ShouldLogRawDigital()) {
                logger::info(
                    "[DualPad][RawDigital] Pulsed unmanaged raw buttons 0x{:08X} for transient press-release",
                    filteredPulse);
            }
        }

        if (filteredPressed != 0) {
            authoritativeState.SetUnmanagedButton(filteredPressed, true);
        }

        if (filteredReleased != 0) {
            authoritativeState.SetUnmanagedButton(filteredReleased, false);
        }
    }
}
