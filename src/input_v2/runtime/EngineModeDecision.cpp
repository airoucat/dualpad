#include "pch.h"

#include "input_v2/runtime/EngineModeDecision.h"

namespace dualpad::input_v2::runtime
{
    bool ResolveGamepadDeviceAvailability(
        const GamepadDeviceAvailabilityDecision& decision,
        GamepadAvailabilityDomain queryDomain,
        bool originalValue) noexcept
    {
        if (decision.policy != GamepadDeviceAvailabilityPolicy::ScopedConnectivity ||
            queryDomain == GamepadAvailabilityDomain::Remap ||
            decision.allowedDomain != GamepadAvailabilityDomain::VerifiedPollOrInitialization ||
            queryDomain != decision.allowedDomain) {
            return originalValue;
        }
        return decision.connected && decision.delegateReady;
    }
}
