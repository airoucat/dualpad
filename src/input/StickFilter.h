#pragma once
#include <cstdint>

namespace dualpad::input::stick
{
    struct Vec2
    {
        float x{ 0.0f };
        float y{ 0.0f };
    };

    enum class CurveType
    {
        Linear,
        Power
    };

    struct Config
    {
        float deadzone{ 0.08f };
        float antiDeadzone{ 0.02f };
        float maxzone{ 1.0f };
        float expo{ 1.6f };         // Power curve exponent
        float smoothAlpha{ 0.35f }; // 1.0 = no smoothing
        float epsilon{ 0.002f };
        bool invertX{ false };
        bool invertY{ false };
        CurveType curve{ CurveType::Power };
    };

    class Filter
    {
    public:
        Filter() = default;
        explicit Filter(const Config& cfg);

        void SetConfig(const Config& cfg);
        void Reset();

        Vec2 ProcessRawU8(std::uint8_t rawX, std::uint8_t rawY);

    private:
        Config _cfg{};
        Vec2 _prev{};

        static float Clamp(float v, float lo, float hi);
        static float NormalizeU8(std::uint8_t v); // [0,255] -> [-1,1]
    };
}