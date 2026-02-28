#pragma once
#include <atomic>
#include <cstdint>

namespace dualpad::input
{
    struct SyntheticFrame
    {
        std::uint32_t downMask{ 0 };

        float lx{ 0.0f };
        float ly{ 0.0f };
        float rx{ 0.0f };
        float ry{ 0.0f };
        float l2{ 0.0f };
        float r2{ 0.0f };
        bool hasAxis{ false };
    };

    class SyntheticPadState
    {
    public:
        static SyntheticPadState& GetSingleton();

        void SetButton(std::uint32_t bit, bool down);
        void PulseButton(std::uint32_t bit);  // ³ÖÐø 50ms

        void SetAxis(float lx, float ly, float rx, float ry, float l2, float r2);

        SyntheticFrame ConsumeFrame();

    private:
        std::atomic<std::uint32_t> _down{ 0 };
        std::atomic<std::uint64_t> _pulseExpireMs{ 0 };

        std::atomic<float> _lx{ 0 }, _ly{ 0 }, _rx{ 0 }, _ry{ 0 }, _l2{ 0 }, _r2{ 0 };
        std::atomic_bool _hasAxis{ false };
    };
}