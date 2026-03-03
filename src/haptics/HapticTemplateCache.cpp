#include "pch.h"
#include "haptics/HapticTemplateCache.h"

namespace dualpad::haptics
{
    HapticTemplateCache& HapticTemplateCache::GetSingleton()
    {
        static HapticTemplateCache s;
        return s;
    }

    void HapticTemplateCache::WarmupDefaults()
    {
        std::scoped_lock lk(_mx);

        HapticTemplate def{};
        for (auto& t : _table) {
            t = def;
        }

        auto set = [this](EventType e, HapticTemplate t) {
            _table[static_cast<std::uint8_t>(e)] = t;
            };

        set(EventType::Footstep, HapticTemplate{ 0.22f, 1.0f, 1.0f, 70,  0.18f, 0.18f, 30 });
        set(EventType::WeaponSwing, HapticTemplate{ 0.55f, 1.0f, 1.0f, 120, 0.30f, 0.25f, 36 });
        set(EventType::HitImpact, HapticTemplate{ 0.90f, 1.0f, 1.0f, 110, 0.40f, 0.30f, 40 });
        set(EventType::Block, HapticTemplate{ 0.65f, 1.0f, 1.0f, 90,  0.28f, 0.25f, 34 });
        set(EventType::BowRelease, HapticTemplate{ 0.52f, 1.0f, 1.0f, 95,  0.24f, 0.22f, 34 });
        set(EventType::Jump, HapticTemplate{ 0.34f, 1.0f, 1.0f, 80,  0.20f, 0.18f, 30 });
        set(EventType::Land, HapticTemplate{ 0.45f, 1.0f, 1.0f, 90,  0.26f, 0.20f, 34 });

        _ready.store(true, std::memory_order_release);
    }

    const HapticTemplate& HapticTemplateCache::Get(EventType t) const
    {
        static HapticTemplate fallback{};
        if (!_ready.load(std::memory_order_acquire)) {
            return fallback;
        }

        std::scoped_lock lk(_mx);
        return _table[static_cast<std::uint8_t>(t)];
    }
}