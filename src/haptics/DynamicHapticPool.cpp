#include "pch.h"
#include "haptics/DynamicHapticPool.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace dualpad::haptics
{
    namespace
    {
        constexpr std::uint64_t kFormIdTag = 1ull << 63;
    }

    DynamicHapticPool& DynamicHapticPool::GetSingleton()
    {
        static DynamicHapticPool s;
        return s;
    }

    float DynamicHapticPool::Clamp01(float v)
    {
        return std::clamp(v, 0.0f, 1.0f);
    }

    std::uint64_t DynamicHapticPool::MakeKey(const HapticSourceMsg& source)
    {
        if (source.sourceFormId != 0) {
            return kFormIdTag | static_cast<std::uint64_t>(source.sourceFormId);
        }
        if (source.eventType != EventType::Unknown) {
            return static_cast<std::uint64_t>(source.eventType);
        }
        return 0;
    }

    void DynamicHapticPool::Configure(
        bool enabled,
        std::uint32_t topK,
        float minConfidence,
        float outputCap,
        std::uint32_t resolveMinHits,
        float resolveMinInputEnergy)
    {
        std::scoped_lock lock(_mutex);
        _enabled = enabled;
        _topK = std::max<std::uint32_t>(1, topK);
        _minConfidence = Clamp01(minConfidence);
        _outputCap = Clamp01(outputCap);
        _resolveMinHits = std::max<std::uint32_t>(1, resolveMinHits);
        _resolveMinInputEnergy = Clamp01(resolveMinInputEnergy);
    }

    void DynamicHapticPool::EvictOneLocked()
    {
        if (_entries.empty()) {
            return;
        }

        auto worstIt = _entries.end();
        float worstRank = std::numeric_limits<float>::max();
        for (auto it = _entries.begin(); it != _entries.end(); ++it) {
            // Prefer high quality templates that have been hit repeatedly.
            const float hitBonus = std::log1p(static_cast<float>(it->second.hits)) * 0.03f;
            const float rank = it->second.score + hitBonus;
            if (rank < worstRank) {
                worstRank = rank;
                worstIt = it;
            }
        }

        if (worstIt != _entries.end()) {
            _entries.erase(worstIt);
            _evicted.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void DynamicHapticPool::ObserveL1(const HapticSourceMsg& source, float matchScore)
    {
        _observeCalls.fetch_add(1, std::memory_order_relaxed);

        std::scoped_lock lock(_mutex);
        if (!_enabled) {
            return;
        }

        const auto key = MakeKey(source);
        if (key == 0) {
            _rejectedNoKey.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        const auto confidence = Clamp01(std::max(matchScore, source.confidence));
        if (confidence < _minConfidence || std::max(source.left, source.right) < 0.05f) {
            _rejectedLowConfidence.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        auto it = _entries.find(key);
        if (it == _entries.end()) {
            if (_entries.size() >= _topK) {
                EvictOneLocked();
            }

            Entry e{};
            e.sourceFormId = source.sourceFormId;
            e.eventType = source.eventType;
            e.left = Clamp01(source.left);
            e.right = Clamp01(source.right);
            e.confidence = confidence;
            e.priority = source.priority;
            e.ttlMs = source.ttlMs;
            e.score = confidence;
            e.hits = 1;
            e.lastSeenUs = ToQPC(Now());
            _entries.emplace(key, e);
        }
        else {
            auto& e = it->second;
            constexpr float kAlpha = 0.25f;
            e.sourceFormId = (source.sourceFormId != 0) ? source.sourceFormId : e.sourceFormId;
            e.eventType = (source.eventType != EventType::Unknown) ? source.eventType : e.eventType;
            e.left = Clamp01((1.0f - kAlpha) * e.left + kAlpha * source.left);
            e.right = Clamp01((1.0f - kAlpha) * e.right + kAlpha * source.right);
            e.confidence = Clamp01((1.0f - kAlpha) * e.confidence + kAlpha * confidence);
            e.priority = std::max(e.priority, source.priority);
            e.ttlMs = std::clamp(source.ttlMs, 24u, 180u);
            e.score = Clamp01((1.0f - kAlpha) * e.score + kAlpha * confidence);
            e.hits += 1;
            e.lastSeenUs = ToQPC(Now());
        }

        _admitted.fetch_add(1, std::memory_order_relaxed);
    }

    bool DynamicHapticPool::ShadowCanResolve(const HapticSourceMsg& input)
    {
        _shadowCalls.fetch_add(1, std::memory_order_relaxed);

        std::scoped_lock lock(_mutex);
        if (!_enabled) {
            _shadowMisses.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        const auto key = MakeKey(input);
        if (key == 0) {
            _shadowMisses.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        if (_entries.find(key) != _entries.end()) {
            _shadowHits.fetch_add(1, std::memory_order_relaxed);
            return true;
        }

        _shadowMisses.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    bool DynamicHapticPool::TryResolve(const HapticSourceMsg& input, HapticSourceMsg& output)
    {
        _resolveCalls.fetch_add(1, std::memory_order_relaxed);

        std::scoped_lock lock(_mutex);
        if (!_enabled) {
            _resolveMisses.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        const auto key = MakeKey(input);
        if (key == 0) {
            _resolveMisses.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        auto it = _entries.find(key);
        if (it == _entries.end()) {
            _resolveMisses.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        auto& e = it->second;
        if (e.hits < _resolveMinHits) {
            _resolveRejectMinHits.fetch_add(1, std::memory_order_relaxed);
            _resolveMisses.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        if (std::max(input.left, input.right) < _resolveMinInputEnergy) {
            _resolveRejectLowInput.fetch_add(1, std::memory_order_relaxed);
            _resolveMisses.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        e.hits += 1;
        e.lastSeenUs = ToQPC(Now());

        output = input;
        if (output.sourceFormId == 0 && e.sourceFormId != 0) {
            output.sourceFormId = e.sourceFormId;
        }
        if (output.eventType == EventType::Unknown && e.eventType != EventType::Unknown) {
            output.eventType = e.eventType;
        }

        output.left = std::clamp(e.left, 0.0f, _outputCap);
        output.right = std::clamp(e.right, 0.0f, _outputCap);
        output.confidence = std::max(output.confidence, std::clamp(e.confidence, 0.0f, _outputCap));
        output.priority = std::max(output.priority, e.priority);
        output.ttlMs = std::clamp(e.ttlMs, 24u, 180u);

        _resolveHits.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    DynamicHapticPool::Stats DynamicHapticPool::GetStats() const
    {
        std::scoped_lock lock(_mutex);
        Stats s{};
        s.observeCalls = _observeCalls.load(std::memory_order_relaxed);
        s.admitted = _admitted.load(std::memory_order_relaxed);
        s.rejectedNoKey = _rejectedNoKey.load(std::memory_order_relaxed);
        s.rejectedLowConfidence = _rejectedLowConfidence.load(std::memory_order_relaxed);
        s.shadowCalls = _shadowCalls.load(std::memory_order_relaxed);
        s.shadowHits = _shadowHits.load(std::memory_order_relaxed);
        s.shadowMisses = _shadowMisses.load(std::memory_order_relaxed);
        s.resolveCalls = _resolveCalls.load(std::memory_order_relaxed);
        s.resolveHits = _resolveHits.load(std::memory_order_relaxed);
        s.resolveMisses = _resolveMisses.load(std::memory_order_relaxed);
        s.resolveRejectMinHits = _resolveRejectMinHits.load(std::memory_order_relaxed);
        s.resolveRejectLowInput = _resolveRejectLowInput.load(std::memory_order_relaxed);
        s.evicted = _evicted.load(std::memory_order_relaxed);
        s.currentSize = static_cast<std::uint64_t>(_entries.size());
        return s;
    }

    void DynamicHapticPool::ResetStats()
    {
        _observeCalls.store(0, std::memory_order_relaxed);
        _admitted.store(0, std::memory_order_relaxed);
        _rejectedNoKey.store(0, std::memory_order_relaxed);
        _rejectedLowConfidence.store(0, std::memory_order_relaxed);
        _shadowCalls.store(0, std::memory_order_relaxed);
        _shadowHits.store(0, std::memory_order_relaxed);
        _shadowMisses.store(0, std::memory_order_relaxed);
        _resolveCalls.store(0, std::memory_order_relaxed);
        _resolveHits.store(0, std::memory_order_relaxed);
        _resolveMisses.store(0, std::memory_order_relaxed);
        _resolveRejectMinHits.store(0, std::memory_order_relaxed);
        _resolveRejectLowInput.store(0, std::memory_order_relaxed);
        _evicted.store(0, std::memory_order_relaxed);
    }

    void DynamicHapticPool::Clear()
    {
        std::scoped_lock lock(_mutex);
        _entries.clear();
    }
}
