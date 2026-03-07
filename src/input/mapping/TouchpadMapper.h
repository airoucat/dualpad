#pragma once

#include "input/mapping/PadEvent.h"
#include "input/state/PadState.h"

#include <array>
#include <cstdint>

namespace dualpad::input
{
    struct TouchpadConfig
    {
        TouchpadMode mode{ TouchpadMode::LeftCenterRight };
        float edgeThreshold{ 0.15f };
        float leftRightBoundary{ 1.0f / 3.0f };
        float slideThreshold{ 0.18f };
    };

    class TouchpadMapper
    {
    public:
        TouchpadMapper();

        void Reset();

        void SetMode(TouchpadMode mode);
        TouchpadMode GetMode() const;

        void SetConfig(const TouchpadConfig& config);
        const TouchpadConfig& GetConfig() const;

        void SetPressBinding(TouchpadPressRegion region, std::uint32_t code);
        void SetSlideBinding(TouchpadSlideDirection direction, std::uint32_t code);

        void ProcessTouch(const PadState& state, PadEventBuffer& outEvents);

    private:
        TouchpadConfig _config{};
        std::array<std::uint32_t, 9> _pressBindings{};
        std::array<std::uint32_t, 5> _slideBindings{};

        bool _pressActive{ false };
        std::uint8_t _pressTouchId{ 0 };
        std::uint16_t _pressX{ 0 };
        std::uint16_t _pressY{ 0 };
        TouchpadPressRegion _pressRegion{ TouchpadPressRegion::None };
        std::uint32_t _pressCode{ 0 };

        bool _tracking{ false };
        std::uint8_t _trackingTouchId{ 0 };
        int _startX{ 0 };
        int _startY{ 0 };
        int _lastX{ 0 };
        int _lastY{ 0 };
        bool _suppressSlide{ false };

        void GeneratePressEvent(const PadState& state, PadEventBuffer& outEvents);
        void GenerateSlideEvent(const PadState& state, PadEventBuffer& outEvents);

        TouchpadPressRegion MapTouchToPressRegion(const TouchPointState& touch) const;
        TouchpadPressRegion MapTouchToEdge(float xNorm, float yNorm) const;
        TouchpadSlideDirection EvaluateSlide() const;

        std::uint32_t GetPressBinding(TouchpadPressRegion region) const;
        std::uint32_t GetSlideBinding(TouchpadSlideDirection direction) const;
    };
}
