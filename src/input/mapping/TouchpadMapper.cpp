#include "pch.h"
#include "input/mapping/TouchpadMapper.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        constexpr float kTouchpadMaxX = 1919.0f;
        constexpr float kTouchpadMaxY = 1079.0f;

        float Clamp01(float value)
        {
            return (std::clamp)(value, 0.0f, 1.0f);
        }

        float ClampHalf(float value)
        {
            return (std::clamp)(value, 0.0f, 0.5f);
        }

        float NormalizeX(std::uint16_t value)
        {
            return Clamp01(static_cast<float>(value) / kTouchpadMaxX);
        }

        float NormalizeY(std::uint16_t value)
        {
            return Clamp01(static_cast<float>(value) / kTouchpadMaxY);
        }

        void PushTouchpadEvent(
            PadEventType type,
            std::uint32_t code,
            const PadState& state,
            std::uint8_t touchId,
            std::uint16_t touchX,
            std::uint16_t touchY,
            TouchpadMode mode,
            TouchpadPressRegion region,
            TouchpadSlideDirection direction,
            PadEventBuffer& outEvents)
        {
            PadEvent event{};
            event.type = type;
            event.triggerType = TriggerType::Gesture;
            event.code = code;
            event.timestampUs = state.timestampUs;
            event.touchId = touchId;
            event.touchX = touchX;
            event.touchY = touchY;
            event.touchpadMode = mode;
            event.touchRegion = region;
            event.slideDirection = direction;
            outEvents.Push(event);
        }
    }

    TouchpadMapper::TouchpadMapper()
    {
        SetPressBinding(TouchpadPressRegion::Left, mapping_codes::kTpLeftPress);
        SetPressBinding(TouchpadPressRegion::Center, mapping_codes::kTpMidPress);
        SetPressBinding(TouchpadPressRegion::Right, mapping_codes::kTpRightPress);
        SetPressBinding(TouchpadPressRegion::TopEdge, mapping_codes::kTpEdgeTopPress);
        SetPressBinding(TouchpadPressRegion::BottomEdge, mapping_codes::kTpEdgeBottomPress);
        SetPressBinding(TouchpadPressRegion::LeftEdge, mapping_codes::kTpEdgeLeftPress);
        SetPressBinding(TouchpadPressRegion::RightEdge, mapping_codes::kTpEdgeRightPress);
        SetPressBinding(TouchpadPressRegion::Whole, mapping_codes::kTpWholePress);

        SetSlideBinding(TouchpadSlideDirection::Up, mapping_codes::kTpSwipeUp);
        SetSlideBinding(TouchpadSlideDirection::Down, mapping_codes::kTpSwipeDown);
        SetSlideBinding(TouchpadSlideDirection::Left, mapping_codes::kTpSwipeLeft);
        SetSlideBinding(TouchpadSlideDirection::Right, mapping_codes::kTpSwipeRight);
    }

    void TouchpadMapper::Reset()
    {
        _pressActive = false;
        _pressTouchId = 0;
        _pressX = 0;
        _pressY = 0;
        _pressRegion = TouchpadPressRegion::None;
        _pressCode = 0;

        _tracking = false;
        _trackingTouchId = 0;
        _startX = 0;
        _startY = 0;
        _lastX = 0;
        _lastY = 0;
        _suppressSlide = false;
        _hasLastKnownTouch = false;
        _lastKnownTouchId = 0;
        _lastKnownTouchX = 0;
        _lastKnownTouchY = 0;
    }

    void TouchpadMapper::SetMode(TouchpadMode mode)
    {
        _config.mode = mode;
    }

    TouchpadMode TouchpadMapper::GetMode() const
    {
        return _config.mode;
    }

    void TouchpadMapper::SetConfig(const TouchpadConfig& config)
    {
        _config = config;
        const auto edgeThreshold = Clamp01(config.edgeThreshold);
        const auto leftRightBoundary = ClampHalf(config.leftRightBoundary);
        const auto slideThreshold = Clamp01(config.slideThreshold);

        if (edgeThreshold != config.edgeThreshold ||
            leftRightBoundary != config.leftRightBoundary ||
            slideThreshold != config.slideThreshold) {
            logger::warn(
                "[DualPad][Touchpad] Clamped config edgeThreshold {:.2f}->{:.2f} leftRightBoundary {:.2f}->{:.2f} slideThreshold {:.2f}->{:.2f}",
                config.edgeThreshold,
                edgeThreshold,
                config.leftRightBoundary,
                leftRightBoundary,
                config.slideThreshold,
                slideThreshold);
        }

        _config.edgeThreshold = edgeThreshold;
        _config.leftRightBoundary = leftRightBoundary;
        _config.slideThreshold = slideThreshold;
    }

    const TouchpadConfig& TouchpadMapper::GetConfig() const
    {
        return _config;
    }

    void TouchpadMapper::SetPressBinding(TouchpadPressRegion region, std::uint32_t code)
    {
        _pressBindings[static_cast<std::size_t>(region)] = code;
    }

    void TouchpadMapper::SetSlideBinding(TouchpadSlideDirection direction, std::uint32_t code)
    {
        _slideBindings[static_cast<std::size_t>(direction)] = code;
    }

    void TouchpadMapper::ProcessTouch(const PadState& state, PadEventBuffer& outEvents)
    {
        if (state.touch1.active) {
            _hasLastKnownTouch = true;
            _lastKnownTouchId = state.touch1.id;
            _lastKnownTouchX = state.touch1.x;
            _lastKnownTouchY = state.touch1.y;
        }

        GeneratePressEvent(state, outEvents);
        GenerateSlideEvent(state, outEvents);
    }

    void TouchpadMapper::GeneratePressEvent(const PadState& state, PadEventBuffer& outEvents)
    {
        const auto& touch = state.touch1;
        const bool clicking = state.buttons.touchpadClick;
        auto effectiveTouch = touch;
        if (!effectiveTouch.active && _hasLastKnownTouch) {
            effectiveTouch.active = true;
            effectiveTouch.id = _lastKnownTouchId;
            effectiveTouch.x = _lastKnownTouchX;
            effectiveTouch.y = _lastKnownTouchY;
        }

        if (!_pressActive && clicking) {
            _pressActive = true;
            _pressTouchId = effectiveTouch.id;
            _pressX = effectiveTouch.x;
            _pressY = effectiveTouch.y;
            _pressRegion = MapTouchToPressRegion(effectiveTouch);
            _pressCode = GetPressBinding(_pressRegion);

            if (_tracking) {
                _suppressSlide = true;
            }

            if (_pressRegion != TouchpadPressRegion::None) {
                PushTouchpadEvent(
                    PadEventType::TouchpadPress,
                    _pressCode,
                    state,
                    _pressTouchId,
                    _pressX,
                    _pressY,
                    _config.mode,
                    _pressRegion,
                    TouchpadSlideDirection::None,
                    outEvents);
            }
        }
        else if (_pressActive && !clicking) {
            if (_pressRegion != TouchpadPressRegion::None) {
                PushTouchpadEvent(
                    PadEventType::TouchpadRelease,
                    _pressCode,
                    state,
                    _pressTouchId,
                    _pressX,
                    _pressY,
                    _config.mode,
                    _pressRegion,
                    TouchpadSlideDirection::None,
                    outEvents);
            }

            _pressActive = false;
            _pressTouchId = 0;
            _pressX = 0;
            _pressY = 0;
            _pressRegion = TouchpadPressRegion::None;
            _pressCode = 0;
        }
    }

    void TouchpadMapper::GenerateSlideEvent(const PadState& state, PadEventBuffer& outEvents)
    {
        const auto& touch = state.touch1;
        const bool touching = touch.active;

        if (!_pressActive && touching && !_tracking) {
            _tracking = true;
            _trackingTouchId = touch.id;
            _startX = static_cast<int>(touch.x);
            _startY = static_cast<int>(touch.y);
            _lastX = _startX;
            _lastY = _startY;
            _suppressSlide = false;
            return;
        }

        if (_tracking && touching) {
            if (touch.id != _trackingTouchId) {
                _trackingTouchId = touch.id;
                _startX = static_cast<int>(touch.x);
                _startY = static_cast<int>(touch.y);
            }

            _lastX = static_cast<int>(touch.x);
            _lastY = static_cast<int>(touch.y);
            return;
        }

        if (_tracking && !touching) {
            const auto direction = _suppressSlide ? TouchpadSlideDirection::None : EvaluateSlide();
            const auto code = GetSlideBinding(direction);
            const auto touchId = _trackingTouchId;
            const auto touchX = static_cast<std::uint16_t>((std::clamp)(_lastX, 0, static_cast<int>(kTouchpadMaxX)));
            const auto touchY = static_cast<std::uint16_t>((std::clamp)(_lastY, 0, static_cast<int>(kTouchpadMaxY)));

            _tracking = false;
            _trackingTouchId = 0;
            _startX = 0;
            _startY = 0;
            _lastX = 0;
            _lastY = 0;
            _suppressSlide = false;

            if (direction != TouchpadSlideDirection::None) {
                PushTouchpadEvent(
                    PadEventType::TouchpadSlide,
                    code,
                    state,
                    touchId,
                    touchX,
                    touchY,
                    _config.mode,
                    TouchpadPressRegion::None,
                    direction,
                    outEvents);
            }
        }
    }

    TouchpadPressRegion TouchpadMapper::MapTouchToPressRegion(const TouchPointState& touch) const
    {
        switch (_config.mode) {
        case TouchpadMode::LeftCenterRight:
            if (!touch.active) {
                return TouchpadPressRegion::Center;
            }

            {
                const auto xNorm = NormalizeX(touch.x);
                const auto boundary = _config.leftRightBoundary;
                if (xNorm < boundary) {
                    return TouchpadPressRegion::Left;
                }
                if (xNorm > (1.0f - boundary)) {
                    return TouchpadPressRegion::Right;
                }
                return TouchpadPressRegion::Center;
            }

        case TouchpadMode::Edge:
            if (!touch.active) {
                return TouchpadPressRegion::None;
            }

            return MapTouchToEdge(NormalizeX(touch.x), NormalizeY(touch.y));

        case TouchpadMode::Whole:
            return TouchpadPressRegion::Whole;

        case TouchpadMode::Disabled:
        default:
            return TouchpadPressRegion::None;
        }
    }

    TouchpadPressRegion TouchpadMapper::MapTouchToEdge(float xNorm, float yNorm) const
    {
        const auto threshold = _config.edgeThreshold;

        TouchpadPressRegion bestRegion = TouchpadPressRegion::None;
        float bestDistance = std::numeric_limits<float>::max();

        if (yNorm <= threshold && yNorm < bestDistance) {
            bestDistance = yNorm;
            bestRegion = TouchpadPressRegion::TopEdge;
        }
        if ((1.0f - yNorm) <= threshold && (1.0f - yNorm) < bestDistance) {
            bestDistance = 1.0f - yNorm;
            bestRegion = TouchpadPressRegion::BottomEdge;
        }
        if (xNorm <= threshold && xNorm < bestDistance) {
            bestDistance = xNorm;
            bestRegion = TouchpadPressRegion::LeftEdge;
        }
        if ((1.0f - xNorm) <= threshold && (1.0f - xNorm) < bestDistance) {
            bestDistance = 1.0f - xNorm;
            bestRegion = TouchpadPressRegion::RightEdge;
        }

        return bestRegion;
    }

    TouchpadSlideDirection TouchpadMapper::EvaluateSlide() const
    {
        const auto dxNorm = static_cast<float>(_lastX - _startX) / kTouchpadMaxX;
        const auto dyNorm = static_cast<float>(_lastY - _startY) / kTouchpadMaxY;

        if (std::fabs(dxNorm) < _config.slideThreshold &&
            std::fabs(dyNorm) < _config.slideThreshold) {
            return TouchpadSlideDirection::None;
        }

        if (std::fabs(dxNorm) >= std::fabs(dyNorm)) {
            return dxNorm >= 0.0f ? TouchpadSlideDirection::Right : TouchpadSlideDirection::Left;
        }

        return dyNorm >= 0.0f ? TouchpadSlideDirection::Down : TouchpadSlideDirection::Up;
    }

    std::uint32_t TouchpadMapper::GetPressBinding(TouchpadPressRegion region) const
    {
        return _pressBindings[static_cast<std::size_t>(region)];
    }

    std::uint32_t TouchpadMapper::GetSlideBinding(TouchpadSlideDirection direction) const
    {
        return _slideBindings[static_cast<std::size_t>(direction)];
    }
}
