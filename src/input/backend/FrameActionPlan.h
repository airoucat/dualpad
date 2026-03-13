#pragma once

#include "input/InputContext.h"
#include "input/backend/ActionOutputContract.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace dualpad::input::backend
{
    enum class PlannedBackend : std::uint8_t
    {
        NativeState = 0,
        ButtonEvent,
        KeyboardNative,
        Plugin,
        ModEvent,
        CompatibilityFallback
    };

    enum class PlannedActionKind : std::uint8_t
    {
        NativeButton = 0,
        KeyboardKey,
        NativeAxis1D,
        NativeAxis2D,
        PluginAction,
        ModEvent
    };

    enum class PlannedActionPhase : std::uint8_t
    {
        None = 0,
        Pulse,
        Press,
        Hold,
        Release,
        Value
    };

    struct PlannedAction
    {
        PlannedBackend backend{ PlannedBackend::CompatibilityFallback };
        PlannedActionKind kind{ PlannedActionKind::PluginAction };
        PlannedActionPhase phase{ PlannedActionPhase::None };
        InputContext context{ InputContext::Gameplay };
        std::string actionId{};
        ActionOutputContract contract{ ActionOutputContract::Pulse };
        std::uint32_t sourceCode{ 0 };
        std::uint32_t outputCode{ 0 };
        std::uint32_t modifierMask{ 0 };
        std::uint64_t timestampUs{ 0 };
        float valueX{ 0.0f };
        float valueY{ 0.0f };
        float heldSeconds{ 0.0f };
    };

    class FrameActionPlan
    {
    public:
        static constexpr std::size_t kMaxActions = 96;

        void Clear()
        {
            _count = 0;
            _overflowed = false;
        }

        bool Push(const PlannedAction& action)
        {
            if (_count >= _actions.size()) {
                _overflowed = true;
                return false;
            }

            _actions[_count++] = action;
            return true;
        }

        [[nodiscard]] std::size_t Size() const
        {
            return _count;
        }

        [[nodiscard]] bool Empty() const
        {
            return _count == 0;
        }

        [[nodiscard]] bool Overflowed() const
        {
            return _overflowed;
        }

        [[nodiscard]] const PlannedAction& operator[](std::size_t index) const
        {
            return _actions[index];
        }

        [[nodiscard]] const PlannedAction* begin() const
        {
            return _actions.data();
        }

        [[nodiscard]] const PlannedAction* end() const
        {
            return _actions.data() + _count;
        }

    private:
        std::array<PlannedAction, kMaxActions> _actions{};
        std::size_t _count{ 0 };
        bool _overflowed{ false };
    };
}
