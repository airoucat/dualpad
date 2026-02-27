#include "pch.h"
#include "input/InputIngress.h"

namespace dualpad::input
{
    InputIngress& InputIngress::GetSingleton()
    {
        static InputIngress s;
        return s;
    }

    void InputIngress::PushTrigger(TriggerCode code, TriggerPhase phase)
    {
        if (code == TriggerCode::None) {
            return;
        }

        const auto head = _head.load(std::memory_order_relaxed);
        const auto next = (head + 1) & static_cast<std::uint32_t>(kMask);
        const auto tail = _tail.load(std::memory_order_acquire);

        if (next == tail) {
            _dropCount.fetch_add(1, std::memory_order_relaxed); // full
            return;
        }

        _ring[head] = TriggerEvent{ code, phase };
        _head.store(next, std::memory_order_release);
    }

    void InputIngress::DrainTriggers(std::vector<TriggerEvent>& out)
    {
        out.clear();

        auto tail = _tail.load(std::memory_order_relaxed);
        const auto head = _head.load(std::memory_order_acquire);

        while (tail != head) {
            out.push_back(_ring[tail]);
            tail = (tail + 1) & static_cast<std::uint32_t>(kMask);
        }

        _tail.store(tail, std::memory_order_release);
    }

    void InputIngress::PushAnalog(const AnalogSample& s)
    {
        auto seq = _aseq.load(std::memory_order_relaxed);
        _aseq.store(seq + 1, std::memory_order_release); // odd => writing
        _analog = s;
        _aseq.store(seq + 2, std::memory_order_release); // even => stable
    }

    bool InputIngress::ReadLatestAnalog(AnalogSample& out) const
    {
        for (int i = 0; i < 4; ++i) {
            const auto a = _aseq.load(std::memory_order_acquire);
            if (a & 1) {
                continue;
            }

            out = _analog;

            const auto b = _aseq.load(std::memory_order_acquire);
            if (a == b && !(b & 1)) {
                return b != 0;
            }
        }
        return false;
    }

    void InputIngress::Reset()
    {
        _head.store(0, std::memory_order_relaxed);
        _tail.store(0, std::memory_order_relaxed);
        _dropCount.store(0, std::memory_order_relaxed);

        _aseq.store(1, std::memory_order_release);
        _analog = {};
        _aseq.store(2, std::memory_order_release);
    }
}