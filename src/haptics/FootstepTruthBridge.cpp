#include "pch.h"
#include "haptics/FootstepTruthBridge.h"

#include "haptics/FootstepCandidateReservoir.h"
#include "haptics/HapticsConfig.h"
#include "haptics/FootstepTruthSessionShadow.h"
#include "haptics/HapticsTypes.h"

#include <SKSE/SKSE.h>
#include <algorithm>

namespace logger = SKSE::log;

namespace dualpad::haptics
{
    namespace
    {
        constexpr std::uint32_t kMaxProbeLinesPerSecond = 6;
        constexpr std::size_t kDeltaSampleCap = 1024;
        constexpr std::size_t kRecentBindingCap = 256;

        bool ShouldEmitWindowedProbe(
            std::atomic<std::uint64_t>& windowUs,
            std::atomic<std::uint32_t>& windowLines,
            std::uint64_t tsUs,
            std::uint32_t maxLinesPerSec)
        {
            auto win = windowUs.load(std::memory_order_relaxed);
            if (win == 0 || tsUs < win || (tsUs - win) >= 1000000ull) {
                windowUs.store(tsUs, std::memory_order_relaxed);
                windowLines.store(0, std::memory_order_relaxed);
            }
            return windowLines.fetch_add(1, std::memory_order_relaxed) < maxLinesPerSec;
        }

        std::uint64_t AbsDiff(std::uint64_t a, std::uint64_t b)
        {
            return (a > b) ? (a - b) : (b - a);
        }
    }

    FootstepTruthBridge& FootstepTruthBridge::GetSingleton()
    {
        static FootstepTruthBridge instance;
        return instance;
    }

    void FootstepTruthBridge::Reset()
    {
        _truthsObserved.store(0, std::memory_order_relaxed);
        _instancesObserved.store(0, std::memory_order_relaxed);
        _claimsMatched.store(0, std::memory_order_relaxed);
        _claimsFromInit.store(0, std::memory_order_relaxed);
        _claimsFromSubmit.store(0, std::memory_order_relaxed);
        _truthExpiredMisses.store(0, std::memory_order_relaxed);
        _instanceExpiredMisses.store(0, std::memory_order_relaxed);

        std::scoped_lock lock(_mutex);
        _pendingTruths.clear();
        _pendingInstances.clear();
        _bindingsByInstance.clear();
        _recentBindings.clear();
        _claimDeltasUs.clear();
    }

    void FootstepTruthBridge::Tick(std::uint64_t nowUs)
    {
        if (nowUs == 0) {
            nowUs = ToQPC(Now());
        }
        std::scoped_lock lock(_mutex);
        ExpireLocked(nowUs);
    }

    void FootstepTruthBridge::ObserveTruthToken(std::uint64_t truthUs, std::string_view tag)
    {
        static std::atomic<std::uint64_t> s_probeWindowUs{ 0 };
        static std::atomic<std::uint32_t> s_probeLines{ 0 };

        _truthsObserved.fetch_add(1, std::memory_order_relaxed);

        std::scoped_lock lock(_mutex);
        ExpireLocked(truthUs);

        PendingTruth truth{
            .truthUs = truthUs,
            .tag = std::string(tag)
        };

        if (!TryClaimFromTruthLocked(truth)) {
            _pendingTruths.push_back(std::move(truth));
            return;
        }

        if (ShouldEmitWindowedProbe(
                s_probeWindowUs,
                s_probeLines,
                truthUs,
                kMaxProbeLinesPerSecond)) {
            logger::info("[Haptics][FootBridge] truth_claimed tag={} pendingTruth={} pendingInst={}",
                tag,
                _pendingTruths.size(),
                _pendingInstances.size());
        }
    }

