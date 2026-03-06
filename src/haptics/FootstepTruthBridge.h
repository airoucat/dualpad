#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace dualpad::haptics
{
    class FootstepTruthBridge
    {
    public:
        struct Binding
        {
            std::uint64_t truthUs{ 0 };
            std::string tag{};
            std::uint64_t instanceId{ 0 };
            std::uintptr_t voicePtr{ 0 };
            std::uint32_t generation{ 0 };
            std::uint64_t observedUs{ 0 };
            bool viaSubmit{ false };
            std::uint32_t deltaUs{ 0 };
        };

        struct Stats
        {
            std::uint64_t truthsObserved{ 0 };
            std::uint64_t instancesObserved{ 0 };
            std::uint64_t claimsMatched{ 0 };
            std::uint64_t claimsFromInit{ 0 };
            std::uint64_t claimsFromSubmit{ 0 };
            std::uint64_t truthExpiredMisses{ 0 };
            std::uint64_t instanceExpiredMisses{ 0 };
            std::uint32_t deltaP50Us{ 0 };
            std::uint32_t deltaP95Us{ 0 };
            std::uint32_t deltaSamples{ 0 };
            std::uint32_t pendingTruths{ 0 };
            std::uint32_t pendingInstances{ 0 };
            std::uint32_t activeBindings{ 0 };
        };

        static FootstepTruthBridge& GetSingleton();

        void Reset();
        void Tick(std::uint64_t nowUs);
        void ObserveTruthToken(std::uint64_t truthUs, std::string_view tag);
        void ObserveFootstepInstance(
            std::uint64_t instanceId,
            std::uintptr_t voicePtr,
            std::uint32_t generation,
            std::uint64_t observedUs,
            bool viaSubmit);

        std::optional<Binding> TryGetBindingForTruth(std::uint64_t truthUs, std::string_view tag) const;
        std::optional<Binding> TryGetBindingForInstance(std::uint64_t instanceId) const;
        Stats GetStats() const;

    private:
        struct PendingTruth
        {
            std::uint64_t truthUs{ 0 };
            std::string tag{};
        };

        struct PendingInstance
        {
            std::uint64_t instanceId{ 0 };
            std::uintptr_t voicePtr{ 0 };
            std::uint32_t generation{ 0 };
            std::uint64_t observedUs{ 0 };
            bool viaSubmit{ false };
        };

        FootstepTruthBridge() = default;

        void ExpireLocked(std::uint64_t nowUs);
        bool TryClaimFromTruthLocked(const PendingTruth& truth);
        bool TryClaimFromInstanceLocked(const PendingInstance& inst);
        void StoreClaimLocked(const PendingTruth& truth, const PendingInstance& inst);
        static std::uint32_t PercentileOf(std::vector<std::uint32_t> values, float p);

        mutable std::mutex _mutex;
        std::deque<PendingTruth> _pendingTruths{};
        std::deque<PendingInstance> _pendingInstances{};
        std::unordered_map<std::uint64_t, Binding> _bindingsByInstance{};
        std::deque<Binding> _recentBindings{};
        std::vector<std::uint32_t> _claimDeltasUs{};

        std::atomic<std::uint64_t> _truthsObserved{ 0 };
        std::atomic<std::uint64_t> _instancesObserved{ 0 };
        std::atomic<std::uint64_t> _claimsMatched{ 0 };
        std::atomic<std::uint64_t> _claimsFromInit{ 0 };
        std::atomic<std::uint64_t> _claimsFromSubmit{ 0 };
        std::atomic<std::uint64_t> _truthExpiredMisses{ 0 };
        std::atomic<std::uint64_t> _instanceExpiredMisses{ 0 };
    };
}
