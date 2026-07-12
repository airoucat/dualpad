#pragma once

#include <cstdint>

namespace dualpad::input_v2::runtime
{
    enum class GamepadDeviceAvailabilityPolicy : std::uint8_t
    {
        Native = 0,
        ScopedConnectivity
    };

    enum class GamepadAvailabilityDomain : std::uint8_t
    {
        None = 0,
        VerifiedPollOrInitialization,
        Remap
    };

    struct GamepadDeviceAvailabilityDecision
    {
        GamepadDeviceAvailabilityPolicy policy{ GamepadDeviceAvailabilityPolicy::Native };
        GamepadAvailabilityDomain allowedDomain{ GamepadAvailabilityDomain::None };
        bool connected{ false };
        bool delegateReady{ false };
        std::uint64_t gamepadSessionId{ 0 };
        std::uint32_t contextRevision{ 0 };
    };

    [[nodiscard]] bool ResolveGamepadDeviceAvailability(
        const GamepadDeviceAvailabilityDecision& decision,
        GamepadAvailabilityDomain queryDomain,
        bool originalValue) noexcept;
}
