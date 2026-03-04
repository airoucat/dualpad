#pragma once

#include "haptics/HapticsTypes.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace dualpad::haptics
{
    class EventShortWindowCache
    {
    public:
        struct Config
        {
            std::size_t capacity{ 128 };           // 推荐 64~256
            std::uint32_t windowUs{ 200'000 };     // 默认 200ms
        };

        static EventShortWindowCache& GetSingleton();

        void Initialize(const Config& cfg = {});
        void Shutdown();

        // 写入事件（生产者可多线程）
        bool Push(const EventToken& token);

        // 查询：给音频时间点找短窗内候选事件
        // 返回按 |tEvent - tRef| 升序（越近越前）
        std::vector<EventToken> QueryAround(
            std::uint64_t tRefUs,
            std::uint32_t lookbackUs = 120'000,
            std::uint32_t lookaheadUs = 30'000,
            std::size_t maxOut = 16) const;

        std::size_t SizeApprox() const;
        std::size_t Capacity() const;

        // 统计
        std::uint64_t GetPushCount() const { return _pushCount.load(std::memory_order_relaxed); }
        std::uint64_t GetOverwriteCount() const { return _overwriteCount.load(std::memory_order_relaxed); }
        std::uint64_t GetPruneCount() const { return _pruneCount.load(std::memory_order_relaxed); }
        std::uint64_t GetQueryCount() const { return _queryCount.load(std::memory_order_relaxed); }
        std::uint64_t GetQueryHitCount() const { return _queryHitCount.load(std::memory_order_relaxed); }

        void ResetStats();

    private:
        EventShortWindowCache() = default;
        EventShortWindowCache(const EventShortWindowCache&) = delete;
        EventShortWindowCache& operator=(const EventShortWindowCache&) = delete;

        void PruneExpiredLocked(std::uint64_t nowUs);

        mutable std::mutex _mtx;
        std::vector<EventToken> _buf;  // ring storage
        std::size_t _head{ 0 };        // oldest index
        std::size_t _size{ 0 };        // valid count
        Config _cfg{};

        std::atomic<bool> _initialized{ false };

        mutable std::atomic<std::uint64_t> _pushCount{ 0 };
        mutable std::atomic<std::uint64_t> _overwriteCount{ 0 };
        mutable std::atomic<std::uint64_t> _pruneCount{ 0 };
        mutable std::atomic<std::uint64_t> _queryCount{ 0 };
        mutable std::atomic<std::uint64_t> _queryHitCount{ 0 };
    };
}  // namespace dualpad::haptics