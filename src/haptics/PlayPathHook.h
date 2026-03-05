#pragma once

#include <atomic>
#include <cstdint>
#include <xaudio2.h>

namespace dualpad::haptics
{
    class PlayPathHook
    {
    public:
        struct Stats
        {
            std::uint64_t initCalls{ 0 };
            std::uint64_t initResolved{ 0 };
            std::uint64_t initNoForm{ 0 };

            std::uint64_t submitCalls{ 0 };
            std::uint64_t submitResolved{ 0 };
            std::uint64_t submitNoContext{ 0 };
            std::uint64_t submitNoForm{ 0 };
            std::uint64_t submitSkipResolved{ 0 };
            std::uint64_t submitRetryScans{ 0 };
            std::uint64_t submitSkipRateLimit{ 0 };
            std::uint64_t submitSkipMaxAttempts{ 0 };
            std::uint64_t submitTraceMetaHit{ 0 };
            std::uint64_t submitTraceMetaMiss{ 0 };
            std::uint64_t submitScanExecuted{ 0 };
            std::uint64_t submitResolvedOnRetry{ 0 };
            std::uint64_t submitNoFormFirstScan{ 0 };
            std::uint64_t submitNoFormRetry{ 0 };
            std::uint64_t submitNoContextScan{ 0 };
            std::uint64_t submitNoContextResolved{ 0 };
            std::uint64_t submitNoContextNoInitPtr{ 0 };
            std::uint64_t submitNoContextDeepScan{ 0 };
            std::uint64_t submitNoContextDeepResolved{ 0 };
            std::uint64_t submitSkipResolvedFromInit{ 0 };
            std::uint64_t submitSkipResolvedFromSubmit{ 0 };
            std::uint64_t submitSkipResolvedOther{ 0 };

            std::uint64_t bindingMisses{ 0 };
            std::uint64_t traceUpserts{ 0 };
        };

        static PlayPathHook& GetSingleton();

        // Called from init-sound hook: best-effort form semantic hydration.
        void OnInitSoundObject(
            std::uintptr_t initSoundObject,
            std::uintptr_t voicePtr,
            std::uint64_t instanceId,
            std::uint64_t nowUs);

        // Called from submit hook: fallback hydration from XAUDIO2_BUFFER::pContext.
        void OnSubmitContext(
            IXAudio2SourceVoice* voice,
            const XAUDIO2_BUFFER* buffer,
            std::uint64_t nowUs);

        Stats GetStats() const;
        void ResetStats();

    private:
        PlayPathHook() = default;

        struct Counters
        {
            std::atomic<std::uint64_t> initCalls{ 0 };
            std::atomic<std::uint64_t> initResolved{ 0 };
            std::atomic<std::uint64_t> initNoForm{ 0 };

            std::atomic<std::uint64_t> submitCalls{ 0 };
            std::atomic<std::uint64_t> submitResolved{ 0 };
            std::atomic<std::uint64_t> submitNoContext{ 0 };
            std::atomic<std::uint64_t> submitNoForm{ 0 };
            std::atomic<std::uint64_t> submitSkipResolved{ 0 };
            std::atomic<std::uint64_t> submitRetryScans{ 0 };
            std::atomic<std::uint64_t> submitSkipRateLimit{ 0 };
            std::atomic<std::uint64_t> submitSkipMaxAttempts{ 0 };
            std::atomic<std::uint64_t> submitTraceMetaHit{ 0 };
            std::atomic<std::uint64_t> submitTraceMetaMiss{ 0 };
            std::atomic<std::uint64_t> submitScanExecuted{ 0 };
            std::atomic<std::uint64_t> submitResolvedOnRetry{ 0 };
            std::atomic<std::uint64_t> submitNoFormFirstScan{ 0 };
            std::atomic<std::uint64_t> submitNoFormRetry{ 0 };
            std::atomic<std::uint64_t> submitNoContextScan{ 0 };
            std::atomic<std::uint64_t> submitNoContextResolved{ 0 };
            std::atomic<std::uint64_t> submitNoContextNoInitPtr{ 0 };
            std::atomic<std::uint64_t> submitNoContextDeepScan{ 0 };
            std::atomic<std::uint64_t> submitNoContextDeepResolved{ 0 };
            std::atomic<std::uint64_t> submitSkipResolvedFromInit{ 0 };
            std::atomic<std::uint64_t> submitSkipResolvedFromSubmit{ 0 };
            std::atomic<std::uint64_t> submitSkipResolvedOther{ 0 };

            std::atomic<std::uint64_t> bindingMisses{ 0 };
            std::atomic<std::uint64_t> traceUpserts{ 0 };
        };

        Counters _counters;
    };
}
