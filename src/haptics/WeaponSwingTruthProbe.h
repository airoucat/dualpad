#pragma once

#include <RE/Skyrim.h>

#include <atomic>
#include <cstdint>

namespace dualpad::haptics
{
    class WeaponSwingTruthProbe :
        public RE::BSTEventSink<RE::BSAnimationGraphEvent>
    {
    public:
        struct Stats
        {
            std::uint64_t totalEvents{ 0 };
            std::uint64_t playerEvents{ 0 };
            std::uint64_t swingMatched{ 0 };
            std::uint64_t swingSubmitted{ 0 };
            std::uint64_t attackLikeRejected{ 0 };
        };

        static WeaponSwingTruthProbe& GetSingleton();

        bool Register();
        void Unregister();
        Stats GetStats() const;

        RE::BSEventNotifyControl ProcessEvent(
            const RE::BSAnimationGraphEvent* event,
            RE::BSTEventSource<RE::BSAnimationGraphEvent>* source) override;

    private:
        WeaponSwingTruthProbe() = default;
        void ResetStats();

        std::atomic<bool> _registered{ false };
        std::atomic<std::uint64_t> _totalEvents{ 0 };
        std::atomic<std::uint64_t> _playerEvents{ 0 };
        std::atomic<std::uint64_t> _swingMatched{ 0 };
        std::atomic<std::uint64_t> _swingSubmitted{ 0 };
        std::atomic<std::uint64_t> _attackLikeRejected{ 0 };
    };
}
