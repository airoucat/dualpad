#include "pch.h"
#include "input/SyntheticPadState.h"
#include <chrono>

namespace dualpad::input
{
    namespace
    {
        inline std::uint64_t NowMs()
        {
            using namespace std::chrono;
            return duration_cast<milliseconds>(
                steady_clock::now().time_since_epoch()).count();
        }
    }

    SyntheticPadState& SyntheticPadState::GetSingleton()
    {
        static SyntheticPadState s;
        return s;
    }

    void SyntheticPadState::SetButton(std::uint32_t bit, bool down)
    {
        if (!bit) return;

        if (down) {
            _down.fetch_or(bit, std::memory_order_acq_rel);
        }
        else {
            _down.fetch_and(~bit, std::memory_order_acq_rel);
        }
    }

    void SyntheticPadState::PulseButton(std::uint32_t bit)
    {
        if (!bit) return;

        _down.fetch_or(bit, std::memory_order_acq_rel);
        _pulseExpireMs.store(NowMs() + 50, std::memory_order_release);
    }

    void SyntheticPadState::SetAxis(float lx, float ly, float rx, float ry, float l2, float r2)
    {
        _lx.store(lx, std::memory_order_release);
        _ly.store(ly, std::memory_order_release);
        _rx.store(rx, std::memory_order_release);
        _ry.store(ry, std::memory_order_release);
        _l2.store(l2, std::memory_order_release);
        _r2.store(r2, std::memory_order_release);
        _hasAxis.store(true, std::memory_order_release);
    }

    SyntheticFrame SyntheticPadState::ConsumeFrame()
    {
        SyntheticFrame f{};

        // 检查 pulse 是否过期
        const auto expireMs = _pulseExpireMs.load(std::memory_order_acquire);
        if (expireMs > 0 && NowMs() >= expireMs) {
            _down.store(0, std::memory_order_release);
            _pulseExpireMs.store(0, std::memory_order_release);
        }

        f.downMask = _down.load(std::memory_order_acquire);

        f.hasAxis = _hasAxis.load(std::memory_order_acquire);
        if (f.hasAxis) {
            f.lx = _lx.load(std::memory_order_acquire);
            f.ly = _ly.load(std::memory_order_acquire);
            f.rx = _rx.load(std::memory_order_acquire);
            f.ry = _ry.load(std::memory_order_acquire);
            f.l2 = _l2.load(std::memory_order_acquire);
            f.r2 = _r2.load(std::memory_order_acquire);
        }

        return f;
    }
}