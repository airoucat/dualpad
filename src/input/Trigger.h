#pragma once
#include <cstdint>
#include <vector>

namespace dualpad::input
{
    enum class TriggerType : std::uint8_t
    {
        Button,
        Gesture,
        Combo,
        Axis
    };

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