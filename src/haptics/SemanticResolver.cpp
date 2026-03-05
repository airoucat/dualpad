#include "pch.h"
#include "haptics/SemanticResolver.h"

#include "haptics/FormSemanticCache.h"

#include <algorithm>

namespace dualpad::haptics
{
    SemanticResolver& SemanticResolver::GetSingleton()
    {
        static SemanticResolver s;
        return s;
    }

    SemanticResolveResult SemanticResolver::Resolve(std::uint32_t formID, float minConfidence)
    {
        _lookups.fetch_add(1, std::memory_order_relaxed);

        SemanticResolveResult out{};
        out.formID = formID;

        if (formID == 0) {
            out.rejectReason = SemanticRejectReason::NoFormID;
            _noFormID.fetch_add(1, std::memory_order_relaxed);
            return out;
        }

        if (!FormSemanticCache::GetSingleton().TryGet(formID, out.meta)) {
            out.rejectReason = SemanticRejectReason::CacheMiss;
            _cacheMiss.fetch_add(1, std::memory_order_relaxed);
            return out;
        }

        const auto threshold = std::clamp(minConfidence, 0.0f, 1.0f);
        if (out.meta.confidence < threshold) {
            out.rejectReason = SemanticRejectReason::LowConfidence;
            _lowConfidence.fetch_add(1, std::memory_order_relaxed);
            return out;
        }

        out.rejectReason = SemanticRejectReason::None;
        out.matched = true;
        _hits.fetch_add(1, std::memory_order_relaxed);
        return out;
    }

    SemanticResolver::Stats SemanticResolver::GetStats() const
    {
        Stats s{};
        s.lookups = _lookups.load(std::memory_order_relaxed);
        s.hits = _hits.load(std::memory_order_relaxed);
        s.noFormID = _noFormID.load(std::memory_order_relaxed);
        s.cacheMiss = _cacheMiss.load(std::memory_order_relaxed);
        s.lowConfidence = _lowConfidence.load(std::memory_order_relaxed);
        return s;
    }

    void SemanticResolver::ResetStats()
    {
        _lookups.store(0, std::memory_order_relaxed);
        _hits.store(0, std::memory_order_relaxed);
        _noFormID.store(0, std::memory_order_relaxed);
        _cacheMiss.store(0, std::memory_order_relaxed);
        _lowConfidence.store(0, std::memory_order_relaxed);
    }
}
