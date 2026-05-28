#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace dualpad::input_v2::actions
{
    enum class ControlPathKind : std::uint8_t
    {
        DigitalButton = 0,
        AnalogAxis1D,
        TouchGesture,
        TouchRegion
    };

    enum class AxisComponent : std::uint8_t
    {
        None = 0,
        X,
        Y
    };

    struct ControlPath
    {
        ControlPathKind kind{ ControlPathKind::DigitalButton };
        std::uint32_t code{ 0 };
        AxisComponent component{ AxisComponent::None };

        friend bool operator==(const ControlPath&, const ControlPath&) = default;
    };

    struct ControlPathHash
    {
        std::size_t operator()(const ControlPath& path) const noexcept;
    };

    std::string_view ToString(ControlPathKind kind);
    std::string_view ToString(AxisComponent component);
    std::string ToDebugString(const ControlPath& path);
}
