#pragma once

#include "input/Trigger.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace dualpad::input
{
    enum class PadEventType : std::uint8_t
    {
        None,
        ButtonPress,
        ButtonRelease,
        AxisChange,
        Combo,
        Hold,
        Tap,
        Gesture,
        TouchpadPress,
        TouchpadRelease,
        TouchpadSlide
    };

    enum class PadAxisId : std::uint8_t
    {
        None,
        // Keep axis event codes outside the synthetic digital pad-bit space so
        // generic helpers such as IsSyntheticPadBitCode() cannot mistake an
        // AxisChange event for a button bitmask.
        LeftStickX = 0x91,
        LeftStickY = 0x92,
        RightStickX = 0x93,
        RightStickY = 0x94,
        LeftTrigger = 0x95,
        RightTrigger = 0x96
    };

    enum class TouchpadMode : std::uint8_t
    {
        LeftCenterRight,
        Edge,
        Whole,
        Disabled
    };

    enum class TouchpadPressRegion : std::uint8_t
    {
        None,
        Left,
        Center,
        Right,
        TopEdge,
        BottomEdge,
        LeftEdge,
        RightEdge,
        Whole
    };

    enum class TouchpadSlideDirection : std::uint8_t
    {
        None,
        Up,
        Down,
        Left,
        Right
    };

    namespace mapping_codes
    {
        inline constexpr std::uint32_t kTpLeftPress = 0x01000000;
        inline constexpr std::uint32_t kTpMidPress = 0x02000000;
        inline constexpr std::uint32_t kTpRightPress = 0x04000000;
        inline constexpr std::uint32_t kTpSwipeUp = 0x08000000;
        inline constexpr std::uint32_t kTpSwipeDown = 0x10000000;
        inline constexpr std::uint32_t kTpSwipeLeft = 0x20000000;
        inline constexpr std::uint32_t kTpSwipeRight = 0x40000000;

        // These identifiers live only in the mapping layer and are not forwarded
        // through the legacy XInput compatibility mask.
        inline constexpr std::uint32_t kTpEdgeTopPress = 0x81000001;
        inline constexpr std::uint32_t kTpEdgeBottomPress = 0x81000002;
        inline constexpr std::uint32_t kTpEdgeLeftPress = 0x81000003;
        inline constexpr std::uint32_t kTpEdgeRightPress = 0x81000004;
        inline constexpr std::uint32_t kTpWholePress = 0x81000005;
    }

    inline constexpr std::string_view ToString(PadEventType type)
    {
        switch (type) {
        case PadEventType::ButtonPress: return "ButtonPress";
        case PadEventType::ButtonRelease: return "ButtonRelease";
        case PadEventType::AxisChange: return "AxisChange";
        case PadEventType::Combo: return "Combo";
        case PadEventType::Hold: return "Hold";
        case PadEventType::Tap: return "Tap";
        case PadEventType::Gesture: return "Gesture";
        case PadEventType::TouchpadPress: return "TouchpadPress";
        case PadEventType::TouchpadRelease: return "TouchpadRelease";
        case PadEventType::TouchpadSlide: return "TouchpadSlide";
        default: return "None";
        }
    }

    inline constexpr std::string_view ToString(PadAxisId axis)
    {
        switch (axis) {
        case PadAxisId::LeftStickX: return "LeftStickX";
        case PadAxisId::LeftStickY: return "LeftStickY";
        case PadAxisId::RightStickX: return "RightStickX";
        case PadAxisId::RightStickY: return "RightStickY";
        case PadAxisId::LeftTrigger: return "LeftTrigger";
        case PadAxisId::RightTrigger: return "RightTrigger";
        default: return "None";
        }
    }

    inline constexpr std::string_view ToString(TouchpadMode mode)
    {
        switch (mode) {
        case TouchpadMode::LeftCenterRight: return "LeftCenterRight";
        case TouchpadMode::Edge: return "Edge";
        case TouchpadMode::Whole: return "Whole";
        default: return "Disabled";
        }
    }

    inline constexpr std::string_view ToString(TouchpadPressRegion region)
    {
        switch (region) {
        case TouchpadPressRegion::Left: return "Left";
        case TouchpadPressRegion::Center: return "Center";
        case TouchpadPressRegion::Right: return "Right";
        case TouchpadPressRegion::TopEdge: return "TopEdge";
        case TouchpadPressRegion::BottomEdge: return "BottomEdge";
        case TouchpadPressRegion::LeftEdge: return "LeftEdge";
        case TouchpadPressRegion::RightEdge: return "RightEdge";
        case TouchpadPressRegion::Whole: return "Whole";
        default: return "None";
        }
    }

    inline constexpr std::string_view ToString(TouchpadSlideDirection direction)
    {
        switch (direction) {
        case TouchpadSlideDirection::Up: return "Up";
        case TouchpadSlideDirection::Down: return "Down";
        case TouchpadSlideDirection::Left: return "Left";
        case TouchpadSlideDirection::Right: return "Right";
        default: return "None";
        }
    }

    inline constexpr bool IsSyntheticPadBitCode(std::uint32_t code)
    {
        return code != 0 &&
            code < 0x80000000u &&
            std::has_single_bit(code);
    }

    struct PadEvent
    {
        PadEventType type{ PadEventType::None };
        TriggerType triggerType{ TriggerType::Button };
        std::uint32_t code{ 0 };
        std::uint64_t timestampUs{ 0 };
        std::uint32_t modifierMask{ 0 };

        PadAxisId axis{ PadAxisId::None };
        float value{ 0.0f };
        float previousValue{ 0.0f };

        std::uint8_t touchId{ 0 };
        std::uint16_t touchX{ 0 };
        std::uint16_t touchY{ 0 };
        TouchpadMode touchpadMode{ TouchpadMode::Disabled };
        TouchpadPressRegion touchRegion{ TouchpadPressRegion::None };
        TouchpadSlideDirection slideDirection{ TouchpadSlideDirection::None };
    };

    inline constexpr std::size_t kMaxPadEventsPerFrame = 64;

    struct PadEventBuffer
    {
        std::array<PadEvent, kMaxPadEventsPerFrame> events{};
        std::size_t count{ 0 };
        std::size_t droppedCount{ 0 };
        bool overflowed{ false };

        void Clear()
        {
            count = 0;
            droppedCount = 0;
            overflowed = false;
        }

        bool Push(const PadEvent& event)
        {
            if (count >= events.size()) {
                overflowed = true;
                ++droppedCount;
                return false;
            }

            events[count++] = event;
            return true;
        }

        const PadEvent& operator[](std::size_t index) const
        {
            return events[index];
        }

        PadEvent& operator[](std::size_t index)
        {
            return events[index];
        }
    };
}
