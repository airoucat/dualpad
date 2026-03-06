#include "pch.h"
#include "haptics/AudioOnlyScorer.h"

#include "haptics/HapticsConfig.h"
#include "haptics/VoiceManager.h"

#include <SKSE/SKSE.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace logger = SKSE::log;

namespace
{
    inline float Clamp01(float x)
    {
        return std::clamp(x, 0.0f, 1.0f);
    }
}

namespace dualpad::haptics
{
    AudioOnlyScorer& AudioOnlyScorer::GetSingleton()
    {
        static AudioOnlyScorer s;
        return s;
    }

    void AudioOnlyScorer::ReloadParamsFromConfig()
    {
        const auto& cfg = HapticsConfig::GetSingleton();

        _rp.gain = std::clamp(cfg.correctionGain, 0.05f, 2.0f);
        _rp.minEnergy = 0.0008f;
        _rp.ampFloor = 0.02f;
        _rp.panMix = 0.25f;
        _rp.relativeEnergyRatioThreshold = std::clamp(cfg.relativeEnergyRatioThreshold, 1.0f, 8.0f);
        _rp.minTtlMs = 24;
        _rp.maxTtlMs = 360;
        _rp.priority = std::max(10, cfg.priorityFootstep);
    }

    void AudioOnlyScorer::Initialize()
    {
        bool expected = false;
        if (!_initialized.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            logger::warn("[Haptics][AudioOnlyScorer] already initialized");
            return;
        }

        ReloadParamsFromConfig();
        ResetStats();
        _voiceEnergyState.clear();

        logger::info("[Haptics][AudioOnlyScorer] initialized gain={:.2f} minEnergy={:.5f} ampFloor={:.3f} relRatio={:.2f}",
            _rp.gain, _rp.minEnergy, _rp.ampFloor, _rp.relativeEnergyRatioThreshold);
    }

    void AudioOnlyScorer::Shutdown()
    {
        bool expected = true;
        if (!_initialized.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
            return;
        }

        logger::info("[Haptics][AudioOnlyScorer] shutdown");
    }

    std::vector<HapticSourceMsg> AudioOnlyScorer::Update()
    {
        std::vector<HapticSourceMsg> out;
        if (!_initialized.load(std::memory_order_acquire)) {
            return out;
        }

        std::array<AudioFeatureMsg, 256> feats{};
        const std::size_t n = VoiceManager::GetSingleton().PopAudioFeatures(feats.data(), feats.size());
        if (n == 0) {
            return out;
        }

        _featuresPulled.fetch_add(static_cast<std::uint64_t>(n), std::memory_order_relaxed);

        out.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            const auto& a = feats[i];

            const float loud = Clamp01(0.65f * a.peak + 0.35f * a.rms);
            if (loud < _rp.minEnergy) {
                _lowEnergyDropped.fetch_add(1, std::memory_order_relaxed);
                continue;
            }

            auto& state = _voiceEnergyState[a.voiceId];
            if (state.samples == 0) {
                state.noiseFloor = loud;
            }
            else if (loud < state.noiseFloor) {
                state.noiseFloor = 0.85f * state.noiseFloor + 0.15f * loud;
            }
            else {
                state.noiseFloor = 0.995f * state.noiseFloor + 0.005f * loud;
            }

            state.noiseFloor = std::clamp(state.noiseFloor, 0.00005f, 1.0f);
            const float relEnergy = loud / state.noiseFloor;
            const bool warmupDone = state.samples >= 4;
            ++state.samples;

            const bool lowRelative = relEnergy < _rp.relativeEnergyRatioThreshold;
            const bool strongAbsolute = loud >= 0.16f;
            if (warmupDone && lowRelative && !strongAbsolute) {
                _relativeEnergyDropped.fetch_add(1, std::memory_order_relaxed);
                continue;
            }

            auto src = ToSource(a);
            src.relativeEnergy = warmupDone ? std::max(1.0f, relEnergy) : 0.0f;
            out.push_back(src);
        }

        if (!out.empty()) {
            _sourcesProduced.fetch_add(static_cast<std::uint64_t>(out.size()), std::memory_order_relaxed);
        }

        return out;
    }

    HapticSourceMsg AudioOnlyScorer::ToSource(const AudioFeatureMsg& a) const
    {
        const float loud = Clamp01(0.65f * a.peak + 0.35f * a.rms);

        const float amp = Clamp01(std::max(_rp.ampFloor, loud * _rp.gain));

        const float sum = std::max(1e-5f, a.energyL + a.energyR);
        const float pan = std::clamp((a.energyR - a.energyL) / sum, -1.0f, 1.0f) * _rp.panMix;

        std::uint32_t ttlMs = 36;
        if (a.qpcEnd > a.qpcStart) {
            const auto durUs = static_cast<std::uint32_t>(a.qpcEnd - a.qpcStart);
            // Keep a small padding for pipeline delay while staying close to source audio length.
            ttlMs = std::clamp(durUs / 1000u + 6u, _rp.minTtlMs, _rp.maxTtlMs);
        }

        HapticSourceMsg s{};
        s.qpc = (a.qpcStart != 0) ? a.qpcStart : ToQPC(Now());
        s.type = SourceType::AudioMod;
        s.eventType = EventType::Unknown;
        s.sourceVoiceId = a.voiceId;
        s.left = Clamp01(amp * (1.0f - pan));
        s.right = Clamp01(amp * (1.0f + pan));
        s.confidence = Clamp01(0.42f + 0.58f * loud);
        s.relativeEnergy = 0.0f;
        s.priority = _rp.priority;
        s.ttlMs = ttlMs;
        return s;
    }

    AudioOnlyScorer::Stats AudioOnlyScorer::GetStats() const
    {
        Stats s{};
        s.featuresPulled = _featuresPulled.load(std::memory_order_relaxed);
        s.sourcesProduced = _sourcesProduced.load(std::memory_order_relaxed);
        s.lowEnergyDropped = _lowEnergyDropped.load(std::memory_order_relaxed);
        s.relativeEnergyDropped = _relativeEnergyDropped.load(std::memory_order_relaxed);
        return s;
    }

    void AudioOnlyScorer::ResetStats()
    {
        _featuresPulled.store(0, std::memory_order_relaxed);
        _sourcesProduced.store(0, std::memory_order_relaxed);
        _lowEnergyDropped.store(0, std::memory_order_relaxed);
        _relativeEnergyDropped.store(0, std::memory_order_relaxed);
        _voiceEnergyState.clear();
    }
}
