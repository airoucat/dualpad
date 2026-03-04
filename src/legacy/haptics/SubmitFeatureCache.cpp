#include "pch.h"
#include "haptics/SubmitFeatureCache.h"

#include <SKSE/SKSE.h>
#include <algorithm>
#include <chrono>
#include <cmath>

namespace logger = SKSE::log;

namespace
{
    inline std::uint64_t SteadyNowUs()
    {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(now).count());
    }

    inline std::uint64_t AbsDiffU64(std::uint64_t a, std::uint64_t b)
    {
        return (a >= b) ? (a - b) : (b - a);
    }

    inline float LrImbalance(float l, float r)
    {
        constexpr float eps = 1e-6f;
        return std::fabs(l - r) / (std::fabs(l) + std::fabs(r) + eps);
    }
}

namespace dualpad::haptics
{
    SubmitFeatureCache& SubmitFeatureCache::GetSingleton()
    {
        static SubmitFeatureCache s;
        return s;
    }

    void SubmitFeatureCache::Initialize(const Config& cfg)
    {
        Config c = cfg;
        if (c.capacity == 0) {
            c.capacity = 1;
        }
        if (c.windowUs == 0) {
            c.windowUs = 350'000;
        }
        if (c.voiceProfileTtlUs == 0) {
            c.voiceProfileTtlUs = 10'000'000;
        }

        bool expected = false;
        if (!_initialized.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            logger::warn("[Haptics][SubmitFeatureCache] already initialized");
            return;
        }

        {
            std::lock_guard<std::mutex> lock(_mtx);
            _cfg = c;
            _buf.assign(_cfg.capacity, AudioChunkFeature{});
            _head = 0;
            _size = 0;
            _voiceProfiles.clear();
            _pushSinceLastVoicePrune = 0;
        }

        ResetStats();

        logger::info(
            "[Haptics][SubmitFeatureCache] initialized capacity={} windowUs={} voiceProfileTtlUs={}",
            _cfg.capacity, _cfg.windowUs, _cfg.voiceProfileTtlUs);
    }

    void SubmitFeatureCache::Shutdown()
    {
        bool expected = true;
        if (!_initialized.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(_mtx);
            _buf.clear();
            _buf.shrink_to_fit();
            _head = 0;
            _size = 0;
            _voiceProfiles.clear();
            _pushSinceLastVoicePrune = 0;
        }

        logger::info(
            "[Haptics][SubmitFeatureCache] shutdown push={} overwrite={} prune={} query={} hit={}",
            _pushCount.load(std::memory_order_relaxed),
            _overwriteCount.load(std::memory_order_relaxed),
            _pruneCount.load(std::memory_order_relaxed),
            _queryCount.load(std::memory_order_relaxed),
            _queryHitCount.load(std::memory_order_relaxed));
    }

    bool SubmitFeatureCache::Push(const AudioChunkFeature& fIn)
    {
        if (!_initialized.load(std::memory_order_acquire)) {
            return false;
        }

        AudioChunkFeature f = fIn;
        if (f.tSubmitUs == 0) {
            f.tSubmitUs = SteadyNowUs();
        }

        std::lock_guard<std::mutex> lock(_mtx);

        PruneExpiredLocked(f.tSubmitUs);

        const auto cap = _cfg.capacity;
        if (_size < cap) {
            const std::size_t tail = (_head + _size) % cap;
            _buf[tail] = f;
            ++_size;
        }
        else {
            // 覆盖最旧
            _buf[_head] = f;
            _head = (_head + 1) % cap;
            _overwriteCount.fetch_add(1, std::memory_order_relaxed);
        }

        UpdateVoiceProfileLocked(f);

        // 每64次 push 做一次 voice profile 过期清理（摊销成本）
        ++_pushSinceLastVoicePrune;
        if ((_pushSinceLastVoicePrune & 63ULL) == 0ULL) {
            PruneVoiceProfilesLocked(f.tSubmitUs);
        }

        _pushCount.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    std::vector<AudioChunkFeature> SubmitFeatureCache::QueryByTime(
        std::uint64_t tRefUs,
        std::uint32_t lookbackUs,
        std::uint32_t lookaheadUs,
        std::size_t maxOut) const
    {
        _queryCount.fetch_add(1, std::memory_order_relaxed);

        std::vector<AudioChunkFeature> out;
        if (!_initialized.load(std::memory_order_acquire) || maxOut == 0) {
            return out;
        }

        const std::uint64_t tMin = (tRefUs > lookbackUs) ? (tRefUs - lookbackUs) : 0;
        const std::uint64_t tMax = tRefUs + lookaheadUs;

        struct Candidate
        {
            std::uint64_t dtAbs{ 0 };
            AudioChunkFeature f{};
        };
        std::vector<Candidate> tmp;
        tmp.reserve(64);

        {
            std::lock_guard<std::mutex> lock(_mtx);
            for (std::size_t i = 0; i < _size; ++i) {
                const std::size_t idx = (_head + i) % _cfg.capacity;
                const auto& it = _buf[idx];
                if (it.tSubmitUs >= tMin && it.tSubmitUs <= tMax) {
                    tmp.push_back(Candidate{ AbsDiffU64(it.tSubmitUs, tRefUs), it });
                }
            }
        }

        if (tmp.empty()) {
            return out;
        }

        std::sort(tmp.begin(), tmp.end(), [](const Candidate& a, const Candidate& b) {
            return a.dtAbs < b.dtAbs;
            });

        const std::size_t n = std::min(maxOut, tmp.size());
        out.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            out.push_back(tmp[i].f);
        }

        _queryHitCount.fetch_add(1, std::memory_order_relaxed);
        return out;
    }

