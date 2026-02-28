#include "pch.h"
#include "input/TouchpadGesture.h"
#include <SKSE/SKSE.h>
#include <cmath>

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        constexpr int kSwipeThresholdX = 340;
        constexpr int kSwipeThresholdY = 190;
    }

    TouchpadGestureRecognizer::TouchpadGestureRecognizer() = default;

    std::uint8_t TouchpadGestureRecognizer::ClassifyRegion(const dse::State& state) const
    {
        if (!state.hasTouchData || !state.touch1.active) {
            return 2;  // 无触摸时默认中区
        }

        const auto x = state.touch1.x;
        if (x < 640) return 1;
        if (x < 1280) return 2;
        return 3;
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
            _heldRegion = ClassifyRegion(state);
            _wasClicking = true;

            if (_tracking) {
                _suppressSwipe = true;
            }

            logger::info("[DualPad][Touchpad] Click region: {}", _heldRegion);

            switch (_heldRegion) {
            case 1: return TouchGesture::LeftPress;
            case 2: return TouchGesture::MidPress;
            case 3: return TouchGesture::RightPress;
            }
        }
        else if (_wasClicking && !clicking) {
            _wasClicking = false;
            _heldRegion = 0;
        }

        // === 滑动 ===
        if (!_wasClicking && touching && !_tracking) {
            _tracking = true;
            _startX = static_cast<int>(state.touch1.x);
            _startY = static_cast<int>(state.touch1.y);
            _lastX = _startX;
            _lastY = _startY;
            _suppressSwipe = false;
        }
        else if (_tracking && touching) {
            _lastX = static_cast<int>(state.touch1.x);
            _lastY = static_cast<int>(state.touch1.y);
        }
        else if (_tracking && !touching) {
            _tracking = false;

            if (!_suppressSwipe) {
                auto gesture = EvaluateSwipe();
                if (gesture != TouchGesture::None) {
                    logger::info("[DualPad][Touchpad] Swipe: {}", ToString(gesture));
                    return gesture;
                }
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