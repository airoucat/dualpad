#include "pch.h"
#include "haptics/EventShortWindowCache.h"

#include <SKSE/SKSE.h>
#include <algorithm>
#include <chrono>

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
}

namespace dualpad::haptics
{
    EventShortWindowCache& EventShortWindowCache::GetSingleton()
    {
        static EventShortWindowCache s;
        return s;
    }

    void EventShortWindowCache::Initialize(const Config& cfg)
    {
        Config c = cfg;
        if (c.capacity == 0) {
            c.capacity = 1;
        }
        if (c.windowUs == 0) {
            c.windowUs = 200'000;
        }

        bool expected = false;
        if (!_initialized.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            logger::warn("[Haptics][EventShortWindowCache] already initialized");
            return;
        }

        {
            std::lock_guard<std::mutex> lock(_mtx);
            _cfg = c;
            _buf.assign(_cfg.capacity, EventToken{});
            _head = 0;
            _size = 0;
        }

        ResetStats();
        logger::info(
            "[Haptics][EventShortWindowCache] initialized capacity={} windowUs={}",
            _cfg.capacity, _cfg.windowUs);
    }

    void EventShortWindowCache::Shutdown()
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
        }

        logger::info(
            "[Haptics][EventShortWindowCache] shutdown push={} overwrite={} prune={} query={} hit={}",
            _pushCount.load(std::memory_order_relaxed),
            _overwriteCount.load(std::memory_order_relaxed),
            _pruneCount.load(std::memory_order_relaxed),
            _queryCount.load(std::memory_order_relaxed),
            _queryHitCount.load(std::memory_order_relaxed));
    }

    bool EventShortWindowCache::Push(const EventToken& tokenIn)
    {
        if (!_initialized.load(std::memory_order_acquire)) {
            return false;
        }

        EventToken token = tokenIn;
        if (token.tEventUs == 0) {
            token.tEventUs = SteadyNowUs();
        }

        std::lock_guard<std::mutex> lock(_mtx);

        PruneExpiredLocked(token.tEventUs);

        const auto cap = _cfg.capacity;
        if (_size < cap) {
            const std::size_t tail = (_head + _size) % cap;
            _buf[tail] = token;
            ++_size;
        }
        else {
            // ÂúÁË£º¸²¸Ç×î¾É
            _buf[_head] = token;
            _head = (_head + 1) % cap;
            _overwriteCount.fetch_add(1, std::memory_order_relaxed);
        }

        _pushCount.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    std::vector<EventToken> EventShortWindowCache::QueryAround(
        std::uint64_t tRefUs,
        std::uint32_t lookbackUs,
        std::uint32_t lookaheadUs,
        std::size_t maxOut) const
    {
        _queryCount.fetch_add(1, std::memory_order_relaxed);

        std::vector<EventToken> out;
        if (!_initialized.load(std::memory_order_acquire) || maxOut == 0) {
            return out;
        }

        const std::uint64_t tMin = (tRefUs > lookbackUs) ? (tRefUs - lookbackUs) : 0;
        const std::uint64_t tMax = tRefUs + lookaheadUs;

        struct Candidate
        {
            std::uint64_t dtAbs{ 0 };
            EventToken token{};
        };
        std::vector<Candidate> tmp;
        tmp.reserve(32);

        {
            std::lock_guard<std::mutex> lock(_mtx);

            if (_size == 0) {
                return out;
            }

            for (std::size_t i = 0; i < _size; ++i) {
                const std::size_t idx = (_head + i) % _cfg.capacity;
                const auto& e = _buf[idx];
                if (e.tEventUs >= tMin && e.tEventUs <= tMax) {
                    tmp.push_back(Candidate{ AbsDiffU64(e.tEventUs, tRefUs), e });
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
            out.push_back(tmp[i].token);
        }

        _queryHitCount.fetch_add(1, std::memory_order_relaxed);
        return out;
    }

    std::size_t EventShortWindowCache::SizeApprox() const
    {
        if (!_initialized.load(std::memory_order_acquire)) {
            return 0;
        }
        std::lock_guard<std::mutex> lock(_mtx);
        return _size;
    }

    std::size_t EventShortWindowCache::Capacity() const
    {
        if (!_initialized.load(std::memory_order_acquire)) {
            return 0;
        }
        std::lock_guard<std::mutex> lock(_mtx);
        return _cfg.capacity;
    }

    void EventShortWindowCache::ResetStats()
    {
        _pushCount.store(0, std::memory_order_relaxed);
        _overwriteCount.store(0, std::memory_order_relaxed);
        _pruneCount.store(0, std::memory_order_relaxed);
        _queryCount.store(0, std::memory_order_relaxed);
        _queryHitCount.store(0, std::memory_order_relaxed);
    }

    void EventShortWindowCache::PruneExpiredLocked(std::uint64_t nowUs)
    {
        if (_size == 0) {
            return;
        }

        while (_size > 0) {
            const auto& oldest = _buf[_head];
            if (nowUs >= oldest.tEventUs && (nowUs - oldest.tEventUs) > _cfg.windowUs) {
                _head = (_head + 1) % _cfg.capacity;
                --_size;
                _pruneCount.fetch_add(1, std::memory_order_relaxed);
            }
            else {
                break;
            }
        }
    }
}  // namespace dualpad::haptics