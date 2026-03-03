#include "pch.h"
#include "haptics/EventWindowScorer.h"

#include "haptics/EventQueue.h"
#include "haptics/VoiceManager.h"
#include "haptics/HapticsConfig.h"

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

    inline std::uint64_t AbsDiffU64(std::uint64_t a, std::uint64_t b)
    {
        return (a >= b) ? (a - b) : (b - a);
    }
}

namespace dualpad::haptics
{
    EventWindowScorer& EventWindowScorer::GetSingleton()
    {
        static EventWindowScorer s;
        return s;
    }

    EventWindowScorer::EventWindowScorer() :
        _eventCache(EventShortWindowCache::GetSingleton()),
        _submitCache(SubmitFeatureCache::GetSingleton()),
        _templateCache(HapticTemplateCache::GetSingleton())
    {
    }

    void EventWindowScorer::ReloadRuntimeParamsFromConfig()
    {
        const auto& cfg = HapticsConfig::GetSingleton();

        _rp.correctionWindowUs = std::max<std::uint32_t>(5'000, cfg.correctionWindowMs * 1000);
        _rp.lookbackUs = 8'000;
        _rp.acceptScore = std::clamp(cfg.correctionMinScore, 0.05f, 0.95f);

        float wt = std::max(0.0f, cfg.weightTiming);
        float we = std::max(0.0f, cfg.weightAttack + cfg.weightSpectrum * 0.5f);
        float wp = std::max(0.0f, cfg.weightChannel);

        float sum = wt + we + wp;
        if (sum < 1e-6f) {
            wt = 0.62f; we = 0.30f; wp = 0.08f; sum = 1.0f;
        }

        _rp.wTiming = wt / sum;
        _rp.wEnergy = we / sum;
        _rp.wPan = wp / sum;

        _rp.timingTauUs = std::clamp(static_cast<float>(_rp.correctionWindowUs) * 0.60f, 6'000.0f, 30'000.0f);

        _rp.immediateGain = std::clamp(cfg.immediateGain, 0.05f, 2.0f);
        _rp.correctionGain = std::clamp(cfg.correctionGain, 0.05f, 2.5f);

        _rp.audioDrivenPreferAudioOnly = cfg.audioDrivenPreferAudioOnly;
        _rp.fallbackBaseWhenNoMatch = cfg.fallbackBaseWhenNoMatch;
        _rp.enableAmbientPassthrough = cfg.enableAmbientPassthrough;
    }

    void EventWindowScorer::Initialize()
    {
        bool expected = false;
        if (!_initialized.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            logger::warn("[Haptics][Scorer] already initialized");
            return;
        }

        const auto& cfg = HapticsConfig::GetSingleton();
        ReloadRuntimeParamsFromConfig();

        if (!_templateCache.IsReady()) {
            _templateCache.WarmupDefaults();
        }

        EventShortWindowCache::Config ecfg{};
        ecfg.capacity = 128;
        ecfg.windowUs = std::max<std::uint32_t>(50'000, cfg.eventShortWindowMs * 1000);

        SubmitFeatureCache::Config scfg{};
        scfg.capacity = 320;
        scfg.windowUs = std::max<std::uint32_t>(100'000, cfg.submitFeatureCacheMs * 1000);
        scfg.voiceProfileTtlUs = 10'000'000;

        _eventCache.Initialize(ecfg);
        _submitCache.Initialize(scfg);

        {
            std::scoped_lock lk(_pendingMtx);
            _pending.clear();
            _pending.reserve(128);
        }

        ResetStats();

        logger::info("[Haptics][Scorer] initialized corr={}us accept={:.3f} shortWin={}us submitWin={}us",
            _rp.correctionWindowUs, _rp.acceptScore, ecfg.windowUs, scfg.windowUs);
    }

    void EventWindowScorer::Shutdown()
    {
        bool expected = true;
        if (!_initialized.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
            return;
        }

        {
            std::scoped_lock lk(_pendingMtx);
            _pending.clear();
        }

        logger::info("[Haptics][Scorer] shutdown");
    }

    std::vector<HapticSourceMsg> EventWindowScorer::Update()
    {
        std::vector<HapticSourceMsg> out;
        if (!_initialized.load(std::memory_order_acquire)) {
            return out;
        }

        out.reserve(32);

        auto events = EventQueue::GetSingleton().DrainAll(256);
        if (!events.empty()) {
            _eventsPulled.fetch_add(static_cast<std::uint64_t>(events.size()), std::memory_order_relaxed);
        }

        std::vector<PendingEvent> toAdd;
        toAdd.reserve(events.size());

        for (const auto& e : events) {
            const auto id = _nextEventId.fetch_add(1, std::memory_order_relaxed);
            EventToken t = ToEventToken(e, id);

            _eventCache.Push(t);

            const bool shouldEmitImmediate = [&]() {
                const auto& cfg = HapticsConfig::GetSingleton();
                if (cfg.hapticsMode != HapticsConfig::HapticsMode::AudioDriven) {
                    return true;
                }
                if (_rp.audioDrivenPreferAudioOnly && !_rp.fallbackBaseWhenNoMatch) {
                    return false;
                }
                return true;
                }();

            if (shouldEmitImmediate) {
                out.push_back(MakeImmediateSource(t));
            }
            else {
                _passthroughSources.fetch_add(1, std::memory_order_relaxed);
            }

            PendingEvent p{};
            p.token = t;
            p.deadlineUs = t.tEventUs + _rp.correctionWindowUs;
            toAdd.push_back(p);
        }

        if (!toAdd.empty()) {
            std::scoped_lock lk(_pendingMtx);
            _pending.insert(_pending.end(), toAdd.begin(), toAdd.end());
        }

        std::array<AudioFeatureMsg, 256> feats{};
        const std::size_t n = VoiceManager::GetSingleton().PopAudioFeatures(feats.data(), feats.size());
        if (n > 0) {
            _audioFeaturesPulled.fetch_add(static_cast<std::uint64_t>(n), std::memory_order_relaxed);
        }

        for (std::size_t i = 0; i < n; ++i) {
            _submitCache.Push(ToAudioChunk(feats[i]));
        }

        const std::uint64_t nowUs = ToQPC(Now());

        std::scoped_lock lk(_pendingMtx);
        auto it = _pending.begin();
        while (it != _pending.end()) {
            const auto& token = it->token;

            auto cands = _submitCache.QueryByTime(
                token.tEventUs,
                _rp.lookbackUs,
                _rp.correctionWindowUs,
                32);

            float bestScore = 0.0f;
            AudioChunkFeature best{};

            for (const auto& c : cands) {
                const float s = Score(token, c);
                if (s > bestScore) {
                    bestScore = s;
                    best = c;
                }
            }

            if (bestScore >= _rp.acceptScore) {
                out.push_back(MakeCorrectionSource(token, best, bestScore));
                _matched.fetch_add(1, std::memory_order_relaxed);
                it = _pending.erase(it);
                continue;
            }

            if (nowUs > it->deadlineUs) {
                _unmatched.fetch_add(1, std::memory_order_relaxed);
                it = _pending.erase(it);
            }
            else {
                ++it;
            }
        }

        if (!out.empty()) {
            _sourcesProduced.fetch_add(static_cast<std::uint64_t>(out.size()), std::memory_order_relaxed);
        }

        return out;
    }

    EventWindowScorer::Stats EventWindowScorer::GetStats() const
    {
        Stats s{};
        s.audioFeaturesPulled = _audioFeaturesPulled.load(std::memory_order_relaxed);
        s.sourcesProduced = _sourcesProduced.load(std::memory_order_relaxed);

        s.eventsPulled = _eventsPulled.load(std::memory_order_relaxed);
        s.matched = _matched.load(std::memory_order_relaxed);
        s.unmatched = _unmatched.load(std::memory_order_relaxed);
        s.passthroughSources = _passthroughSources.load(std::memory_order_relaxed);

        s.totalEvents = s.eventsPulled;
        s.totalMatched = s.matched;
        s.totalUnmatched = s.unmatched;

        {
            std::scoped_lock lk(_pendingMtx);
            s.activeWindows = static_cast<std::uint32_t>(_pending.size());
        }

        return s;
    }

    void EventWindowScorer::ResetStats()
    {
        _audioFeaturesPulled.store(0, std::memory_order_relaxed);
        _sourcesProduced.store(0, std::memory_order_relaxed);
        _eventsPulled.store(0, std::memory_order_relaxed);
        _matched.store(0, std::memory_order_relaxed);
        _unmatched.store(0, std::memory_order_relaxed);
        _passthroughSources.store(0, std::memory_order_relaxed);
    }

    EventToken EventWindowScorer::ToEventToken(const EventMsg& e, std::uint64_t id)
    {
        EventToken t{};
        t.eventId = id;
        t.tEventUs = (e.qpc != 0) ? e.qpc : ToQPC(Now());
        t.eventType = e.type;
        t.semantic = e.semanticHint;
        t.intensityHint = Clamp01(e.intensity);
        t.formId = e.formId;
        t.actorId = e.actorId;
        return t;
    }

    AudioChunkFeature EventWindowScorer::ToAudioChunk(const AudioFeatureMsg& a)
    {
        AudioChunkFeature c{};
        c.tSubmitUs = (a.qpcStart != 0) ? a.qpcStart : ToQPC(Now());
        c.voice.voicePtr = static_cast<std::uintptr_t>(a.voiceId);
        c.voice.generation = 0;
        c.sampleRate = a.sampleRate;
        c.channels = a.channels;
        c.rms = a.rms;
        c.peak = a.peak;
        c.energyL = a.energyL;
        c.energyR = a.energyR;
        c.durationUs = (a.qpcEnd > a.qpcStart) ? static_cast<std::uint32_t>(a.qpcEnd - a.qpcStart) : 0;
        c.centroid = 0.0f;
        c.zcr = 0.0f;
        return c;
    }

    float EventWindowScorer::BaseAmpFor(EventType t, float intensity) const
    {
        const auto& tp = _templateCache.Get(t);
        return Clamp01(tp.immediateAmp * Clamp01(intensity) * _rp.immediateGain);
    }

    std::uint32_t EventWindowScorer::BaseTtlFor(EventType t) const
    {
        const auto* ec = HapticsConfig::GetSingleton().GetEventConfig(t);
        if (ec) {
            return std::max<std::uint32_t>(10, ec->ttlMs);
        }
        const auto& tp = _templateCache.Get(t);
        return std::max<std::uint32_t>(10, tp.immediateTtlMs);
    }

    int EventWindowScorer::PriorityFor(EventType t) const
    {
        const auto* ec = HapticsConfig::GetSingleton().GetEventConfig(t);
        if (ec) {
            return static_cast<int>(ec->priority);
        }

        switch (t) {
        case EventType::HitImpact:   return 100;
        case EventType::Block:       return 95;
        case EventType::Shout:       return 90;
        case EventType::WeaponSwing: return 75;
        case EventType::SpellImpact: return 75;
        case EventType::SpellCast:   return 70;
        case EventType::BowRelease:  return 70;
        case EventType::Land:        return 65;
        case EventType::Jump:        return 55;
        case EventType::Footstep:    return 35;
        default:                     return 50;
        }
    }

    HapticSourceMsg EventWindowScorer::MakeImmediateSource(const EventToken& e) const
    {
        const auto& tp = _templateCache.Get(e.eventType);
        const float amp = BaseAmpFor(e.eventType, e.intensityHint);

        HapticSourceMsg s{};
        s.qpc = e.tEventUs;
        s.type = SourceType::BaseEvent;
        s.eventType = e.eventType;
        s.left = Clamp01(amp * tp.leftGain);
        s.right = Clamp01(amp * tp.rightGain);
        s.confidence = 1.0f;
        s.priority = PriorityFor(e.eventType);
        s.ttlMs = BaseTtlFor(e.eventType);
        return s;
    }

    HapticSourceMsg EventWindowScorer::MakeCorrectionSource(
        const EventToken& e,
        const AudioChunkFeature& a,
        float score) const
    {
        const auto& tp = _templateCache.Get(e.eventType);

        const float loud = Clamp01(0.65f * a.peak + 0.35f * a.rms);
        const float base = BaseAmpFor(e.eventType, e.intensityHint) * tp.correctionAmp;
        const float amp = Clamp01(base * loud * score * _rp.correctionGain);

        const float sum = std::max(1e-4f, a.energyL + a.energyR);
        const float pan = std::clamp((a.energyR - a.energyL) / sum, -1.0f, 1.0f) * tp.maxPan;

        HapticSourceMsg s{};
        s.qpc = a.tSubmitUs;
        s.type = SourceType::AudioMod;
        s.eventType = e.eventType;
        s.left = Clamp01(amp * (1.0f - pan));
        s.right = Clamp01(amp * (1.0f + pan));
        s.confidence = Clamp01(score);
        s.priority = PriorityFor(e.eventType);
        s.ttlMs = std::max<std::uint32_t>(8, tp.correctionTtlMs);
        return s;
    }

    float EventWindowScorer::Score(const EventToken& e, const AudioChunkFeature& a) const
    {
        const std::uint64_t dt = AbsDiffU64(a.tSubmitUs, e.tEventUs);
        const float timing = std::exp(-static_cast<float>(dt) / _rp.timingTauUs);

        const float energy = Clamp01(0.6f * a.rms + 0.4f * a.peak);

        const float sum = std::max(1e-4f, a.energyL + a.energyR);
        const float lrBalance = 1.0f - std::abs((a.energyR - a.energyL) / sum);
        const float panConf = Clamp01(lrBalance);

        float voicePenalty = 0.0f;
        SubmitFeatureCache::VoiceProfile vp{};
        if (_submitCache.GetVoiceProfile(a.voice, vp) && vp.seenCount >= 8) {
            const float avg = std::max(1e-4f, vp.avgRms);
            if (a.rms < avg * 0.35f) {
                voicePenalty = 0.25f;
            }
        }

        const float score =
            _rp.wTiming * timing +
            _rp.wEnergy * energy +
            _rp.wPan * panConf -
            0.10f * voicePenalty;

        return Clamp01(score);
    }
}