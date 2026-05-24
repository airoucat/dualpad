#include "pch.h"

#include "input_v2/actions/InteractionSpec.h"

#include <sstream>

namespace dualpad::input_v2::actions
{
    std::string_view ToString(BindingModifierKind kind)
    {
        switch (kind) {
        case BindingModifierKind::Deadzone:
            return "Deadzone";
        case BindingModifierKind::Scale:
            return "Scale";
        case BindingModifierKind::Invert:
            return "Invert";
        case BindingModifierKind::Clamp:
            return "Clamp";
        case BindingModifierKind::AxisThreshold:
            return "AxisThreshold";
        default:
            return "Unknown";
        }
    }

    std::string_view ToString(InteractionKind kind)
    {
        switch (kind) {
        case InteractionKind::Value:
            return "Value";
        case InteractionKind::Press:
            return "Press";
        case InteractionKind::Hold:
            return "Hold";
        case InteractionKind::Tap:
            return "Tap";
        case InteractionKind::Repeat:
            return "Repeat";
        case InteractionKind::Toggle:
            return "Toggle";
        case InteractionKind::Chord:
            return "Chord";
        case InteractionKind::Gesture:
            return "Gesture";
        default:
            return "Unknown";
        }
    }

    std::string_view ToString(BindingMatchPolicy policy)
    {
        switch (policy) {
        case BindingMatchPolicy::ExactOnly:
            return "ExactOnly";
        case BindingMatchPolicy::PreferExactThenSubset:
            return "PreferExactThenSubset";
        default:
            return "Unknown";
        }
    }

    std::string_view ToString(DisplayBindingMode mode)
    {
        switch (mode) {
        case DisplayBindingMode::Primary:
            return "Primary";
        case DisplayBindingMode::Alternate:
            return "Alternate";
        case DisplayBindingMode::Hidden:
            return "Hidden";
        default:
            return "Unknown";
        }
    }

    std::string ToDebugString(const InteractionSpec& spec)
    {
        std::ostringstream out;
        out << ToString(spec.kind) << "(primary=" << spec.primaryPathIndex << ",required=";
        for (std::size_t i = 0; i < spec.requiredPathIndices.size(); ++i) {
            if (i > 0) {
                out << '+';
            }
            out << spec.requiredPathIndices[i];
        }
        out << ')';
        return out.str();
    }
}
