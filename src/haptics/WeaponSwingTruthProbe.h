#pragma once

#include <RE/Skyrim.h>

#include <atomic>
#include <array>
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
        void Tick(std::uint64_t nowUs);
        Stats GetStats() const;

        RE::BSEventNotifyControl ProcessEvent(
            const RE::BSAnimationGraphEvent* event,
            RE::BSTEventSource<RE::BSAnimationGraphEvent>* source) override;

    private:
        WeaponSwingTruthProbe() = default;
        void ResetStats();
        bool TryRegisterSink(bool logFailure);

        std::atomic<bool> _registered{ false };
        std::atomic<bool> _wantRegistered{ false };
        std::atomic<bool> _statsResetForSession{ false };
        std::atomic<std::uint64_t> _lastRegisterAttemptUs{ 0 };
        std::atomic<std::uint64_t> _totalEvents{ 0 };
        std::atomic<std::uint64_t> _playerEvents{ 0 };
        std::atomic<std::uint64_t> _swingMatched{ 0 };
        std::atomic<std::uint64_t> _swingSubmitted{ 0 };
        std::atomic<std::uint64_t> _attackLikeRejected{ 0 };
        std::atomic<std::uint64_t> _lastSwingSubmitUs{ 0 };
    };
}
