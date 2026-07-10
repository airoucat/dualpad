#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace dualpad::input
{
    struct PollDiagnosticReservation
    {
        std::uint64_t sequence{ 0 };
        std::uint32_t inFlight{ 0 };
        bool record{ false };
    };

    class PollDiagnosticLimiter
    {
    public:
        explicit PollDiagnosticLimiter(std::uint64_t capacity) noexcept :
            _capacity(capacity)
        {}

        [[nodiscard]] PollDiagnosticReservation Begin(bool enabled) noexcept
        {
            if (!enabled) {
                return {};
            }

            const auto sequence = _sequence.fetch_add(1, std::memory_order_relaxed) + 1;
            const auto inFlight = _inFlight.fetch_add(1, std::memory_order_acq_rel) + 1;
            const bool record = sequence <= _capacity;
            if (!record) {
                _dropped.fetch_add(1, std::memory_order_relaxed);
            }
            return {
                .sequence = sequence,
                .inFlight = inFlight,
                .record = record
            };
        }

        [[nodiscard]] std::uint32_t End(bool enabled) noexcept
        {
            if (!enabled) {
                return _inFlight.load(std::memory_order_acquire);
            }

            auto current = _inFlight.load(std::memory_order_acquire);
            while (current != 0) {
                if (_inFlight.compare_exchange_weak(
                        current,
                        current - 1,
                        std::memory_order_acq_rel,
                        std::memory_order_acquire)) {
                    return current - 1;
                }
            }
            return 0;
        }

        [[nodiscard]] std::uint32_t InFlight() const noexcept
        {
            return _inFlight.load(std::memory_order_acquire);
        }

        [[nodiscard]] std::uint64_t Dropped() const noexcept
        {
            return _dropped.load(std::memory_order_relaxed);
        }

    private:
        const std::uint64_t _capacity;
        std::atomic<std::uint64_t> _sequence{ 0 };
        std::atomic<std::uint64_t> _dropped{ 0 };
        std::atomic<std::uint32_t> _inFlight{ 0 };
    };
}
