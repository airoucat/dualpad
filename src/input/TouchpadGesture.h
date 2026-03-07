#pragma once
#include "input/DualSenseProtocol.h"
#include <cstdint>

namespace dualpad::input
{
    enum class TouchGesture : std::uint8_t
    {
        None,
        LeftPress,
        MidPress,
        RightPress,
        SwipeUp,
        SwipeDown,
        SwipeLeft,
        SwipeRight
    };

    inline constexpr std::string_view ToString(TouchGesture g)
    {
        switch (g) {
        case TouchGesture::LeftPress: return "TpLeftPress";
        case TouchGesture::MidPress: return "TpMidPress";
        case TouchGesture::RightPress: return "TpRightPress";
        case TouchGesture::SwipeUp: return "TpSwipeUp";
        case TouchGesture::SwipeDown: return "TpSwipeDown";
        case TouchGesture::SwipeLeft: return "TpSwipeLeft";
        case TouchGesture::SwipeRight: return "TpSwipeRight";
        default: return "None";
        }
    }

    // Converts raw touch data into click regions and coarse swipe directions.
    class TouchpadGestureRecognizer
    {
    public:
        TouchpadGestureRecognizer();

        // Returns one gesture edge for the latest touch update.
        TouchGesture Update(const dse::State& state);

        void Reset();

    private:
        // Click state remembers which zone the press started in.
        std::uint8_t _heldRegion{ 0 };
        bool _wasClicking{ false };

        // Swipe state keeps the first and last touch points until release.
        bool _tracking{ false };
        int _startX{ 0 }, _startY{ 0 };
        int _lastX{ 0 }, _lastY{ 0 };
        bool _suppressSwipe{ false };

        std::uint8_t ClassifyRegion(const dse::State& state) const;
        TouchGesture EvaluateSwipe() const;
    };
}
