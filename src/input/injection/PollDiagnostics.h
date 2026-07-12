#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace dualpad::input
{
    enum class I0AvailabilitySampleReason : std::uint8_t
    {
        None = 0,
        First,
        StateChanged,
        IntervalElapsed,
        ClockRegressed
    };

    inline constexpr const char* ToString(I0AvailabilitySampleReason reason) noexcept
    {
        switch (reason) {
        case I0AvailabilitySampleReason::First: return "first";
        case I0AvailabilitySampleReason::StateChanged: return "state_changed";
        case I0AvailabilitySampleReason::IntervalElapsed: return "interval_elapsed";
        case I0AvailabilitySampleReason::ClockRegressed: return "clock_regressed";
        case I0AvailabilitySampleReason::None:
        default: return "none";
        }
    }

    struct I0AvailabilityFingerprintInput
    {
        std::uint32_t xinputResult{ 0 };
        std::uint32_t currentStateClass{ 0 };
        std::uint64_t gamepadSessionId{ 0 };
        std::uint32_t contextRevision{ 0 };
        std::uint32_t menuStackRevision{ 0 };
        std::uint16_t context{ 0 };
        std::uint8_t routeHealth{ 0 };
        bool remapMode{ false };
        bool connected{ false };
        bool delegateReady{ false };
    };

    inline constexpr std::uint32_t BuildI0AvailabilityStateClass(
        std::uint16_t buttons,
        std::int16_t lx,
        std::int16_t ly,
        std::int16_t rx,
        std::int16_t ry,
        std::uint8_t lt,
        std::uint8_t rt) noexcept
    {
        return static_cast<std::uint32_t>(buttons) |
            ((lx != 0 || ly != 0) ? (1U << 16) : 0U) |
            ((rx != 0 || ry != 0) ? (1U << 17) : 0U) |
            (lt != 0 ? (1U << 18) : 0U) |
            (rt != 0 ? (1U << 19) : 0U);
    }

    inline constexpr std::uint64_t BuildI0AvailabilityFingerprint(
        const I0AvailabilityFingerprintInput& input) noexcept
    {
        std::uint64_t hash = 14695981039346656037ull;
        const auto mix = [&hash](std::uint64_t value, std::size_t byteCount) {
            for (std::size_t byte = 0; byte < byteCount; ++byte) {
                hash ^= (value >> (byte * 8)) & 0xFF;
                hash *= 1099511628211ull;
            }
        };
        mix(input.xinputResult, sizeof(input.xinputResult));
        mix(input.currentStateClass, sizeof(input.currentStateClass));
        mix(input.gamepadSessionId, sizeof(input.gamepadSessionId));
        mix(input.contextRevision, sizeof(input.contextRevision));
        mix(input.menuStackRevision, sizeof(input.menuStackRevision));
        mix(input.context, sizeof(input.context));
        mix(input.routeHealth, sizeof(input.routeHealth));
        mix(input.remapMode ? 1 : 0, sizeof(input.remapMode));
        mix(input.connected ? 1 : 0, sizeof(input.connected));
        mix(input.delegateReady ? 1 : 0, sizeof(input.delegateReady));
        return hash;
    }

    struct I0AvailabilitySampleDecision
    {
        std::uint64_t pollCount{ 0 };
        I0AvailabilitySampleReason reason{ I0AvailabilitySampleReason::None };
        bool record{ false };
    };

    class I0AvailabilitySampler
    {
    public:
        explicit I0AvailabilitySampler(std::uint64_t healthIntervalMs) noexcept :
            _healthIntervalMs(healthIntervalMs)
        {}

        [[nodiscard]] I0AvailabilitySampleDecision Observe(
            std::uint64_t nowMs,
            std::uint64_t fingerprint) noexcept
        {
            const auto pollCount = _pollCount.fetch_add(1, std::memory_order_relaxed) + 1;
            const auto previousFingerprint =
                _lastFingerprint.exchange(fingerprint, std::memory_order_acq_rel);
            const auto previousObservedMs =
                _lastObservedMs.exchange(nowMs, std::memory_order_acq_rel);
            const auto lastRecordMs = _lastRecordMs.load(std::memory_order_acquire);

            auto reason = I0AvailabilitySampleReason::None;
            if (pollCount == 1) {
                reason = I0AvailabilitySampleReason::First;
            } else if (previousFingerprint != fingerprint) {
                reason = I0AvailabilitySampleReason::StateChanged;
            } else if (nowMs < previousObservedMs) {
                reason = I0AvailabilitySampleReason::ClockRegressed;
            } else if (_healthIntervalMs != 0 &&
                nowMs - lastRecordMs >= _healthIntervalMs) {
                reason = I0AvailabilitySampleReason::IntervalElapsed;
            }

            if (reason == I0AvailabilitySampleReason::None) {
                return { .pollCount = pollCount };
            }

            if (reason == I0AvailabilitySampleReason::IntervalElapsed) {
                auto expected = lastRecordMs;
                if (!_lastRecordMs.compare_exchange_strong(
                        expected,
                        nowMs,
                        std::memory_order_acq_rel,
                        std::memory_order_acquire)) {
                    return { .pollCount = pollCount };
                }
            } else {
                _lastRecordMs.store(nowMs, std::memory_order_release);
            }
            return { .pollCount = pollCount, .reason = reason, .record = true };
        }

    private:
        const std::uint64_t _healthIntervalMs;
        std::atomic<std::uint64_t> _pollCount{ 0 };
        std::atomic<std::uint64_t> _lastFingerprint{ 0 };
        std::atomic<std::uint64_t> _lastObservedMs{ 0 };
        std::atomic<std::uint64_t> _lastRecordMs{ 0 };
    };

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