    std::vector<AudioChunkFeature> SubmitFeatureCache::QueryByVoiceAndTime(
        VoiceKey voice,
        std::uint64_t tRefUs,
        std::uint32_t lookbackUs,
        std::uint32_t lookaheadUs,
        std::size_t maxOut) const
    {
        _queryCount.fetch_add(1, std::memory_order_relaxed);

        std::vector<AudioChunkFeature> out;
        if (!_initialized.load(std::memory_order_acquire) || maxOut == 0) {
            return out;
        }

        const std::uint64_t tMin = (tRefUs > lookbackUs) ? (tRefUs - lookbackUs) : 0;
        const std::uint64_t tMax = tRefUs + lookaheadUs;

        struct Candidate
        {
            std::uint64_t dtAbs{ 0 };
            AudioChunkFeature f{};
        };
        std::vector<Candidate> tmp;
        tmp.reserve(32);

        {
            std::lock_guard<std::mutex> lock(_mtx);
            for (std::size_t i = 0; i < _size; ++i) {
                const std::size_t idx = (_head + i) % _cfg.capacity;
                const auto& it = _buf[idx];
                if (it.voice == voice && it.tSubmitUs >= tMin && it.tSubmitUs <= tMax) {
                    tmp.push_back(Candidate{ AbsDiffU64(it.tSubmitUs, tRefUs), it });
                }
            }
        }

        if (tmp.empty()) {
            return out;
        }

        std::sort(tmp.begin(), tmp.end(), [](const Candidate& a, const Candidate& b) {
            return a.dtAbs < b.dtAbs;
            });

        const std::size_t n = std::min(maxOut, tmp.size());
        out.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            out.push_back(tmp[i].f);
        }

        _queryHitCount.fetch_add(1, std::memory_order_relaxed);
        return out;
    }

    bool SubmitFeatureCache::GetVoiceProfile(VoiceKey voice, VoiceProfile& out) const
    {
        if (!_initialized.load(std::memory_order_acquire)) {
            return false;
        }

        std::lock_guard<std::mutex> lock(_mtx);
        const auto it = _voiceProfiles.find(voice);
        if (it == _voiceProfiles.end()) {
            return false;
        }

        out.voice = voice;
        out.seenCount = it->second.seenCount;
        out.avgRms = it->second.avgRms;
        out.avgPeak = it->second.avgPeak;
        out.avgDurationUs = it->second.avgDurationUs;
        out.avgLrImbalance = it->second.avgLrImbalance;
        out.lastSeenUs = it->second.lastSeenUs;
        return true;
    }

    std::size_t SubmitFeatureCache::SizeApprox() const
    {
        if (!_initialized.load(std::memory_order_acquire)) {
            return 0;
        }
        std::lock_guard<std::mutex> lock(_mtx);
        return _size;
    }

    std::size_t SubmitFeatureCache::Capacity() const
    {
        if (!_initialized.load(std::memory_order_acquire)) {
            return 0;
        }
        std::lock_guard<std::mutex> lock(_mtx);
        return _cfg.capacity;
    }

    void SubmitFeatureCache::ResetStats()
    {
        _pushCount.store(0, std::memory_order_relaxed);
        _overwriteCount.store(0, std::memory_order_relaxed);
        _pruneCount.store(0, std::memory_order_relaxed);
        _queryCount.store(0, std::memory_order_relaxed);
        _queryHitCount.store(0, std::memory_order_relaxed);
    }

    void SubmitFeatureCache::PruneExpiredLocked(std::uint64_t nowUs)
    {
        while (_size > 0) {
            const auto& oldest = _buf[_head];
            if (nowUs >= oldest.tSubmitUs && (nowUs - oldest.tSubmitUs) > _cfg.windowUs) {
                _head = (_head + 1) % _cfg.capacity;
                --_size;
                _pruneCount.fetch_add(1, std::memory_order_relaxed);
            }
            else {
                break;
            }
        }
    }

    void SubmitFeatureCache::UpdateVoiceProfileLocked(const AudioChunkFeature& f)
    {
        auto& p = _voiceProfiles[f.voice];
        p.seenCount += 1;
        const float n = static_cast<float>(p.seenCount);

        // EMA/在线均值（这里用在线均值）
        p.avgRms += (f.rms - p.avgRms) / n;
        p.avgPeak += (f.peak - p.avgPeak) / n;
        p.avgDurationUs += (static_cast<float>(f.durationUs) - p.avgDurationUs) / n;
        const float imb = LrImbalance(f.energyL, f.energyR);
        p.avgLrImbalance += (imb - p.avgLrImbalance) / n;
        p.lastSeenUs = f.tSubmitUs;
    }

    void SubmitFeatureCache::PruneVoiceProfilesLocked(std::uint64_t nowUs)
    {
        if (_voiceProfiles.empty()) {
            return;
        }

        for (auto it = _voiceProfiles.begin(); it != _voiceProfiles.end(); ) {
            const auto last = it->second.lastSeenUs;
            const bool expired = (nowUs >= last) && ((nowUs - last) > _cfg.voiceProfileTtlUs);
            if (expired) {
                it = _voiceProfiles.erase(it);
            }
            else {
                ++it;
            }
        }
    }
}  // namespace dualpad::haptics