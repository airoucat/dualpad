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
        auto snap = std::make_shared<Snapshot>();
        snap->table.reserve(512);
        std::atomic_store_explicit(&_snapshot, std::shared_ptr<const Snapshot>(snap), std::memory_order_release);
    }

    void FormSemanticCache::InstallSnapshot(std::shared_ptr<const Snapshot> snapshot)
    {
        if (!snapshot) {
            return;
        }
        std::atomic_store_explicit(&_snapshot, std::move(snapshot), std::memory_order_release);
    }

    void FormSemanticCache::InstallFromRecords(
        const std::vector<FormSemanticRecord>& records,
        std::uint64_t fingerprintHash,
        std::uint32_t rulesVersion)
    {
        auto snap = std::make_shared<Snapshot>();
        snap->table.reserve(records.size() * 2 + 64);
        snap->fingerprintHash = fingerprintHash;
        snap->rulesVersion = rulesVersion;

        for (const auto& r : records) {
            if (r.formId == 0) {
                continue;
            }
            snap->table[r.formId] = r.meta;
        }

        InstallSnapshot(std::shared_ptr<const Snapshot>(snap));
    }

    void FormSemanticCache::Set(std::uint32_t formId, const SemanticMeta& meta)
    {
        if (formId == 0) {
            return;
        }

        auto cur = std::atomic_load_explicit(&_snapshot, std::memory_order_acquire);
        auto next = std::make_shared<Snapshot>();

        if (cur) {
            *next = *cur;
        }
        else {
            next->table.reserve(1024);
        }

        next->table[formId] = meta;
        InstallSnapshot(std::shared_ptr<const Snapshot>(next));
    }

    FormSemanticCache::Value FormSemanticCache::Resolve(std::uint32_t formId, EventType fallbackType) const
    {
        _queries.fetch_add(1, std::memory_order_relaxed);

        auto snap = std::atomic_load_explicit(&_snapshot, std::memory_order_acquire);
        if (snap && formId != 0) {
            auto it = snap->table.find(formId);
            if (it != snap->table.end()) {
                _hits.fetch_add(1, std::memory_order_relaxed);

                Value v{};
                v.group = it->second.group;
                v.confidence = it->second.confidence;
                v.baseWeight = it->second.baseWeight;
                v.weight = it->second.baseWeight;  // ¼æÈÝ¾Éµ÷ÓÃ
                v.texturePresetId = it->second.texturePresetId;
                v.flags = it->second.flags;
                return v;
            }
        }

        _fallbacks.fetch_add(1, std::memory_order_relaxed);

        Value fb{};
        fb.group = FallbackFromEvent(fallbackType);
        fb.confidence = 0.45f;
        fb.baseWeight = 0.55f;
        fb.weight = fb.baseWeight;
        fb.texturePresetId = 0;
        fb.flags = SemanticFlags::None;

        if (fb.group == SemanticGroup::Unknown) {
            _unknownFallbacks.fetch_add(1, std::memory_order_relaxed);
        }

        return fb;
    }

    std::shared_ptr<const FormSemanticCache::Snapshot> FormSemanticCache::GetSnapshot() const
    {
        return std::atomic_load_explicit(&_snapshot, std::memory_order_acquire);
    }

    std::size_t FormSemanticCache::Size() const
    {
        auto s = GetSnapshot();
        return s ? s->table.size() : 0;
    }

    FormSemanticCache::Stats FormSemanticCache::GetStats() const
    {
        Stats s{};
        s.queries = _queries.load(std::memory_order_relaxed);
        s.hits = _hits.load(std::memory_order_relaxed);
        s.fallbacks = _fallbacks.load(std::memory_order_relaxed);
        s.unknownFallbacks = _unknownFallbacks.load(std::memory_order_relaxed);
        s.snapshotSize = Size();
        return s;
    }

    void FormSemanticCache::ResetStats()
    {
        _queries.store(0, std::memory_order_relaxed);
        _hits.store(0, std::memory_order_relaxed);
        _fallbacks.store(0, std::memory_order_relaxed);
        _unknownFallbacks.store(0, std::memory_order_relaxed);
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