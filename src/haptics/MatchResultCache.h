#pragma once
#include "haptics/HapticsTypes.h"

#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace dualpad::haptics
{
    class MatchResultCache
    {
    public:
        void Initialize(std::size_t capacity = 512);
        void Clear();

        bool TryGet(const EventToken& e, std::uint64_t nowUs, HapticSourceMsg& out);
        void Put(const EventToken& e, const HapticSourceMsg& src, float score, std::uint64_t nowUs);

    private:
        struct Key
        {
            EventType type{ EventType::Unknown };
            SemanticGroup semantic{ SemanticGroup::Unknown };
            std::uint32_t actorId{ 0 };
            std::uint32_t formId{ 0 };
            std::uint8_t intensityQ{ 0 }; // 0..7

            bool operator==(const Key& o) const
            {
                return type == o.type &&
                    semantic == o.semantic &&
                    actorId == o.actorId &&
                    formId == o.formId &&
                    intensityQ == o.intensityQ;
            }
        };

        struct KeyHash
        {
            std::size_t operator()(const Key& k) const noexcept
            {
                std::size_t h = 1469598103934665603ull;
                auto mix = [&](std::uint64_t v) {
                    h ^= static_cast<std::size_t>(v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2));
                    };
                mix(static_cast<std::uint64_t>(k.type));
                mix(static_cast<std::uint64_t>(k.semantic));
                mix(static_cast<std::uint64_t>(k.actorId));
                mix(static_cast<std::uint64_t>(k.formId));
                mix(static_cast<std::uint64_t>(k.intensityQ));
                return h;
            }
        };

        struct Entry
        {
            HapticSourceMsg src{};
            float score{ 0.0f };
            std::uint64_t tsUs{ 0 };
            std::uint64_t hits{ 0 };
        };

        static Key MakeKey(const EventToken& e);
        static std::uint64_t TtlUsFor(EventType t);

        void PruneExpired(std::uint64_t nowUs);

        std::mutex _mx;
        std::unordered_map<Key, Entry, KeyHash> _map;
        std::size_t _capacity{ 512 };
    };
}