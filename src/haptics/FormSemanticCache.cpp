#include "pch.h"
#include "haptics/FormSemanticCache.h"

#include <algorithm>

namespace dualpad::haptics
{
    FormSemanticCache& FormSemanticCache::GetSingleton()
    {
        static FormSemanticCache s;
        return s;
    }

    void FormSemanticCache::WarmupDefaults()
    {
        std::unique_lock lk(_mx);
        _map.reserve(512);
    }

    void FormSemanticCache::Set(std::uint32_t formId, SemanticGroup group, float weight, std::uint32_t ttlMs)
    {
        const auto nowUs = ToQPC(Now());

        Value v{};
        v.group = group;
        v.weight = std::clamp(weight, 0.0f, 1.0f);
        v.expireUs = (ttlMs == 0) ? 0 : nowUs + static_cast<std::uint64_t>(ttlMs) * 1000ULL;
        v.stampUs = nowUs;

        std::unique_lock lk(_mx);
        _map[formId] = v;
        _fifo.push_back(formId);

        if (_map.size() > _maxEntries) {
            constexpr std::size_t kKick = 256;
            for (std::size_t i = 0; i < kKick && !_fifo.empty() && _map.size() > _maxEntries; ++i) {
                const auto k = _fifo.front();
                _fifo.pop_front();
                _map.erase(k);
            }
        }
    }

    FormSemanticCache::Value FormSemanticCache::Resolve(std::uint32_t formId, EventType fallbackType) const
    {
        const auto nowUs = ToQPC(Now());

        if (formId != 0) {
            std::shared_lock lk(_mx);
            auto it = _map.find(formId);
            if (it != _map.end()) {
                const auto& v = it->second;
                if (v.expireUs == 0 || nowUs <= v.expireUs) {
                    return v;
                }
            }
        }

        Value fb{};
        fb.group = FallbackFromEvent(fallbackType);
        fb.weight = 0.55f;
        fb.expireUs = 0;
        fb.stampUs = nowUs;
        return fb;
    }

    void FormSemanticCache::PruneExpired(std::uint64_t nowUs)
    {
        std::unique_lock lk(_mx);
        for (auto it = _map.begin(); it != _map.end();) {
            if (it->second.expireUs != 0 && nowUs > it->second.expireUs) {
                it = _map.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    SemanticGroup FormSemanticCache::FallbackFromEvent(EventType type) const
    {
        switch (type) {
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
}