#pragma once
#include <cstdint>
#include <string_view>
#include <vector>

namespace dualpad::input
{
    enum class TriggerType : std::uint8_t
    {
        Button,
        Gesture,
        Layer,
        Combo,
        Axis,
        Hold,
        Tap
    };

    inline constexpr std::string_view ToString(TriggerType type)
    {
        switch (type) {
        case TriggerType::Button: return "Button";
        case TriggerType::Gesture: return "Gesture";
        case TriggerType::Layer: return "Layer";
        case TriggerType::Combo: return "Combo";
        case TriggerType::Axis: return "Axis";
        case TriggerType::Hold: return "Hold";
        case TriggerType::Tap: return "Tap";
        default: return "Unknown";
        }
    }

    struct Trigger
    {
        TriggerType type{ TriggerType::Button };
        std::uint32_t code{ 0 };
        std::vector<std::uint32_t> modifiers;

        bool operator==(const Trigger& other) const
        {
            return type == other.type &&
                code == other.code &&
                modifiers == other.modifiers;
        }
    };

    struct TriggerHash
    {
        std::size_t operator()(const Trigger& t) const noexcept
        {
            std::size_t h = std::hash<std::uint8_t>{}(static_cast<std::uint8_t>(t.type));
            h ^= std::hash<std::uint32_t>{}(t.code) << 1;
            for (auto mod : t.modifiers) {
                h ^= std::hash<std::uint32_t>{}(mod) << 2;
            }
            return h;
        }
    };
}
