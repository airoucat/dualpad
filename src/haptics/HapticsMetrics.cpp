#include "pch.h"
#include "haptics/HapticsMetrics.h"

#include <algorithm>

namespace dualpad::haptics
{
    HapticsMetrics& HapticsMetrics::GetSingleton()
    {
        static HapticsMetrics s;
        return s;
    }

    void HapticsMetrics::Reset()
    {
        for (auto& b : _baseBins) {
            b.store(0, std::memory_order_relaxed);
        }
        for (auto& b : _audioBins) {
            b.store(0, std::memory_order_relaxed);
        }

        _baseCount.store(0, std::memory_order_relaxed);
        _audioCount.store(0, std::memory_order_relaxed);

        _totalEvents.store(0, std::memory_order_relaxed);
        _totalMatched.store(0, std::memory_order_relaxed);
        _totalUnmatched.store(0, std::memory_order_relaxed);

        _eventQueueSize.store(0, std::memory_order_relaxed);
        _eventQueueCap.store(0, std::memory_order_relaxed);
        _voiceQueueSize.store(0, std::memory_order_relaxed);
        _voiceQueueCap.store(0, std::memory_order_relaxed);
    }

    std::size_t HapticsMetrics::ToLatencyBin(std::uint64_t us)
    {
        const std::uint64_t ms = us / 1000ULL;
        return static_cast<std::size_t>(std::min<std::uint64_t>(100ULL, ms));
    }

    void HapticsMetrics::OnSourceAdded(const HapticSourceMsg& msg, std::uint64_t nowUs)
    {
        if (msg.qpc == 0 || nowUs < msg.qpc) {
            return;
        }

        const auto dtUs = nowUs - msg.qpc;
        const auto bin = ToLatencyBin(dtUs);

        if (msg.type == SourceType::BaseEvent) {
            _baseBins[bin].fetch_add(1, std::memory_order_relaxed);
            _baseCount.fetch_add(1, std::memory_order_relaxed);
        }
        else if (msg.type == SourceType::AudioMod) {
            _audioBins[bin].fetch_add(1, std::memory_order_relaxed);
            _audioCount.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void HapticsMetrics::OnScorerStats(const EventWindowScorer::Stats& s)
    {
        _totalEvents.store(s.totalEvents, std::memory_order_relaxed);
        _totalMatched.store(s.totalMatched, std::memory_order_relaxed);
        _totalUnmatched.store(s.totalUnmatched, std::memory_order_relaxed);
    }

    void HapticsMetrics::OnQueueStats(std::size_t eventSz, std::size_t eventCap, std::size_t voiceSz, std::size_t voiceCap)
    {
        _eventQueueSize.store(eventSz, std::memory_order_relaxed);
        _eventQueueCap.store(eventCap, std::memory_order_relaxed);
        _voiceQueueSize.store(voiceSz, std::memory_order_relaxed);
        _voiceQueueCap.store(voiceCap, std::memory_order_relaxed);
    }

    float HapticsMetrics::PercentileFromBins(
        const std::array<std::atomic<std::uint32_t>, kBins>& bins,
        float p,
        std::uint64_t total)
    {
        if (total == 0) {
            return 0.0f;
        }

        const auto rank = static_cast<std::uint64_t>(std::ceil(static_cast<double>(total) * p));
        std::uint64_t acc = 0;

        for (std::size_t i = 0; i < kBins; ++i) {
            acc += bins[i].load(std::memory_order_relaxed);
            if (acc >= rank) {
                return static_cast<float>(i); // ms
            }
        }
        return 100.0f;
    }

    HapticsMetrics::Snapshot HapticsMetrics::GetSnapshot() const
    {
        Snapshot s{};
        s.baseCount = _baseCount.load(std::memory_order_relaxed);
        s.audioModCount = _audioCount.load(std::memory_order_relaxed);

        s.baseP50Ms = PercentileFromBins(_baseBins, 0.50f, s.baseCount);
        s.baseP95Ms = PercentileFromBins(_baseBins, 0.95f, s.baseCount);

        s.audioModP50Ms = PercentileFromBins(_audioBins, 0.50f, s.audioModCount);
        s.audioModP95Ms = PercentileFromBins(_audioBins, 0.95f, s.audioModCount);

        const auto total = _totalEvents.load(std::memory_order_relaxed);
        const auto matched = _totalMatched.load(std::memory_order_relaxed);
        const auto unmatched = _totalUnmatched.load(std::memory_order_relaxed);

        if (total > 0) {
            s.matchRate = static_cast<float>(matched) / static_cast<float>(total);
            s.fallbackRate = static_cast<float>(unmatched) / static_cast<float>(total);
        }

        s.eventQueueSize = _eventQueueSize.load(std::memory_order_relaxed);
        s.eventQueueCap = _eventQueueCap.load(std::memory_order_relaxed);
        s.voiceQueueSize = _voiceQueueSize.load(std::memory_order_relaxed);
        s.voiceQueueCap = _voiceQueueCap.load(std::memory_order_relaxed);

        return s;
    }
}