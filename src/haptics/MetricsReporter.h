#pragma once

#include "haptics/DecisionEngine.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace dualpad::haptics
{
    class MetricsReporter
    {
    public:
        struct Snapshot
        {
            std::uint32_t latencyP50Us{ 0 };
            std::uint32_t latencyP95Us{ 0 };
            std::uint32_t sampleCount{ 0 };
            float unknownRatio{ 0.0f };
            float metaMismatchRatio{ 0.0f };
            std::uint32_t queueDepth{ 0 };
            std::uint64_t dropCount{ 0 };
        };

        static MetricsReporter& GetSingleton();

        void Reset();
        void OnDecisions(const std::vector<DecisionResult>& decisions, std::uint64_t nowUs);

        Snapshot SnapshotAndReset(std::size_t queueDepth, std::uint64_t cumulativeDrops);

    private:
        MetricsReporter() = default;

        std::mutex _mutex;
        std::vector<std::uint32_t> _latencySamplesUs;
        std::uint64_t _totalDecisions{ 0 };
        std::uint64_t _unknownDecisions{ 0 };
        std::uint64_t _metaMismatchDecisions{ 0 };
        std::uint64_t _lastCumulativeDrops{ 0 };
    };
}
