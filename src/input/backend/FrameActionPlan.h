#pragma once

#include "input/InputContext.h"
#include "input/backend/ActionLifecyclePolicy.h"
#include "input/backend/ActionOutputContract.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace dualpad::input::backend
{
    enum class PlannedBackend : std::uint8_t
    {
        None = 0,
        NativeState,
        NativeButtonCommit,
        KeyboardHelper,
        Plugin,
        ModEvent
    };

    inline constexpr std::string_view ToString(PlannedBackend backend)
    {
        switch (backend) {
        case PlannedBackend::None:
            return "None";
        case PlannedBackend::NativeState:
            return "NativeState";
        case PlannedBackend::NativeButtonCommit:
            return "NativeButtonCommit";
        case PlannedBackend::KeyboardHelper:
            return "KeyboardHelper";
        case PlannedBackend::Plugin:
            return "Plugin";
        case PlannedBackend::ModEvent:
            return "ModEvent";
        default:
            return "None";
        }
    }

    enum class PlannedActionKind : std::uint8_t
    {
        NativeButton = 0,
        KeyboardKey,
        NativeAxis1D,
        NativeAxis2D,
        PluginAction,
        ModEvent
    };

    inline constexpr std::string_view ToString(PlannedActionKind kind)
    {
        switch (kind) {
        case PlannedActionKind::NativeButton:
            return "NativeButton";
        case PlannedActionKind::KeyboardKey:
            return "KeyboardKey";
        case PlannedActionKind::NativeAxis1D:
            return "NativeAxis1D";
        case PlannedActionKind::NativeAxis2D:
            return "NativeAxis2D";
        case PlannedActionKind::PluginAction:
            return "PluginAction";
        case PlannedActionKind::ModEvent:
        default:
            return "ModEvent";
        }
    }

    enum class PlannedActionPhase : std::uint8_t
    {
        None = 0,
        Pulse,
        Press,
        Hold,
        Release,
        Value
    };

    inline constexpr std::string_view ToString(PlannedActionPhase phase)
    {
        switch (phase) {
        case PlannedActionPhase::Pulse:
            return "Pulse";
        case PlannedActionPhase::Press:
            return "Press";
        case PlannedActionPhase::Hold:
            return "Hold";
        case PlannedActionPhase::Release:
            return "Release";
        case PlannedActionPhase::Value:
            return "Value";
        case PlannedActionPhase::None:
        default:
            return "None";
        }
    }

    enum class NativeDigitalPolicyKind : std::uint8_t
    {
        None = 0,
        PulseMinDown,
        HoldOwner,
        RepeatOwner,
        ToggleDebounced
    };

    inline constexpr std::string_view ToString(NativeDigitalPolicyKind policy)
    {
        switch (policy) {
        case NativeDigitalPolicyKind::PulseMinDown:
            return "PulseMinDown";
        case NativeDigitalPolicyKind::HoldOwner:
            return "HoldOwner";
        case NativeDigitalPolicyKind::RepeatOwner:
            return "RepeatOwner";
        case NativeDigitalPolicyKind::ToggleDebounced:
            return "ToggleDebounced";
        case NativeDigitalPolicyKind::None:
        default:
            return "None";
        }
    }

    struct PlannedAction
    {
        PlannedBackend backend{ PlannedBackend::None };
        PlannedActionKind kind{ PlannedActionKind::PluginAction };
        PlannedActionPhase phase{ PlannedActionPhase::None };
        InputContext context{ InputContext::Gameplay };
        std::string actionId{};
        ActionOutputContract contract{ ActionOutputContract::Pulse };
        ActionLifecyclePolicy lifecyclePolicy{ ActionLifecyclePolicy::None };
        std::uint32_t sourceCode{ 0 };
        std::uint32_t outputCode{ 0 };
        std::uint32_t modifierMask{ 0 };
        std::uint64_t timestampUs{ 0 };
        float valueX{ 0.0f };
        float valueY{ 0.0f };
        float heldSeconds{ 0.0f };
        NativeDigitalPolicyKind digitalPolicy{ NativeDigitalPolicyKind::None };
        bool gateAware{ false };
        std::uint32_t minDownMs{ 0 };
        std::uint32_t repeatDelayMs{ 0 };
        std::uint32_t repeatIntervalMs{ 0 };
        std::uint32_t contextEpoch{ 0 };
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