    void FootstepTruthBridge::ObserveFootstepInstance(
        std::uint64_t instanceId,
        std::uintptr_t voicePtr,
        std::uint32_t generation,
        std::uint64_t observedUs,
        bool viaSubmit)
    {
        static std::atomic<std::uint64_t> s_probeWindowUs{ 0 };
        static std::atomic<std::uint32_t> s_probeLines{ 0 };

        if (instanceId == 0 || voicePtr == 0 || observedUs == 0) {
            return;
        }

        _instancesObserved.fetch_add(1, std::memory_order_relaxed);
        FootstepTruthSessionShadow::GetSingleton().ObserveFootstepInstance(
            instanceId,
            voicePtr,
            generation,
            observedUs,
            viaSubmit);
        FootstepCandidateReservoir::GetSingleton().ObserveCandidate(
            instanceId,
            voicePtr,
            generation,
            observedUs,
            viaSubmit ? FootstepCandidateReservoir::Source::Submit : FootstepCandidateReservoir::Source::Init,
            viaSubmit ? 1.0f : 0.85f);

        std::scoped_lock lock(_mutex);
        ExpireLocked(observedUs);

        auto bound = _bindingsByInstance.find(instanceId);
        if (bound != _bindingsByInstance.end()) {
            return;
        }

        for (auto& pending : _pendingInstances) {
            if (pending.instanceId == instanceId) {
                pending.voicePtr = voicePtr;
                pending.generation = generation;
                pending.observedUs = observedUs;
                pending.viaSubmit = pending.viaSubmit || viaSubmit;
                return;
            }
        }

        PendingInstance inst{
            .instanceId = instanceId,
            .voicePtr = voicePtr,
            .generation = generation,
            .observedUs = observedUs,
            .viaSubmit = viaSubmit
        };

        if (!TryClaimFromInstanceLocked(inst)) {
            _pendingInstances.push_back(std::move(inst));
            return;
        }

        if (ShouldEmitWindowedProbe(
                s_probeWindowUs,
                s_probeLines,
                observedUs,
                kMaxProbeLinesPerSecond)) {
            logger::info("[Haptics][FootBridge] instance_claimed iid={} via={} pendingTruth={} pendingInst={}",
                instanceId,
                viaSubmit ? "submit" : "init",
                _pendingTruths.size(),
                _pendingInstances.size());
        }
    }

    std::optional<FootstepTruthBridge::Binding> FootstepTruthBridge::TryGetBindingForTruth(
        std::uint64_t truthUs,
        std::string_view tag) const
    {
        std::scoped_lock lock(_mutex);
        for (auto it = _recentBindings.rbegin(); it != _recentBindings.rend(); ++it) {
            if (it->truthUs == truthUs && it->tag == tag) {
                return *it;
            }
        }
        return std::nullopt;
    }

