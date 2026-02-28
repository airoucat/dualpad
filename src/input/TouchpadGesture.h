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

    class TouchpadGestureRecognizer
    {
    public:
        TouchpadGestureRecognizer();

        // 每帧调用，返回识别到的手势
        TouchGesture Update(const dse::State& state);

        void Reset();

    private:
        // 分区点击
        std::uint8_t _heldRegion{ 0 };  // 0=none, 1=left, 2=mid, 3=right
        bool _wasClicking{ false };

        // 滑动
        bool _tracking{ false };
        int _startX{ 0 }, _startY{ 0 };
        int _lastX{ 0 }, _lastY{ 0 };
        bool _suppressSwipe{ false };

        std::uint8_t ClassifyRegion(const dse::State& state) const;
        TouchGesture EvaluateSwipe() const;
    };
}