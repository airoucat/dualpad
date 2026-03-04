#pragma once
#include "haptics/HapticsTypes.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

namespace dualpad::haptics
{
    struct HapticTemplate
    {
        // Á¢¼´²ã
        float immediateAmp{ 0.20f };
        float leftGain{ 1.0f };
        float rightGain{ 1.0f };
        std::uint32_t immediateTtlMs{ 80 };

        // ÐÞÕý²ã
        float correctionAmp{ 0.25f };
        float maxPan{ 0.20f };
        std::uint32_t correctionTtlMs{ 36 };
    };

    class HapticTemplateCache
    {
    public:
        static HapticTemplateCache& GetSingleton();

        void WarmupDefaults();
        const HapticTemplate& Get(EventType t) const;
        bool IsReady() const { return _ready.load(std::memory_order_acquire); }

    private:
        HapticTemplateCache() = default;

        mutable std::mutex _mx;
        std::array<HapticTemplate, 256> _table{};
        std::atomic<bool> _ready{ false };
    };
}