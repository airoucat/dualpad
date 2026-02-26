#include "pch.h"
#include "input/StickFilter.h"

#include <cmath>
#include <algorithm>

// ∑¿÷π Windows.h µƒ min/max ∫ÍŒ€»æ std::min/std::max
#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

namespace dualpad::input::stick
{
    static constexpr float kEps = 1e-6f;

    Filter::Filter(const Config& cfg)
    {
        SetConfig(cfg);
    }

    void Filter::SetConfig(const Config& cfg)
    {
        _cfg = cfg;
        _cfg.deadzone = Clamp(_cfg.deadzone, 0.0f, 0.95f);
        _cfg.antiDeadzone = Clamp(_cfg.antiDeadzone, 0.0f, 0.5f);
        _cfg.maxzone = Clamp(_cfg.maxzone, _cfg.deadzone + 1e-4f, 1.5f);
        _cfg.expo = Clamp(_cfg.expo, 0.1f, 6.0f);
        _cfg.smoothAlpha = Clamp(_cfg.smoothAlpha, 0.0f, 1.0f);
        _cfg.epsilon = Clamp(_cfg.epsilon, 0.0f, 0.1f);
    }

    void Filter::Reset()
    {
        _prev = {};
    }

    float Filter::Clamp(float v, float lo, float hi)
    {
        return (v < lo) ? lo : ((v > hi) ? hi : v);
    }

    float Filter::NormalizeU8(std::uint8_t v)
    {
        return Clamp((static_cast<float>(v) - 127.5f) / 127.5f, -1.0f, 1.0f);
    }

    Vec2 Filter::ProcessRawU8(std::uint8_t rawX, std::uint8_t rawY)
    {
        float x = NormalizeU8(rawX);
        float y = NormalizeU8(rawY);

        if (_cfg.invertX) x = -x;
        if (_cfg.invertY) y = -y;

        const float mag = std::sqrt(x * x + y * y);
        Vec2 target{};

        if (mag > _cfg.deadzone) {
            const float denom = (_cfg.maxzone - _cfg.deadzone > kEps) ? (_cfg.maxzone - _cfg.deadzone) : kEps;
            float t = (mag - _cfg.deadzone) / denom;
            t = Clamp(t, 0.0f, 1.0f);

            if (t > 0.0f) {
                t = _cfg.antiDeadzone + (1.0f - _cfg.antiDeadzone) * t;
                t = Clamp(t, 0.0f, 1.0f);
            }

            if (_cfg.curve == CurveType::Power) {
                t = std::pow(t, _cfg.expo);
            }

            const float inv = (mag > kEps) ? (1.0f / mag) : 0.0f;
            target.x = x * inv * t;
            target.y = y * inv * t;
        }

        const float a = _cfg.smoothAlpha;
        Vec2 out;
        out.x = _prev.x + a * (target.x - _prev.x);
        out.y = _prev.y + a * (target.y - _prev.y);

        if (std::fabs(out.x) < _cfg.epsilon) out.x = 0.0f;
        if (std::fabs(out.y) < _cfg.epsilon) out.y = 0.0f;

        out.x = Clamp(out.x, -1.0f, 1.0f);
        out.y = Clamp(out.y, -1.0f, 1.0f);

        _prev = out;
        return out;
    }
}