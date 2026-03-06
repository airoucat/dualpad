#pragma once

#include "haptics/HapticsTypes.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace dualpad::haptics
{
    class AudioOnlyScorer
    {
    public:
        struct Stats
        {
            std::uint64_t featuresPulled{ 0 };
            std::uint64_t sourcesProduced{ 0 };
            std::uint64_t lowEnergyDropped{ 0 };
            std::uint64_t relativeEnergyDropped{ 0 };
        };

        static AudioOnlyScorer& GetSingleton();

        void Initialize();
        void Shutdown();

        std::vector<HapticSourceMsg> Update();

        Stats GetStats() const;
        void ResetStats();

    private:
        AudioOnlyScorer() = default;

        HapticSourceMsg ToSource(const AudioFeatureMsg& a) const;
        void ReloadParamsFromConfig();

        struct RuntimeParams
        {
            float gain{ 1.00f };
            float minEnergy{ 0.0008f };
            float ampFloor{ 0.02f };
            float panMix{ 0.25f };
            float relativeEnergyRatioThreshold{ 1.10f };
            std::uint32_t minTtlMs{ 24 };
            std::uint32_t maxTtlMs{ 180 };
            int priority{ 60 };
        };

        struct VoiceEnergyState
        {
            float noiseFloor{ 0.0f };
            std::uint32_t samples{ 0 };
        };

        RuntimeParams _rp{};
        std::atomic<bool> _initialized{ false };
        std::unordered_map<std::uint64_t, VoiceEnergyState> _voiceEnergyState;

        std::atomic<std::uint64_t> _featuresPulled{ 0 };
        std::atomic<std::uint64_t> _sourcesProduced{ 0 };
        std::atomic<std::uint64_t> _lowEnergyDropped{ 0 };
        std::atomic<std::uint64_t> _relativeEnergyDropped{ 0 };
    };
}
