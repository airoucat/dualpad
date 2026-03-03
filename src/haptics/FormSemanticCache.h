#pragma once
#include "haptics/HapticsTypes.h"

#include <cstdint>
#include <deque>
#include <shared_mutex>
#include <unordered_map>

namespace dualpad::haptics
{
    class FormSemanticCache
    {
    public:
        struct Value
        {
            SemanticGroup group{ SemanticGroup::Unknown };
            float weight{ 0.5f };
            std::uint64_t expireUs{ 0 }; // 0=永不过期
            std::uint64_t stampUs{ 0 };
        };

        static FormSemanticCache& GetSingleton();

        // 启动预热（当前先预置 fallback 逻辑，后续可接数据库）
        void WarmupDefaults();

        void Set(std::uint32_t formId, SemanticGroup group, float weight, std::uint32_t ttlMs);
        Value Resolve(std::uint32_t formId, EventType fallbackType) const;
        void PruneExpired(std::uint64_t nowUs);

    private:
        FormSemanticCache() = default;
        SemanticGroup FallbackFromEvent(EventType type) const;

        mutable std::shared_mutex _mx;
        std::unordered_map<std::uint32_t, Value> _map;
        std::deque<std::uint32_t> _fifo;
        std::size_t _maxEntries{ 8192 };
    };
}