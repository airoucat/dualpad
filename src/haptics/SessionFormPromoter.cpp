#include "pch.h"
#include "haptics/SessionFormPromoter.h"

#include <algorithm>
#include <mutex>

namespace dualpad::haptics
{
    namespace
    {
        constexpr std::uint32_t kMinHits = 3;
        constexpr std::uint64_t kMaxAgeUs = 10ull * 1000ull * 1000ull;
        constexpr float kMinConfidence = 0.52f;
        constexpr std::size_t kMaxEntries = 2048;
    }

    SessionFormPromoter& SessionFormPromoter::GetSingleton()
    {
        static SessionFormPromoter s;
        return s;
    }

    bool SessionFormPromoter::IsPromotableEvent(EventType eventType)
    {
        switch (eventType) {
        case EventType::Footstep:
        case EventType::WeaponSwing:
        case EventType::HitImpact:
        case EventType::SpellCast:
        case EventType::SpellImpact:
        case EventType::BowRelease:
        case EventType::Jump:
        case EventType::Land:
        case EventType::Block:
        case EventType::Shout:
            return true;
        default:
            return false;
        }
    }

    void SessionFormPromoter::Observe(
        std::uint32_t formId,
        EventType eventType,
        float confidence,
        std::uint64_t nowUs)
    {
        _observeCalls.fetch_add(1, std::memory_order_relaxed);

        if (formId == 0 || !IsPromotableEvent(eventType)) {
            return;
        }

        confidence = std::clamp(confidence, 0.0f, 1.0f);
        if (confidence < 0.30f) {
            return;
        }

        std::unique_lock lk(_mx);
        auto it = _entries.find(formId);
        if (it == _entries.end()) {
            if (_entries.size() >= kMaxEntries) {
                auto worst = _entries.end();
                float worstRank = 10.0f;
                for (auto iter = _entries.begin(); iter != _entries.end(); ++iter) {
                    const float rank = iter->second.confidence + 0.03f * static_cast<float>(iter->second.hits);
                    if (rank < worstRank) {
                        worstRank = rank;
                        worst = iter;
                    }
                }
                if (worst != _entries.end()) {
                    _entries.erase(worst);
                }
            }

            Entry e{};
            e.eventType = eventType;
            e.confidence = confidence;
            e.hits = 1;
            e.lastSeenUs = nowUs;
            _entries.emplace(formId, e);
            _observedAccepted.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        auto& entry = it->second;
        const bool expired = (nowUs > entry.lastSeenUs) && ((nowUs - entry.lastSeenUs) > kMaxAgeUs);
        if (expired) {
            entry.eventType = eventType;
            entry.confidence = confidence;
            entry.hits = 1;
            entry.lastSeenUs = nowUs;
            _observedAccepted.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        if (entry.eventType == eventType) {
            constexpr float kAlpha = 0.28f;
            entry.confidence = std::clamp(
                entry.confidence * (1.0f - kAlpha) + confidence * kAlpha,
                0.0f,
                1.0f);
            entry.hits = std::min<std::uint32_t>(entry.hits + 1, 255);
            entry.lastSeenUs = nowUs;
            _observedAccepted.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        if (confidence >= entry.confidence + 0.10f) {
            entry.eventType = eventType;
            entry.confidence = confidence;
            entry.hits = 1;
            entry.lastSeenUs = nowUs;
            _observedAccepted.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        entry.confidence = std::max(0.0f, entry.confidence - 0.01f);
        entry.lastSeenUs = nowUs;
    }

    bool SessionFormPromoter::TryPromote(
        std::uint32_t formId,
        std::uint64_t nowUs,
        EventType& outEventType,
        float& outConfidence) const
    {
        _promoteCalls.fetch_add(1, std::memory_order_relaxed);

        if (formId == 0) {
            _promoteMisses.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        std::shared_lock lk(_mx);
        const auto it = _entries.find(formId);
        if (it == _entries.end()) {
            _promoteMisses.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        const auto& entry = it->second;
        if (!IsPromotableEvent(entry.eventType)) {
            _promoteMisses.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        const bool expired = (nowUs > entry.lastSeenUs) && ((nowUs - entry.lastSeenUs) > kMaxAgeUs);
        if (expired || entry.hits < kMinHits || entry.confidence < kMinConfidence) {
            _promoteMisses.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        outEventType = entry.eventType;
        outConfidence = entry.confidence;
        _promoteHits.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    SessionFormPromoter::Stats SessionFormPromoter::GetStats() const
    {
        std::shared_lock lk(_mx);
        Stats s{};
        s.observeCalls = _observeCalls.load(std::memory_order_relaxed);
        s.observedAccepted = _observedAccepted.load(std::memory_order_relaxed);
        s.promoteCalls = _promoteCalls.load(std::memory_order_relaxed);
        s.promoteHits = _promoteHits.load(std::memory_order_relaxed);
        s.promoteMisses = _promoteMisses.load(std::memory_order_relaxed);
        s.entries = static_cast<std::uint64_t>(_entries.size());
        return s;
    }

    void SessionFormPromoter::Reset()
    {
        {
            std::unique_lock lk(_mx);
            _entries.clear();
        }

        _observeCalls.store(0, std::memory_order_relaxed);
        _observedAccepted.store(0, std::memory_order_relaxed);
        _promoteCalls.store(0, std::memory_order_relaxed);
        _promoteHits.store(0, std::memory_order_relaxed);
        _promoteMisses.store(0, std::memory_order_relaxed);
    }
}
