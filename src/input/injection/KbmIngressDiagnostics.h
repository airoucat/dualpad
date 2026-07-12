#pragma once

#include <cstdint>
#include <mutex>

namespace dualpad::input
{
    enum class KbmIngressSampleReason : std::uint8_t
    {
        None = 0,
        First,
        StateChanged,
        IntervalElapsed,
        ClockRegressed,
        BatchRejected
    };

    inline constexpr const char* ToString(KbmIngressSampleReason reason) noexcept
    {
        switch (reason) {
        case KbmIngressSampleReason::First: return "first";
        case KbmIngressSampleReason::StateChanged: return "state_changed";
        case KbmIngressSampleReason::IntervalElapsed: return "interval_elapsed";
        case KbmIngressSampleReason::ClockRegressed: return "clock_regressed";
        case KbmIngressSampleReason::BatchRejected: return "batch_rejected";
        case KbmIngressSampleReason::None:
        default:
            return "none";
        }
    }

    struct KbmIngressDiagnosticInput
    {
        std::uint32_t contextRevision{ 0 };
        std::uint32_t menuStackRevision{ 0 };
        std::uint32_t controlMapRevision{ 0 };
        std::uint64_t bindingGeneration{ 0 };
        std::uint32_t observedEventCount{ 0 };
        std::uint32_t mappedEdgeCount{ 0 };
        std::uint32_t sourceActivityCount{ 0 };
        std::uint32_t physicalDownCount{ 0 };
        std::uint32_t quarantineCount{ 0 };
        std::uint32_t keyboardMoveHeldMask{ 0 };
        std::uint32_t keyboardCombatHeldMask{ 0 };
        std::uint32_t mouseCombatHeldMask{ 0 };
        std::uint32_t keyboardTransientHeldMask{ 0 };
        std::uint32_t mouseTransientHeldMask{ 0 };
        std::uint32_t keyboardSustainedHeldMask{ 0 };
        std::uint32_t mouseSustainedHeldMask{ 0 };
        std::uint32_t firstIdCode{ 0 };
        std::uint8_t firstDevice{ 0 };
        std::uint8_t firstPhase{ 0 };
        bool firstInitialPress{ false };
        bool bindingsComplete{ false };
        bool eventListComplete{ false };
        bool batchBuilt{ false };
        bool batchAccepted{ false };
    };

    inline std::uint64_t BuildKbmIngressFingerprint(
        const KbmIngressDiagnosticInput& input) noexcept
    {
        std::uint64_t hash = 1469598103934665603ull;
        const auto append = [&](std::uint64_t value, std::uint8_t bytes) {
            for (std::uint8_t index = 0; index < bytes; ++index) {
                hash ^= static_cast<std::uint8_t>(value >> (index * 8));
                hash *= 1099511628211ull;
            }
        };
        append(input.contextRevision, 4);
        append(input.menuStackRevision, 4);
        append(input.controlMapRevision, 4);
        append(input.bindingGeneration, 8);
        append(input.observedEventCount, 4);
        append(input.mappedEdgeCount, 4);
        append(input.sourceActivityCount, 4);
        append(input.physicalDownCount, 4);
        append(input.quarantineCount, 4);
        append(input.keyboardMoveHeldMask, 4);
        append(input.keyboardCombatHeldMask, 4);
        append(input.mouseCombatHeldMask, 4);
        append(input.keyboardTransientHeldMask, 4);
        append(input.mouseTransientHeldMask, 4);
        append(input.keyboardSustainedHeldMask, 4);
        append(input.mouseSustainedHeldMask, 4);
        append(input.firstIdCode, 4);
        append(input.firstDevice, 1);
        append(input.firstPhase, 1);
        append(input.firstInitialPress, 1);
        append(input.bindingsComplete, 1);
        append(input.eventListComplete, 1);
        append(input.batchBuilt, 1);
        append(input.batchAccepted, 1);
        return hash;
    }

    struct KbmIngressSampleDecision
    {
        bool record{ false };
        KbmIngressSampleReason reason{ KbmIngressSampleReason::None };
        std::uint64_t callbackCount{ 0 };
    };

    class KbmIngressDiagnosticSampler
    {
    public:
        explicit KbmIngressDiagnosticSampler(std::uint64_t intervalMs) noexcept :
            _intervalMs(intervalMs)
        {}

        KbmIngressSampleDecision Observe(
            std::uint64_t nowMs,
            const KbmIngressDiagnosticInput& input) noexcept
        {
            std::scoped_lock lock(_mutex);
            const auto callbackCount = ++_callbackCount;
            const auto fingerprint = BuildKbmIngressFingerprint(input);

            if (!_initialized) {
                _initialized = true;
                _lastFingerprint = fingerprint;
                _lastObservedMs = nowMs;
                _lastRecordedMs = nowMs;
                return { true, KbmIngressSampleReason::First, callbackCount };
            }

            const bool clockRegressed = nowMs < _lastObservedMs;
            _lastObservedMs = nowMs;
            const bool stateChanged = fingerprint != _lastFingerprint;
            _lastFingerprint = fingerprint;

            KbmIngressSampleReason reason = KbmIngressSampleReason::None;
            if (clockRegressed) {
                reason = KbmIngressSampleReason::ClockRegressed;
            } else if (stateChanged) {
                reason = input.batchBuilt && !input.batchAccepted ?
                    KbmIngressSampleReason::BatchRejected :
                    KbmIngressSampleReason::StateChanged;
            } else if (nowMs - _lastRecordedMs >= _intervalMs) {
                reason = KbmIngressSampleReason::IntervalElapsed;
            }

            if (reason == KbmIngressSampleReason::None) {
                return { false, reason, callbackCount };
            }
            _lastRecordedMs = nowMs;
            return { true, reason, callbackCount };
        }

    private:
        std::mutex _mutex;
        std::uint64_t _intervalMs{ 0 };
        std::uint64_t _callbackCount{ 0 };
        std::uint64_t _lastFingerprint{ 0 };
        std::uint64_t _lastObservedMs{ 0 };
        std::uint64_t _lastRecordedMs{ 0 };
        bool _initialized{ false };
    };
}
