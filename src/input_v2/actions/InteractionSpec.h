#pragma once

#include "input_v2/actions/ControlPath.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dualpad::input_v2::actions
{
    enum class BindingModifierKind : std::uint8_t
    {
        Deadzone = 0,
        Scale,
        Invert,
        Clamp,
        AxisThreshold
    };

    struct BindingModifier
    {
        BindingModifierKind kind{ BindingModifierKind::Deadzone };
        float primary{ 0.0f };
        float secondary{ 0.0f };

        friend bool operator==(const BindingModifier&, const BindingModifier&) = default;
    };

    enum class InteractionKind : std::uint8_t
    {
        Value = 0,
        Press,
        Hold,
        Tap,
        Repeat,
        Toggle,
        Chord,
        Gesture
    };

    enum class BindingMatchPolicy : std::uint8_t
    {
        ExactOnly = 0,
        PreferExactThenSubset
    };

    enum class DisplayBindingMode : std::uint8_t
    {
        Primary = 0,
        Alternate,
        Hidden
    };

    struct InteractionSpec
    {
        InteractionKind kind{ InteractionKind::Press };
        std::uint16_t primaryPathIndex{ 0 };
        std::vector<std::uint16_t> requiredPathIndices;
        std::uint64_t holdThresholdUs{ 0 };
        std::uint64_t tapMaxUs{ 0 };
        std::uint64_t repeatDelayUs{ 0 };
        std::uint64_t repeatIntervalUs{ 0 };
        std::uint64_t chordWindowUs{ 0 };
        bool unordered{ false };

        friend bool operator==(const InteractionSpec&, const InteractionSpec&) = default;
    };

    inline constexpr std::uint64_t kLegacyHoldThresholdUs = 350'000;
    inline constexpr std::uint64_t kLegacyTapThresholdUs = 220'000;
    inline constexpr std::uint64_t kLegacyRepeatDelayUs = 400'000;
    inline constexpr std::uint64_t kLegacyRepeatIntervalUs = 80'000;
    inline constexpr std::uint64_t kLegacyComboWindowUs = 120'000;

    std::string_view ToString(BindingModifierKind kind);
    std::string_view ToString(InteractionKind kind);
    std::string_view ToString(BindingMatchPolicy policy);
    std::string_view ToString(DisplayBindingMode mode);
    std::string ToDebugString(const InteractionSpec& spec);
}
