#include "pch.h"
#include "haptics/MetricsReporter.h"

#include <algorithm>

namespace dualpad::haptics
{
    namespace
    {
        std::uint32_t PercentileFromSorted(const std::vector<std::uint32_t>& sorted, float p)
        {
            if (sorted.empty()) {
                return 0;
            }
            const float clamped = std::clamp(p, 0.0f, 1.0f);
            const auto idx = static_cast<std::size_t>(clamped * static_cast<float>(sorted.size() - 1));
            return sorted[idx];
        }

        bool IsMetaMismatchReason(DecisionReason reason)
        {
            return reason == DecisionReason::L1FormSemanticCacheMiss ||
                reason == DecisionReason::L1FormSemanticLowConfidence;
        }
    }

    MetricsReporter& MetricsReporter::GetSingleton()
    {
        static MetricsReporter s;
        return s;
    }

    void MetricsReporter::Reset()
    {
        std::scoped_lock lock(_mutex);
        _latencySamplesUs.clear();
        _totalDecisions = 0;
        _unknownDecisions = 0;
        _metaMismatchDecisions = 0;
        _lastCumulativeDrops = 0;
    }

    void MetricsReporter::OnDecisions(const std::vector<DecisionResult>& decisions, std::uint64_t nowUs)
    {
        if (decisions.empty()) {
            return;
        }

        std::scoped_lock lock(_mutex);
        _latencySamplesUs.reserve(_latencySamplesUs.size() + decisions.size());

        for (const auto& d : decisions) {
            ++_totalDecisions;

            if (d.source.eventType == EventType::Unknown) {
                ++_unknownDecisions;
            }
            if (IsMetaMismatchReason(d.reason)) {
                ++_metaMismatchDecisions;
            }

            if (d.source.qpc > 0 && nowUs >= d.source.qpc) {
                const auto latencyUs = static_cast<std::uint64_t>(nowUs - d.source.qpc);
                if (latencyUs <= 2'000'000ull) {
                    _latencySamplesUs.push_back(static_cast<std::uint32_t>(latencyUs));
                }
            }
        }
    }

    MetricsReporter::Snapshot MetricsReporter::SnapshotAndReset(
        std::size_t queueDepth,
        std::uint64_t cumulativeDrops)
    {
        std::scoped_lock lock(_mutex);

        Snapshot s{};
        s.queueDepth = static_cast<std::uint32_t>(queueDepth);
        s.dropCount = (cumulativeDrops >= _lastCumulativeDrops) ?
            (cumulativeDrops - _lastCumulativeDrops) :
            cumulativeDrops;
        _lastCumulativeDrops = cumulativeDrops;

        if (!_latencySamplesUs.empty()) {
            std::sort(_latencySamplesUs.begin(), _latencySamplesUs.end());
            s.sampleCount = static_cast<std::uint32_t>(_latencySamplesUs.size());
            s.latencyP50Us = PercentileFromSorted(_latencySamplesUs, 0.50f);
            s.latencyP95Us = PercentileFromSorted(_latencySamplesUs, 0.95f);
        }

        if (_totalDecisions > 0) {
            s.unknownRatio = static_cast<float>(_unknownDecisions) /
                static_cast<float>(_totalDecisions);
            s.metaMismatchRatio = static_cast<float>(_metaMismatchDecisions) /
                static_cast<float>(_totalDecisions);
        }

        _latencySamplesUs.clear();
        _totalDecisions = 0;
        _unknownDecisions = 0;
        _metaMismatchDecisions = 0;
        return s;
    }
}
