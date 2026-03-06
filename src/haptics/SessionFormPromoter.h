#pragma once

#include "haptics/HapticsTypes.h"

#include <atomic>
#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <unordered_map>

namespace dualpad::haptics
{
    class SessionFormPromoter
    {
    public:
        struct Stats
        {
            std::uint64_t observeCalls{ 0 };
            std::uint64_t observedAccepted{ 0 };
            std::uint64_t promoteCalls{ 0 };
            std::uint64_t promoteHits{ 0 };
            std::uint64_t promoteMisses{ 0 };
            std::uint64_t entries{ 0 };
        };

        static SessionFormPromoter& GetSingleton();

        void Observe(std::uint32_t formId, EventType eventType, float confidence, std::uint64_t nowUs);
        bool TryPromote(std::uint32_t formId, std::uint64_t nowUs, EventType& outEventType, float& outConfidence) const;

        Stats GetStats() const;
        void Reset();

    private:
        struct Entry
        {
            EventType eventType{ EventType::Unknown };
            float confidence{ 0.0f };
            std::uint32_t hits{ 0 };
            std::uint64_t lastSeenUs{ 0 };
        };

        SessionFormPromoter() = default;

        static bool IsPromotableEvent(EventType eventType);

        mutable std::shared_mutex _mx;
        std::unordered_map<std::uint32_t, Entry> _entries;

        std::atomic<std::uint64_t> _observeCalls{ 0 };
        std::atomic<std::uint64_t> _observedAccepted{ 0 };
        mutable std::atomic<std::uint64_t> _promoteCalls{ 0 };
        mutable std::atomic<std::uint64_t> _promoteHits{ 0 };
        mutable std::atomic<std::uint64_t> _promoteMisses{ 0 };
    };
}

