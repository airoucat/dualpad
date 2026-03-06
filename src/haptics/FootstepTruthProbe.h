#pragma once

#include "haptics/HapticsTypes.h"
#include <RE/Skyrim.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace dualpad::haptics
{
    class FootstepTruthProbe :
        public RE::BSTEventSink<RE::BGSFootstepEvent>
    {
    public:
        struct Stats
        {
            std::uint64_t totalEvents{ 0 };
            std::uint64_t actorResolvedEvents{ 0 };
            std::uint64_t playerEvents{ 0 };
            std::uint64_t nonPlayerEvents{ 0 };
            std::uint64_t contextAllowedEvents{ 0 };
            std::uint64_t contextBlockedEvents{ 0 };
            std::uint64_t movingEvents{ 0 };
            std::uint64_t recentMoveEvents{ 0 };
            std::uint64_t admissibleEvents{ 0 };
            std::uint64_t shadowMatchedRenders{ 0 };
            std::uint64_t shadowExpiredTruthMisses{ 0 };
            std::uint64_t shadowRenderWithoutTruth{ 0 };
            std::uint32_t shadowRenderDeltaP50Us{ 0 };
            std::uint32_t shadowRenderDeltaP95Us{ 0 };
            std::uint32_t shadowRenderDeltaSamples{ 0 };
            std::uint32_t shadowPendingTruth{ 0 };
        };

        static FootstepTruthProbe& GetSingleton();

        bool Register();
        void Unregister();
        Stats GetStats() const;
        void ObserveShadowRender(
            std::uint64_t renderUs,
            std::uint64_t seq,
            EventType eventType,
            std::uint8_t leftMotor,
            std::uint8_t rightMotor);

        RE::BSEventNotifyControl ProcessEvent(
            const RE::BGSFootstepEvent* event,
            RE::BSTEventSource<RE::BGSFootstepEvent>* source) override;

    private:
        struct ShadowTruthEvent
        {
            std::uint64_t tsUs{ 0 };
            std::string tag{};
        };

        FootstepTruthProbe() = default;

        void ResetStats();
        void ExpireShadowTruthLocked(std::uint64_t nowUs);
        static std::uint32_t PercentileOf(std::vector<std::uint32_t> values, float p);

        std::atomic<bool> _registered{ false };
        std::atomic<std::uint64_t> _totalEvents{ 0 };
        std::atomic<std::uint64_t> _actorResolvedEvents{ 0 };
        std::atomic<std::uint64_t> _playerEvents{ 0 };
        std::atomic<std::uint64_t> _nonPlayerEvents{ 0 };
        std::atomic<std::uint64_t> _contextAllowedEvents{ 0 };
        std::atomic<std::uint64_t> _contextBlockedEvents{ 0 };
        std::atomic<std::uint64_t> _movingEvents{ 0 };
        std::atomic<std::uint64_t> _recentMoveEvents{ 0 };
        std::atomic<std::uint64_t> _admissibleEvents{ 0 };
        std::atomic<std::uint64_t> _shadowMatchedRenders{ 0 };
        std::atomic<std::uint64_t> _shadowExpiredTruthMisses{ 0 };
        std::atomic<std::uint64_t> _shadowRenderWithoutTruth{ 0 };

        mutable std::mutex _shadowMutex;
        std::deque<ShadowTruthEvent> _pendingTruth{};
        std::vector<std::uint32_t> _shadowRenderDeltasUs{};
    };
}
