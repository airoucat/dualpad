#include "pch.h"
#include "haptics/FootstepCandidateReservoir.h"

#include "haptics/HapticsConfig.h"
#include "haptics/HapticsTypes.h"

#include <algorithm>

namespace dualpad::haptics
{
    namespace
    {
        std::uint64_t AbsDiff(std::uint64_t a, std::uint64_t b)
        {
            return (a > b) ? (a - b) : (b - a);
        }
    }

    FootstepCandidateReservoir& FootstepCandidateReservoir::GetSingleton()
    {
        static FootstepCandidateReservoir instance;
        return instance;
    }

    void FootstepCandidateReservoir::Reset()
    {
        _observed.store(0, std::memory_order_relaxed);
        _observedInit.store(0, std::memory_order_relaxed);
        _observedSubmit.store(0, std::memory_order_relaxed);
        _observedTap.store(0, std::memory_order_relaxed);
        _expired.store(0, std::memory_order_relaxed);
        _snapshotCalls.store(0, std::memory_order_relaxed);
        _returnedCandidates.store(0, std::memory_order_relaxed);
        std::scoped_lock lock(_mutex);
        _candidates.clear();
    }

    void FootstepCandidateReservoir::ObserveCandidate(
        std::uint64_t instanceId,
        std::uintptr_t voicePtr,
        std::uint32_t generation,
        std::uint64_t observedUs,
        Source source,
        float confidence)
    {
        if (instanceId == 0 || voicePtr == 0 || observedUs == 0) {
            return;
        }

        _observed.fetch_add(1, std::memory_order_relaxed);
        switch (source) {
        case Source::Init:
            _observedInit.fetch_add(1, std::memory_order_relaxed);
            break;
        case Source::Submit:
            _observedSubmit.fetch_add(1, std::memory_order_relaxed);
            break;
        case Source::Tap:
            _observedTap.fetch_add(1, std::memory_order_relaxed);
            break;
        default:
            break;
        }

        std::scoped_lock lock(_mutex);
        ExpireLocked(observedUs);

        for (auto& candidate : _candidates) {
            if (candidate.instanceId != instanceId) {
                continue;
            }
            candidate.voicePtr = voicePtr;
            candidate.generation = generation;
            candidate.observedUs = observedUs;
            candidate.source = source;
            candidate.confidence = std::max(candidate.confidence, confidence);
            return;
        }

        _candidates.push_back(Candidate{
            .instanceId = instanceId,
            .voicePtr = voicePtr,
            .generation = generation,
            .observedUs = observedUs,
            .source = source,
            .confidence = confidence
        });
        if (_candidates.size() > 256) {
            std::sort(
                _candidates.begin(),
                _candidates.end(),
                [](const Candidate& a, const Candidate& b) {
                    return a.observedUs > b.observedUs;
                });
            _candidates.resize(256);
        }
    }

    std::vector<FootstepCandidateReservoir::Candidate> FootstepCandidateReservoir::SnapshotForTruth(
        std::uint64_t truthUs,
        std::size_t maxCount)
    {
        _snapshotCalls.fetch_add(1, std::memory_order_relaxed);
        std::vector<Candidate> out{};
        if (truthUs == 0 || maxCount == 0) {
            return out;
        }

        std::scoped_lock lock(_mutex);
        ExpireLocked(truthUs);

        const auto& cfg = HapticsConfig::GetSingleton();
        const auto lookbackUs = static_cast<std::uint64_t>(cfg.footstepTruthBridgeLookbackUs);
        const auto lookaheadUs = static_cast<std::uint64_t>(cfg.footstepTruthBridgeLookaheadUs);

        std::vector<Candidate> ranked{};
        ranked.reserve(_candidates.size());
        for (const auto& candidate : _candidates) {
            const bool inWindow =
                (candidate.observedUs + lookbackUs) >= truthUs &&
                candidate.observedUs <= (truthUs + lookaheadUs);
            if (!inWindow) {
                continue;
            }
            ranked.push_back(candidate);
        }

        std::sort(
            ranked.begin(),
            ranked.end(),
            [&](const Candidate& a, const Candidate& b) {
                const auto ap = SourcePriority(a.source);
                const auto bp = SourcePriority(b.source);
                if (ap != bp) {
                    return ap < bp;
                }
                const auto ad = AbsDiff(a.observedUs, truthUs);
                const auto bd = AbsDiff(b.observedUs, truthUs);
                if (ad != bd) {
                    return ad < bd;
                }
                return a.confidence > b.confidence;
            });

        if (ranked.size() > maxCount) {
            ranked.resize(maxCount);
        }
        _returnedCandidates.fetch_add(ranked.size(), std::memory_order_relaxed);
        return ranked;
    }

    FootstepCandidateReservoir::Stats FootstepCandidateReservoir::GetStats() const
    {
        const auto nowUs = ToQPC(Now());
        {
            std::scoped_lock lock(_mutex);
            const_cast<FootstepCandidateReservoir*>(this)->ExpireLocked(nowUs);
        }

        Stats stats{};
        stats.observed = _observed.load(std::memory_order_relaxed);
        stats.observedInit = _observedInit.load(std::memory_order_relaxed);
        stats.observedSubmit = _observedSubmit.load(std::memory_order_relaxed);
        stats.observedTap = _observedTap.load(std::memory_order_relaxed);
        stats.expired = _expired.load(std::memory_order_relaxed);
        stats.snapshotCalls = _snapshotCalls.load(std::memory_order_relaxed);
        stats.returnedCandidates = _returnedCandidates.load(std::memory_order_relaxed);
        std::scoped_lock lock(_mutex);
        stats.active = static_cast<std::uint32_t>(_candidates.size());
        return stats;
    }

    void FootstepCandidateReservoir::ExpireLocked(std::uint64_t nowUs)
    {
        const auto& cfg = HapticsConfig::GetSingleton();
        const auto ttlUs = std::max<std::uint64_t>(
            180000ull,
            static_cast<std::uint64_t>(cfg.footstepTruthBridgeLookaheadUs) + 120000ull);

        auto it = std::remove_if(
            _candidates.begin(),
            _candidates.end(),
            [&](const Candidate& candidate) {
                if (candidate.observedUs == 0 || nowUs <= candidate.observedUs) {
                    return false;
                }
                return (nowUs - candidate.observedUs) > ttlUs;
            });
        if (it != _candidates.end()) {
            _expired.fetch_add(std::distance(it, _candidates.end()), std::memory_order_relaxed);
            _candidates.erase(it, _candidates.end());
        }
    }

    std::uint32_t FootstepCandidateReservoir::SourcePriority(Source source)
    {
        switch (source) {
        case Source::Submit:
            return 0u;
        case Source::Init:
            return 1u;
        case Source::Tap:
            return 2u;
        default:
            return 3u;
        }
    }
}
