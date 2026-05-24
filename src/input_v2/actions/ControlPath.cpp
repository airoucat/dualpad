#include "pch.h"

#include "input_v2/actions/ControlPath.h"

#include <functional>
#include <sstream>

namespace dualpad::input_v2::actions
{
    std::size_t ControlPathHash::operator()(const ControlPath& path) const noexcept
    {
        std::size_t h = std::hash<std::uint8_t>{}(static_cast<std::uint8_t>(path.kind));
        h ^= std::hash<std::uint32_t>{}(path.code) << 1;
        h ^= std::hash<std::uint8_t>{}(static_cast<std::uint8_t>(path.component)) << 2;
        return h;
    }

    std::string_view ToString(ControlPathKind kind)
    {
        switch (kind) {
        case ControlPathKind::DigitalButton:
            return "DigitalButton";
        case ControlPathKind::AnalogAxis1D:
            return "AnalogAxis1D";
        case ControlPathKind::TouchGesture:
            return "TouchGesture";
        case ControlPathKind::TouchRegion:
            return "TouchRegion";
        default:
            return "Unknown";
        }
    }

    std::string_view ToString(AxisComponent component)
    {
        switch (component) {
        case AxisComponent::None:
            return "None";
        case AxisComponent::X:
            return "X";
        case AxisComponent::Y:
            return "Y";
        default:
            return "Unknown";
        }
    }

    std::string ToDebugString(const ControlPath& path)
    {
        std::ostringstream out;
        out << ToString(path.kind) << '(' << path.code << ',' << ToString(path.component) << ')';
        return out.str();
    }
}