    std::optional<FootstepTruthBridge::Binding> FootstepTruthBridge::TryGetBindingForInstance(
        std::uint64_t instanceId) const
    {
        std::scoped_lock lock(_mutex);
        auto it = _bindingsByInstance.find(instanceId);
        if (it == _bindingsByInstance.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    FootstepTruthBridge::Stats FootstepTruthBridge::GetStats() const
    {
        const auto nowUs = ToQPC(Now());
        {
            std::scoped_lock lock(_mutex);
            const_cast<FootstepTruthBridge*>(this)->ExpireLocked(nowUs);
        }

        Stats stats{};
        stats.truthsObserved = _truthsObserved.load(std::memory_order_relaxed);
        stats.instancesObserved = _instancesObserved.load(std::memory_order_relaxed);
        stats.claimsMatched = _claimsMatched.load(std::memory_order_relaxed);
        stats.claimsFromInit = _claimsFromInit.load(std::memory_order_relaxed);
        stats.claimsFromSubmit = _claimsFromSubmit.load(std::memory_order_relaxed);
        stats.truthExpiredMisses = _truthExpiredMisses.load(std::memory_order_relaxed);
        stats.instanceExpiredMisses = _instanceExpiredMisses.load(std::memory_order_relaxed);
        std::scoped_lock lock(_mutex);
        stats.deltaP50Us = PercentileOf(_claimDeltasUs, 0.50f);
        stats.deltaP95Us = PercentileOf(_claimDeltasUs, 0.95f);
        stats.deltaSamples = static_cast<std::uint32_t>(_claimDeltasUs.size());
        stats.pendingTruths = static_cast<std::uint32_t>(_pendingTruths.size());
        stats.pendingInstances = static_cast<std::uint32_t>(_pendingInstances.size());
        stats.activeBindings = static_cast<std::uint32_t>(_bindingsByInstance.size());
        return stats;
    }

    void FootstepTruthBridge::ExpireLocked(std::uint64_t nowUs)
    {
        static std::atomic<std::uint64_t> s_probeWindowUs{ 0 };
        static std::atomic<std::uint32_t> s_probeLines{ 0 };

        const auto& cfg = HapticsConfig::GetSingleton();
        const auto lookbackUs = static_cast<std::uint64_t>(cfg.footstepTruthBridgeLookbackUs);
        const auto lookaheadUs = static_cast<std::uint64_t>(cfg.footstepTruthBridgeLookaheadUs);
        const auto truthExpireUs = lookaheadUs + 40000ull;
        const auto instanceExpireUs = lookbackUs + 40000ull;
        const auto bindingTtlUs = static_cast<std::uint64_t>(
            std::max<std::uint32_t>(100u, cfg.footstepTruthBridgeBindingTtlMs)) * 1000ull;

        while (!_pendingTruths.empty()) {
            const auto& truth = _pendingTruths.front();
            if (truth.truthUs == 0 || nowUs <= truth.truthUs || (nowUs - truth.truthUs) <= truthExpireUs) {
                break;
            }

            const auto expired = _pendingTruths.front();
            _pendingTruths.pop_front();
            _truthExpiredMisses.fetch_add(1, std::memory_order_relaxed);
            if (ShouldEmitWindowedProbe(
                    s_probeWindowUs,
                    s_probeLines,
                    nowUs,
                    kMaxProbeLinesPerSecond)) {
                logger::info(
                    "[Haptics][FootBridge] truth_expired tag={} age={}us pendingInst={}",
                    expired.tag,
                    nowUs - expired.truthUs,
                    _pendingInstances.size());
            }
        }

        while (!_pendingInstances.empty()) {
            const auto& inst = _pendingInstances.front();
            if (inst.observedUs == 0 || nowUs <= inst.observedUs || (nowUs - inst.observedUs) <= instanceExpireUs) {
                break;
            }

            const auto expired = _pendingInstances.front();
            _pendingInstances.pop_front();
            _instanceExpiredMisses.fetch_add(1, std::memory_order_relaxed);
            if (ShouldEmitWindowedProbe(
                    s_probeWindowUs,
                    s_probeLines,
                    nowUs,
                    kMaxProbeLinesPerSecond)) {
                logger::info(
                    "[Haptics][FootBridge] instance_expired iid={} age={}us via={}",
                    expired.instanceId,
                    nowUs - expired.observedUs,
                    expired.viaSubmit ? "submit" : "init");
            }
        }

        while (!_recentBindings.empty()) {
            const auto& binding = _recentBindings.front();
            if (binding.observedUs == 0 || nowUs <= binding.observedUs || (nowUs - binding.observedUs) <= bindingTtlUs) {
                break;
            }
            _bindingsByInstance.erase(binding.instanceId);
            _recentBindings.pop_front();
        }
    }

    bool FootstepTruthBridge::TryClaimFromTruthLocked(const PendingTruth& truth)
    {
        const auto& cfg = HapticsConfig::GetSingleton();
        const auto lookbackUs = static_cast<std::uint64_t>(cfg.footstepTruthBridgeLookbackUs);
        const auto lookaheadUs = static_cast<std::uint64_t>(cfg.footstepTruthBridgeLookaheadUs);

        auto bestIt = _pendingInstances.end();
        std::uint64_t bestRank = std::numeric_limits<std::uint64_t>::max();
        for (auto it = _pendingInstances.begin(); it != _pendingInstances.end(); ++it) {
            const bool inWindow =
                (it->observedUs + lookbackUs) >= truth.truthUs &&
                it->observedUs <= (truth.truthUs + lookaheadUs);
            if (!inWindow) {
                continue;
            }

            auto rank = AbsDiff(it->observedUs, truth.truthUs);
            if (it->observedUs < truth.truthUs) {
                rank += 15000ull;
            }
            if (rank < bestRank) {
                bestRank = rank;
                bestIt = it;
            }
        }

        if (bestIt == _pendingInstances.end()) {
            return false;
        }

        const auto inst = *bestIt;
        _pendingInstances.erase(bestIt);
        StoreClaimLocked(truth, inst);
        return true;
    }

    bool FootstepTruthBridge::TryClaimFromInstanceLocked(const PendingInstance& inst)
    {
        const auto& cfg = HapticsConfig::GetSingleton();
        const auto lookbackUs = static_cast<std::uint64_t>(cfg.footstepTruthBridgeLookbackUs);
        const auto lookaheadUs = static_cast<std::uint64_t>(cfg.footstepTruthBridgeLookaheadUs);

        auto bestIt = _pendingTruths.end();
        std::uint64_t bestRank = std::numeric_limits<std::uint64_t>::max();
        for (auto it = _pendingTruths.begin(); it != _pendingTruths.end(); ++it) {
            const bool inWindow =
                (inst.observedUs + lookbackUs) >= it->truthUs &&
                inst.observedUs <= (it->truthUs + lookaheadUs);
            if (!inWindow) {
                continue;
            }

            auto rank = AbsDiff(inst.observedUs, it->truthUs);
            if (inst.observedUs < it->truthUs) {
                rank += 15000ull;
            }
            if (rank < bestRank) {
                bestRank = rank;
                bestIt = it;
            }
        }

        if (bestIt == _pendingTruths.end()) {
            return false;
        }

        const auto truth = *bestIt;
        _pendingTruths.erase(bestIt);
        StoreClaimLocked(truth, inst);
        return true;
    }

    void FootstepTruthBridge::StoreClaimLocked(const PendingTruth& truth, const PendingInstance& inst)
    {
        Binding binding{};
        binding.truthUs = truth.truthUs;
        binding.tag = truth.tag;
        binding.instanceId = inst.instanceId;
        binding.voicePtr = inst.voicePtr;
        binding.generation = inst.generation;
        binding.observedUs = inst.observedUs;
        binding.viaSubmit = inst.viaSubmit;
        binding.deltaUs = static_cast<std::uint32_t>(std::min<std::uint64_t>(
            AbsDiff(truth.truthUs, inst.observedUs),
            std::numeric_limits<std::uint32_t>::max()));

        _bindingsByInstance[inst.instanceId] = binding;
        _recentBindings.push_back(binding);
        if (_recentBindings.size() > kRecentBindingCap) {
            _bindingsByInstance.erase(_recentBindings.front().instanceId);
            _recentBindings.pop_front();
        }

        _claimsMatched.fetch_add(1, std::memory_order_relaxed);
        if (inst.viaSubmit) {
            _claimsFromSubmit.fetch_add(1, std::memory_order_relaxed);
        } else {
            _claimsFromInit.fetch_add(1, std::memory_order_relaxed);
        }

        _claimDeltasUs.push_back(binding.deltaUs);
        if (_claimDeltasUs.size() > kDeltaSampleCap) {
            _claimDeltasUs.erase(_claimDeltasUs.begin());
        }
    }

    std::uint32_t FootstepTruthBridge::PercentileOf(std::vector<std::uint32_t> values, float p)
    {
        if (values.empty()) {
            return 0;
        }

        const auto clamped = std::clamp(p, 0.0f, 1.0f);
        const auto idx = static_cast<std::size_t>(clamped * static_cast<float>(values.size() - 1));
        std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(idx), values.end());
        return values[idx];
    }
}
