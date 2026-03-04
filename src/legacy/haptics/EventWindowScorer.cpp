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
    constexpr bool kAudioOnlyMode = true;   // 你要的模式
    constexpr bool kForceNoBase = true;   // 明确不出 BaseEvent
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

        _rp.correctionWindowUs = std::max<std::uint32_t>(150'000, cfg.correctionWindowMs * 1000);
        _rp.lookbackUs = 50'000;
        // const float minOutConf = std::clamp(cfg.minConfidence, 0.05f, 0.95f);

        if constexpr (kAudioOnlyMode) {
            // _rp.acceptScore = std::clamp(cfg.correctionMinScore, 0.16f, 0.90f); // 比 0.24 更稳
            _rp.acceptScore = 0.18f;  // 先固定，后面再回到配置化
        }
        else {
            _rp.acceptScore = std::clamp(cfg.correctionMinScore, 0.24f, 0.95f);
        }

        float wt = std::max(0.0f, cfg.weightTiming);
        float we = std::max(0.0f, cfg.weightAttack + cfg.weightSpectrum * 0.5f);
        float wp = std::max(0.0f, cfg.weightChannel);
        float wm = std::max(0.0f, cfg.weightMeta);

        float sum = wt + we + wp + wm;
        if (sum < 1e-6f) {
            wt = 0.50f; we = 0.25f; wp = 0.05f; wm = 0.20f; sum = 1.0f;
        }

        _rp.wTiming = wt / sum;
        _rp.wEnergy = we / sum;
        _rp.wPan = wp / sum;
        _rp.wMeta = wm / sum;

        _rp.timingTauUs = std::clamp(static_cast<float>(_rp.correctionWindowUs) * 0.60f, 6'000.0f, 55'000.0f);

        _rp.immediateGain = std::clamp(cfg.immediateGain, 0.05f, 2.0f);
        _rp.correctionGain = std::clamp(cfg.correctionGain, 0.05f, 2.5f);

        _rp.audioDrivenPreferAudioOnly = cfg.audioDrivenPreferAudioOnly;
        _rp.fallbackBaseWhenNoMatch = cfg.fallbackBaseWhenNoMatch;
        _rp.enableAmbientPassthrough = cfg.enableAmbientPassthrough;
        _rp.maxCorrectionBursts = 5;
        _rp.minInterCorrectionUs = 8'000;
        _rp.minCorrectionTtlMsGeneral = kAudioOnlyMode ? 50 : 24;
        _rp.minCorrectionTtlMsWeapon = kAudioOnlyMode ? 70 : 40;
        _rp.metaUnknownConf = 0.45f;
        _rp.metaMismatchPenalty = 0.20f;

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
        _matchCache.Initialize(512);   // <- 新增
        {
            std::scoped_lock lk(_pendingMtx);
            _pending.clear();
            _pending.reserve(128);
        }


        ResetStats();

        logger::info("[Haptics][Scorer] initialized corr={}us accept={:.3f} shortWin={}us submitWin={}us burstMax={} inter={}us",
            _rp.correctionWindowUs, _rp.acceptScore, ecfg.windowUs, scfg.windowUs,
            _rp.maxCorrectionBursts, _rp.minInterCorrectionUs);
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
        _matchCache.Clear();  
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
            /*
            if (shouldEmitImmediate) {
                out.push_back(MakeImmediateSource(t));
            }
            else {
                _passthroughSources.fetch_add(1, std::memory_order_relaxed);
            }
            */ // 原代码

            if constexpr (!kForceNoBase && shouldEmitImmediate) {
                out.push_back(MakeImmediateSource(t));
            }
            else {
                _passthroughSources.fetch_add(1, std::memory_order_relaxed);
            } // 测试是否音频震动

            PendingEvent p{};
            p.token = t;

            const bool isMobility =
                (t.eventType == EventType::Jump) ||
                (t.eventType == EventType::Land) ||
                (t.eventType == EventType::Footstep);

            // mobility 用更短窗口，压 P95/P96
            const std::uint64_t perEventWinUs = isMobility ? 90'000 : _rp.correctionWindowUs;
            p.deadlineUs = t.tEventUs + perEventWinUs;

            p.cacheProbeDone = false;
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

            // <- 新增：短窗结果缓存命中，直接复用
            if (it->emittedCount == 0 && !it->cacheProbeDone) {
                it->cacheProbeDone = true;  // 每事件只探测一次

                HapticSourceMsg cached{};
                if (_matchCache.TryGet(token, nowUs, cached)) {
                    _cacheHits.fetch_add(1, std::memory_order_relaxed);
                    out.push_back(cached);
                    _matched.fetch_add(1, std::memory_order_relaxed);
                    it = _pending.erase(it);
                    continue;
                }
                else {
                    _cacheMisses.fetch_add(1, std::memory_order_relaxed);
                }
            }

            auto cands = _submitCache.QueryByTime(
                token.tEventUs,
                _rp.lookbackUs,
                _rp.correctionWindowUs,
                96);

            float bestScore = 0.0f;
            AudioChunkFeature best{};
            bool bestMetaMismatch = false;
            bool anyMetaMismatch = false;

            for (const auto& c : cands) {
                bool mm = false;
                const float s = ScoreWithMeta(token, c, mm);
                anyMetaMismatch = anyMetaMismatch || mm;

                if (s > bestScore) {
                    bestScore = s;
                    best = c;
                    bestMetaMismatch = mm;
                }
            }
            /*
            if (bestScore >= _rp.acceptScore) {
                out.push_back(MakeCorrectionSource(token, best, bestScore));
                _matched.fetch_add(1, std::memory_order_relaxed);
                it = _pending.erase(it);
                continue;
            }
            */

            float accept = _rp.acceptScore;
            if (token.eventType == EventType::Jump || token.eventType == EventType::Land) {
                accept = 0.16f;
            }
            else if (token.eventType == EventType::Footstep) {
                accept = 0.17f;
            }
            if (bestScore >= accept) {
                if (bestMetaMismatch) {
                    _metaMismatch.fetch_add(1, std::memory_order_relaxed);
                }
                const bool spacingOk =
                    (it->emittedCount == 0) ||
                    ((nowUs > it->lastEmitUs) && (nowUs - it->lastEmitUs >= _rp.minInterCorrectionUs));

                if (spacingOk) {
                    out.push_back(MakeCorrectionSource(token, best, bestScore));
                    _matchCache.Put(token, out.back(), bestScore, nowUs);   // <- 新增
                    _cachePuts.fetch_add(1, std::memory_order_relaxed);
                    if (it->emittedCount == 0) {
                        _matched.fetch_add(1, std::memory_order_relaxed);
                    }

                    it->emittedCount++;
                    it->lastEmitUs = nowUs;
                }

                // 达到连发上限或超时才移除
                if (it->emittedCount >= _rp.maxCorrectionBursts || nowUs > it->deadlineUs) {
                    it = _pending.erase(it);
                }
                else {
                    ++it;
                }
                continue;
            }

            const bool isMobility =
                (token.eventType == EventType::Jump) ||
                (token.eventType == EventType::Land) ||
                (token.eventType == EventType::Footstep);

            const std::uint64_t ageUs = (nowUs > token.tEventUs) ? (nowUs - token.tEventUs) : 0;

            // 不等到 deadline，80ms 就决策，压尾延迟
            if (it->emittedCount == 0 && isMobility && ageUs >= 80'000) {
                auto nearCands = _submitCache.QueryByTime(nowUs, 180'000, 180'000, 4);

                if (!nearCands.empty()) {
                    const auto bestIt = std::max_element(nearCands.begin(), nearCands.end(),
                        [](const auto& a, const auto& b) {
                            return (a.peak + 0.6f * a.rms) < (b.peak + 0.6f * b.rms);
                        });

                    const float nearScore = 0.22f;
                    out.push_back(MakeCorrectionSource(token, *bestIt, nearScore));
                    _matchCache.Put(token, out.back(), nearScore, nowUs);
                }
                else {
                    HapticSourceMsg s{};
                    s.qpc = token.tEventUs;
                    s.type = SourceType::AudioMod;
                    s.eventType = token.eventType;

                    float amp = 0.12f; float conf = 0.35f; std::uint32_t ttl = 55;
                    if (token.eventType == EventType::Land) {
                        amp = 0.18f; conf = 0.40f; ttl = 70;
                    }

                    s.left = amp;
                    s.right = amp;
                    s.confidence = conf;
                    s.priority = PriorityFor(token.eventType);
                    s.ttlMs = ttl;

                    out.push_back(s);
                    _matchCache.Put(token, out.back(), 0.20f, nowUs);
                }

                _cachePuts.fetch_add(1, std::memory_order_relaxed);
                _matched.fetch_add(1, std::memory_order_relaxed);
                it = _pending.erase(it);
                continue;
            }


            if (nowUs > it->deadlineUs) {
                if (it->emittedCount == 0) {
                    // 纯音频模式：无命中时先尝试近邻音频兜底（仍不是 Base）
                    if constexpr (kAudioOnlyMode) {

                        // 对 Jump/Land/Footstep 放宽近邻窗口，降低末段漏震
                        const bool isMobility =
                            (token.eventType == EventType::Jump) ||
                            (token.eventType == EventType::Land) ||
                            (token.eventType == EventType::Footstep);

                        const std::uint64_t nearWinUs = isMobility ? 220'000 : 180'000;

                        auto nearCands = _submitCache.QueryByTime(nowUs, nearWinUs, nearWinUs, 6);
                        if (!nearCands.empty()) {
                            const auto bestIt = std::max_element(nearCands.begin(), nearCands.end(),
                                [](const auto& a, const auto& b) {
                                    return (a.peak + 0.6f * a.rms) < (b.peak + 0.6f * b.rms);
                                });

                            const float nearScore = isMobility ? 0.24f : 0.26f;
                            out.push_back(MakeCorrectionSource(token, *bestIt, nearScore));
                            _matchCache.Put(token, out.back(), nearScore, nowUs);
                            _cachePuts.fetch_add(1, std::memory_order_relaxed);
                            _matched.fetch_add(1, std::memory_order_relaxed);
                            it = _pending.erase(it);
                            continue;
                        }

                        // Jump/Land/Footstep 专用弱兜底，解决连跳末段偶发漏震
                        if (token.eventType == EventType::Jump ||
                            token.eventType == EventType::Land ||
                            token.eventType == EventType::Footstep) {

                            HapticSourceMsg s{};
                            s.qpc = token.tEventUs;
                            s.type = SourceType::AudioMod;     // 保持 AudioOnly 语义
                            s.eventType = token.eventType;

                            float amp = 0.10f;
                            float conf = 0.34f;
                            std::uint32_t ttl = 50;

                            if (token.eventType == EventType::Jump) {
                                amp = 0.14f; conf = 0.40f; ttl = 60;
                            }
                            else if (token.eventType == EventType::Land) {
                                amp = 0.20f; conf = 0.42f; ttl = 80;
                            }
                            else { // Footstep
                                amp = 0.10f; conf = 0.34f; ttl = 50;
                            }

                            s.left = amp;
                            s.right = amp;
                            s.confidence = conf;
                            s.priority = PriorityFor(token.eventType);
                            s.ttlMs = ttl;

                            out.push_back(s);

                            const float weakScore =
                                (token.eventType == EventType::Footstep) ? 0.16f : 0.20f;
                            _matchCache.Put(token, out.back(), weakScore, nowUs);
                            _cachePuts.fetch_add(1, std::memory_order_relaxed);

                            _matched.fetch_add(1, std::memory_order_relaxed);
                            it = _pending.erase(it);
                            continue;
                        }
                    }

                    _unmatched.fetch_add(1, std::memory_order_relaxed);
                    if (cands.empty()) {
                        _noCandidate.fetch_add(1, std::memory_order_relaxed);
                    }
                    else if (anyMetaMismatch) {
                        _metaMismatch.fetch_add(1, std::memory_order_relaxed);
                    }
                    else {
                        _candidateLowScore.fetch_add(1, std::memory_order_relaxed);
                    }
                }
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

        s.noCandidate = _noCandidate.load(std::memory_order_relaxed);
        s.candidateLowScore = _candidateLowScore.load(std::memory_order_relaxed);
        s.metaMismatch = _metaMismatch.load(std::memory_order_relaxed);

        s.totalEvents = s.eventsPulled;
        s.totalMatched = s.matched;
        s.totalUnmatched = s.unmatched;

        s.cacheHits = _cacheHits.load(std::memory_order_relaxed);
        s.cacheMisses = _cacheMisses.load(std::memory_order_relaxed);
        s.cachePuts = _cachePuts.load(std::memory_order_relaxed);

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
        _noCandidate.store(0, std::memory_order_relaxed);
        _candidateLowScore.store(0, std::memory_order_relaxed);
        _metaMismatch.store(0, std::memory_order_relaxed);
        _cacheHits.store(0, std::memory_order_relaxed);
        _cacheMisses.store(0, std::memory_order_relaxed);
        _cachePuts.store(0, std::memory_order_relaxed);
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
        t.semanticConfidence = std::clamp(e.semanticConfidence, 0.0f, 1.0f);
        t.semanticWeight = std::clamp(e.semanticWeight, 0.0f, 1.0f);
        t.semanticFlags = e.semanticFlags;
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

    SemanticGroup EventWindowScorer::ExpectedSemantic(EventType t)
    {
        switch (t) {
        case EventType::WeaponSwing: return SemanticGroup::WeaponSwing;
        case EventType::HitImpact:   return SemanticGroup::Hit;
        case EventType::Block:       return SemanticGroup::Block;
        case EventType::Footstep:
        case EventType::Jump:
        case EventType::Land:        return SemanticGroup::Footstep;
        case EventType::BowRelease:  return SemanticGroup::Bow;
        case EventType::Shout:       return SemanticGroup::Voice;
        default:                     return SemanticGroup::Unknown;
        }
    }

    float EventWindowScorer::ScoreWithMeta(const EventToken& e, const AudioChunkFeature& a, bool& outMetaMismatch) const
    {
        outMetaMismatch = false;

        const std::uint64_t dt = AbsDiffU64(a.tSubmitUs, e.tEventUs);
        const float timing = std::exp(-static_cast<float>(dt) / _rp.timingTauUs);
        const float energy = Clamp01(0.6f * a.rms + 0.4f * a.peak);

        const float sum = std::max(1e-4f, a.energyL + a.energyR);
        const float lrBalance = 1.0f - std::abs((a.energyR - a.energyL) / sum);
        const float panConf = Clamp01(lrBalance);

        // --- meta ---
        float meta = Clamp01(0.55f * e.semanticConfidence + 0.45f * e.semanticWeight);

        const auto expected = ExpectedSemantic(e.eventType);
        const bool unknown = (e.semantic == SemanticGroup::Unknown);
        const bool mismatch = (!unknown && expected != SemanticGroup::Unknown && e.semantic != expected);

        if (unknown) {
            meta = std::min(meta, _rp.metaUnknownConf);
        }
        if (mismatch) {
            meta = Clamp01(meta - _rp.metaMismatchPenalty);
            outMetaMismatch = true;
        }

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
            _rp.wPan * panConf +
            _rp.wMeta * meta -
            0.10f * voicePenalty;

        return Clamp01(score);
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

        // ---------- 幅度 ----------
        const float loud = Clamp01(0.65f * a.peak + 0.35f * a.rms);

        // 纯音频模式下给一个稳定 seed，避免过短/过弱
        const float seed = std::max(0.18f, tp.immediateAmp * 0.75f);
        const float scoreGain = 0.65f + 0.35f * Clamp01(score);
        const float loudGain = 0.55f + 0.45f * loud;

        float minAmp = 0.10f;
        switch (e.eventType) {
        case EventType::WeaponSwing: minAmp = 0.18f; break;
        case EventType::HitImpact:   minAmp = 0.22f; break;
        case EventType::Block:       minAmp = 0.20f; break;
        case EventType::Footstep:    minAmp = 0.11f; break;
        case EventType::Jump:        minAmp = 0.14f; break;
        case EventType::Land:        minAmp = 0.20f; break;
        default: break;
        }

        const float ampRaw = seed * tp.correctionAmp * scoreGain * loudGain * _rp.correctionGain;
        const float amp = Clamp01(std::max(minAmp, ampRaw));

        // ---------- 声道 ----------
        const float sum = std::max(1e-4f, a.energyL + a.energyR);
        const float pan = std::clamp((a.energyR - a.energyL) / sum, -1.0f, 1.0f) * tp.maxPan;

        HapticSourceMsg s{};
        s.qpc = a.tSubmitUs;
        s.type = SourceType::AudioMod;
        s.eventType = e.eventType;
        s.left = Clamp01(amp * (1.0f - pan));
        s.right = Clamp01(amp * (1.0f + pan));

        // ---------- 置信度 ----------
        const float confFloor = kAudioOnlyMode ? 0.42f : 0.35f;
        s.confidence = std::max(confFloor, Clamp01(0.55f + 0.45f * score));

        s.priority = PriorityFor(e.eventType);

        // ---------- 时长（关键：跟随真实音频时长） ----------
        std::uint32_t minTtl = _rp.minCorrectionTtlMsGeneral;
        switch (e.eventType) {
        case EventType::WeaponSwing:
        case EventType::HitImpact:
        case EventType::Block:
        case EventType::BowRelease:
        case EventType::SpellImpact:
            minTtl = _rp.minCorrectionTtlMsWeapon;
            break;
        default:
            break;
        }

        const std::uint32_t audioMs = std::clamp<std::uint32_t>(a.durationUs / 1000, 12, 220);
        const std::uint32_t followMs = audioMs + 12;  // 可调：18~28

        s.ttlMs = std::max<std::uint32_t>({
            minTtl,
            tp.correctionTtlMs,
            followMs
            });

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