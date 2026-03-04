#include "pch.h"
#include "haptics/InstanceTraceCache.h"

namespace dualpad::haptics
{
    InstanceTraceCache& InstanceTraceCache::GetSingleton()
    {
        static InstanceTraceCache s;
        return s;
    }

    void InstanceTraceCache::Upsert(const TraceMeta& m)
    {
        {
            std::unique_lock lk(_mx);
            _map[m.instanceId] = m;
        }
        _upserts.fetch_add(1, std::memory_order_relaxed);
    }

    std::optional<TraceMeta> InstanceTraceCache::TryGet(std::uint64_t instanceId) const
    {
        std::shared_lock lk(_mx);
        auto it = _map.find(instanceId);
        if (it == _map.end()) {
            _misses.fetch_add(1, std::memory_order_relaxed);
            return std::nullopt;
        }
        _hits.fetch_add(1, std::memory_order_relaxed);
        return it->second;
    }

    void InstanceTraceCache::Erase(std::uint64_t instanceId)
    {
        std::unique_lock lk(_mx);
        _map.erase(instanceId);
    }

    void InstanceTraceCache::Clear()
    {
        std::unique_lock lk(_mx);
        _map.clear();
    }

    InstanceTraceCache::Stats InstanceTraceCache::GetStats() const
    {
        return {
            _upserts.load(std::memory_order_relaxed),
            _hits.load(std::memory_order_relaxed),
            _misses.load(std::memory_order_relaxed)
        };
    }
}