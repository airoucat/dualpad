#pragma once
#include "input/InputActions.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace dualpad::input
{
    struct TriggerEvent
    {
        TriggerCode code{ TriggerCode::None };
        TriggerPhase phase{ TriggerPhase::Press };
    };

    struct AnalogSample
    {
        float lx{ 0.0f };
        float ly{ 0.0f };
        float rx{ 0.0f };
        float ry{ 0.0f };
        float l2{ 0.0f };
        float r2{ 0.0f };
    };

    class InputIngress
    {
    public:
        static InputIngress& GetSingleton();

        // HID thread
        void PushTrigger(TriggerCode code, TriggerPhase phase);
        void PushAnalog(const AnalogSample& s);

        // Main thread
        void DrainTriggers(std::vector<TriggerEvent>& out);
        bool ReadLatestAnalog(AnalogSample& out) const;

        void Reset();

    private:
        InputIngress() = default;

        // ---------- SPSC ring for trigger events ----------
        static constexpr std::size_t kCap = 2048; // power-of-two
        static constexpr std::size_t kMask = kCap - 1;

        std::array<TriggerEvent, kCap> _ring{};
        std::atomic<std::uint32_t> _head{ 0 }; // producer writes
        std::atomic<std::uint32_t> _tail{ 0 }; // consumer writes
        std::atomic<std::uint64_t> _dropCount{ 0 };

        // ---------- latest analog with seqlock ----------
        mutable std::atomic<std::uint64_t> _aseq{ 0 }; // even=stable, odd=writing
        AnalogSample _analog{};
    };
}