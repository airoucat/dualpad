#pragma once

#include <RE/Skyrim.h>

#include <atomic>
#include <cstdint>

namespace dualpad::haptics
{
    class HitImpactTruthProbe :
        public RE::BSTEventSink<RE::TESHitEvent>
    {
    public:
        struct Stats
        {
            std::uint64_t totalEvents{ 0 };
            std::uint64_t playerEvents{ 0 };
            std::uint64_t playerCauseEvents{ 0 };
            std::uint64_t playerTargetEvents{ 0 };
            std::uint64_t hitMatched{ 0 };
            std::uint64_t blockMatched{ 0 };
            std::uint64_t hitSubmitted{ 0 };
            std::uint64_t blockSubmitted{ 0 };
        };

        static HitImpactTruthProbe& GetSingleton();

        bool Register();
        void Unregister();
        Stats GetStats() const;

        RE::BSEventNotifyControl ProcessEvent(
            const RE::TESHitEvent* event,
            RE::BSTEventSource<RE::TESHitEvent>* source) override;

    private:
        HitImpactTruthProbe() = default;
        void ResetStats();

        std::atomic<bool> _registered{ false };
        std::atomic<std::uint64_t> _totalEvents{ 0 };
        std::atomic<std::uint64_t> _playerEvents{ 0 };
        std::atomic<std::uint64_t> _playerCauseEvents{ 0 };
        std::atomic<std::uint64_t> _playerTargetEvents{ 0 };
        std::atomic<std::uint64_t> _hitMatched{ 0 };
        std::atomic<std::uint64_t> _blockMatched{ 0 };
        std::atomic<std::uint64_t> _hitSubmitted{ 0 };
        std::atomic<std::uint64_t> _blockSubmitted{ 0 };
    };
}
