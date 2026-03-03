#pragma once
#include "haptics/HapticsTypes.h"
#include "haptics/EventWindowScorer.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace dualpad::haptics
{
    class HapticsMetrics
    {
    public:
        struct Snapshot
        {
            std::uint64_t baseCount{ 0 };
            std::uint64_t audioModCount{ 0 };

            float baseP50Ms{ 0.0f };
            float baseP95Ms{ 0.0f };

            float audioModP50Ms{ 0.0f };
            float audioModP95Ms{ 0.0f };

            float matchRate{ 0.0f };     // matched / totalEvents
            float fallbackRate{ 0.0f };  // unmatched / totalEvents

            std::size_t eventQueueSize{ 0 };
            std::size_t eventQueueCap{ 0 };
            std::size_t voiceQueueSize{ 0 };
            std::size_t voiceQueueCap{ 0 };
        };

        static HapticsMetrics& GetSingleton();

        void Reset();

        // 在 AddSource 时调用
        void OnSourceAdded(const HapticSourceMsg& msg, std::uint64_t nowUs);

        // 每秒调用一次
        void OnScorerStats(const EventWindowScorer::Stats& s);
        void OnQueueStats(std::size_t eventSz, std::size_t eventCap, std::size_t voiceSz, std::size_t voiceCap);

        Snapshot GetSnapshot() const;

    private:
        HapticsMetrics() = default;

        static constexpr std::size_t kBins = 101; // 0..100ms, 100表示>=100ms
        static std::size_t ToLatencyBin(std::uint64_t us);
        static float PercentileFromBins(const std::array<std::atomic<std::uint32_t>, kBins>& bins, float p, std::uint64_t total);

        std::array<std::atomic<std::uint32_t>, kBins> _baseBins{};
        std::array<std::atomic<std::uint32_t>, kBins> _audioBins{};

        std::atomic<std::uint64_t> _baseCount{ 0 };
        std::atomic<std::uint64_t> _audioCount{ 0 };

        std::atomic<std::uint64_t> _totalEvents{ 0 };
        std::atomic<std::uint64_t> _totalMatched{ 0 };
        std::atomic<std::uint64_t> _totalUnmatched{ 0 };

        std::atomic<std::size_t> _eventQueueSize{ 0 };
        std::atomic<std::size_t> _eventQueueCap{ 0 };
        std::atomic<std::size_t> _voiceQueueSize{ 0 };
        std::atomic<std::size_t> _voiceQueueCap{ 0 };
    };
}