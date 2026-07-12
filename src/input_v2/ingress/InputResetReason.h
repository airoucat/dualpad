#pragma once

#include <cstdint>

namespace dualpad::input_v2::ingress
{
    enum class InputResetReason : std::uint32_t
    {
        None = 0,
        DeviceDisconnected = 1u << 0,
        ContextBoundary = 1u << 1,
        ControlMapReload = 1u << 2,
        QueueOverflow = 1u << 3,
        SequenceGap = 1u << 4,
        FocusLost = 1u << 5,
        LostReleaseReconciled = 1u << 6,
        SyntheticSuppressionReset = 1u << 7,
        ExplicitReset = 1u << 8,
        RuntimeOwnerFailure = 1u << 9
    };

    using InputResetReasonMask = std::uint32_t;

    [[nodiscard]] inline constexpr InputResetReasonMask ToMask(InputResetReason reason) noexcept
    {
        return static_cast<InputResetReasonMask>(reason);
    }

    enum class InputResetScope : std::uint8_t
    {
        GamepadSource = 0,
        KeyboardMouseSource,
        GlobalInputState
    };

    struct InputResetMarker
    {
        InputResetReasonMask reasons{ 0 };
        InputResetScope scope{ InputResetScope::GlobalInputState };
        std::uint64_t inputStateEpoch{ 0 };
        std::uint64_t gamepadSessionId{ 0 };
        std::uint32_t contextRevision{ 0 };
        std::uint32_t controlMapRevision{ 0 };
    };

    struct CausalLatestHeader
    {
        std::uint64_t generation{ 0 };
        std::uint64_t causalOrderedTailSeq{ 0 };
        std::uint64_t inputStateEpoch{ 0 };
        std::uint32_t contextRevision{ 0 };
        std::uint32_t controlMapRevision{ 0 };
    };

    enum class PhysicalInputSource : std::uint8_t
    {
        None = 0,
        Keyboard,
        Mouse,
        Gamepad
    };

    enum class GameplayChannel : std::uint8_t
    {
        Look = 0,
        Move,
        Combat,
        TransientDigital
    };
}
