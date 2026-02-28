#include "pch.h"
#include "input/TouchpadGesture.h"
#include <cmath>

namespace dualpad::input
{
    namespace
    {
        constexpr int kSwipeThresholdX = 340;  // ~18% of 1920
        constexpr int kSwipeThresholdY = 190;  // ~18% of 1080
    }

    TouchpadGestureRecognizer::TouchpadGestureRecognizer() = default;

    std::uint8_t TouchpadGestureRecognizer::ClassifyRegion(const dse::State& state) const
    {
        if (!state.hasTouchData || !state.touch1.active) {
            return 2;  // 无触摸时默认中区
        }

        const auto x = state.touch1.x;
        if (x < 640) return 1;      // 左区
        if (x < 1280) return 2;     // 中区
        return 3;                   // 右区
    }

    TouchGesture TouchpadGestureRecognizer::EvaluateSwipe() const
    {
        const int dx = _lastX - _startX;
        const int dy = _lastY - _startY;

        if (std::abs(dx) < kSwipeThresholdX && std::abs(dy) < kSwipeThresholdY) {
            return TouchGesture::None;
        }

        if (std::abs(dx) >= std::abs(dy)) {
            return (dx >= 0) ? TouchGesture::SwipeRight : TouchGesture::SwipeLeft;
        }
        else {
            return (dy >= 0) ? TouchGesture::SwipeDown : TouchGesture::SwipeUp;
        }
    }

    TouchGesture TouchpadGestureRecognizer::Update(const dse::State& state)
    {
        const bool clicking = (state.btn2 & dse::btn::kTouchpadClick) != 0;
        const bool touching = state.hasTouchData && state.touch1.active;

        // === 分区点击 ===
        if (!_wasClicking && clicking) {
            // 点击开始
            _heldRegion = ClassifyRegion(state);
            _wasClicking = true;

            if (_tracking) {
                _suppressSwipe = true;  // 点击时禁止滑动
            }

            switch (_heldRegion) {
            case 1: return TouchGesture::LeftPress;
            case 2: return TouchGesture::MidPress;
            case 3: return TouchGesture::RightPress;
            }
        }
        else if (_wasClicking && !clicking) {
            // 点击结束
            _wasClicking = false;
            _heldRegion = 0;
        }

        // === 滑动 ===
        if (!_wasClicking && touching && !_tracking) {
            // 触摸开始
            _tracking = true;
            _startX = static_cast<int>(state.touch1.x);
            _startY = static_cast<int>(state.touch1.y);
            _lastX = _startX;
            _lastY = _startY;
            _suppressSwipe = false;
        }
        else if (_tracking && touching) {
            // 触摸移动
            _lastX = static_cast<int>(state.touch1.x);
            _lastY = static_cast<int>(state.touch1.y);
        }
        else if (_tracking && !touching) {
            // 触摸结束
            _tracking = false;

            if (!_suppressSwipe) {
                return EvaluateSwipe();
            }
        }

        return TouchGesture::None;
    }

    void TouchpadGestureRecognizer::Reset()
    {
        _heldRegion = 0;
        _wasClicking = false;
        _tracking = false;
        _suppressSwipe = false;
    }
}