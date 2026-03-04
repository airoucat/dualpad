#include "pch.h"
#include "haptics/MatchResultCache.h"

#include <algorithm>
#include <limits>

namespace dualpad::haptics
{
    void MatchResultCache::Initialize(std::size_t capacity)
    {
        std::scoped_lock lk(_mx);
        _capacity = std::max<std::size_t>(64, capacity);
        _map.clear();
        _map.reserve(_capacity);
    }

    void MatchResultCache::Clear()
    {
        std::scoped_lock lk(_mx);
        _map.clear();
    }

    MatchResultCache::Key MatchResultCache::MakeKey(const EventToken& e)
    {
        Key k{};
        k.type = e.eventType;

        // 默认
        k.semantic = e.semantic;
        k.actorId = e.actorId;
        k.formId = e.formId;

        const float x = std::clamp(e.intensityHint, 0.0f, 0.999f);
        k.intensityQ = static_cast<std::uint8_t>(x * 8.0f); // 0..7

        // 重复高频事件：强放松（提高命中）
        switch (e.eventType) {
        case EventType::Jump:
        case EventType::Land:
            k.semantic = SemanticGroup::Footstep; // 统一语义
            k.actorId = 0;                        // 忽略actor抖动
            k.formId = 0;                         // 忽略form抖动
            k.intensityQ = 0;                     // 忽略强度抖动
            break;

        case EventType::Footstep:
            k.semantic = SemanticGroup::Footstep;
            k.formId = 0;                         // 脚步常不稳定，先忽略form
            k.intensityQ = static_cast<std::uint8_t>(x * 4.0f); // 降桶数 0..3
            break;

        default:
            break;
        }

        return k;
    }

    std::uint64_t MatchResultCache::TtlUsFor(EventType t)
    {
        switch (t) {
        case EventType::Footstep:    return 300'000;   // 300ms
        case EventType::WeaponSwing: return 220'000;   // 220ms
        case EventType::HitImpact:   return 180'000;   // 180ms
        case EventType::Jump:
        case EventType::Land:        return 900'000;   // 900ms（关键）
        default:                     return 160'000;
        }
    }

    void MatchResultCache::PruneExpired(std::uint64_t nowUs)
    {
        // 先删过期
        for (auto it = _map.begin(); it != _map.end();) {
            const auto ttl = TtlUsFor(it->first.type);
            if (nowUs > it->second.tsUs && (nowUs - it->second.tsUs) > ttl) {
                it = _map.erase(it);
            }
            else {
                ++it;
            }
        }

        // 超容量时删“最旧”条目（稳定）
        while (_map.size() > _capacity) {
            auto oldestIt = _map.end();
            std::uint64_t oldestTs = std::numeric_limits<std::uint64_t>::max();

            for (auto it = _map.begin(); it != _map.end(); ++it) {
                if (it->second.tsUs < oldestTs) {
                    oldestTs = it->second.tsUs;
                    oldestIt = it;
                }
            }

            if (oldestIt != _map.end()) {
                _map.erase(oldestIt);
            }
            else {
                break;
            }
        }
    }

    bool MatchResultCache::TryGet(const EventToken& e, std::uint64_t nowUs, HapticSourceMsg& out)
    {
        std::scoped_lock lk(_mx);

        PruneExpired(nowUs);

        const auto k = MakeKey(e);
        auto it = _map.find(k);
        if (it == _map.end()) {
            return false;
        }

        const auto ttl = TtlUsFor(e.eventType);
        if (nowUs <= it->second.tsUs || (nowUs - it->second.tsUs) > ttl) {
            _map.erase(it);
            return false;
        }

        out = it->second.src;
        out.qpc = nowUs;   // 复用时刷新时间戳
        it->second.hits++;
        return true;
    }

    void MatchResultCache::Put(const EventToken& e, const HapticSourceMsg& src, float score, std::uint64_t nowUs)
    {
        std::scoped_lock lk(_mx);

        PruneExpired(nowUs);

        const auto k = MakeKey(e);

        Entry en{};
        en.src = src;
        en.score = score;
        en.tsUs = nowUs;
        en.hits = 0;

        _map[k] = en;
    }
}