#include "pch.h"
#include "haptics/PlayPathHook.h"

#include "haptics/FootstepTruthBridge.h"
#include "haptics/FormSemanticCache.h"
#include "haptics/HapticsConfig.h"
#include "haptics/InstanceTraceCache.h"
#include "haptics/VoiceBindingMap.h"

#include <Windows.h>

#include <algorithm>
#include <cstring>

namespace dualpad::haptics
{
    namespace
    {
        constexpr std::size_t kInitScanBytes = 0x1C0;
        constexpr std::size_t kSubmitContextScanBytes = 0x80;
        constexpr std::size_t kNestedScanBytes = 0x60;

        constexpr std::uint16_t kFlagInitScanned = 1u << 0;
        constexpr std::uint16_t kFlagSubmitScanned = 1u << 1;
        constexpr std::uint16_t kFlagResolvedFromInit = 1u << 2;
        constexpr std::uint16_t kFlagResolvedFromSubmit = 1u << 3;
        constexpr std::uint16_t kSubmitScanAttemptsMask = 0x0F00u;
        constexpr std::uint8_t kSubmitScanAttemptsShift = 8;

        std::uint8_t GetSubmitScanAttempts(std::uint16_t flags)
        {
            return static_cast<std::uint8_t>((flags & kSubmitScanAttemptsMask) >> kSubmitScanAttemptsShift);
        }

        void SetSubmitScanAttempts(std::uint16_t& flags, std::uint8_t attempts)
        {
            const auto clamped = static_cast<std::uint8_t>(std::min<std::uint8_t>(attempts, 0x0F));
            flags = static_cast<std::uint16_t>(flags & ~kSubmitScanAttemptsMask);
            flags = static_cast<std::uint16_t>(flags | (static_cast<std::uint16_t>(clamped) << kSubmitScanAttemptsShift));
        }

        EventType SemanticToEventType(SemanticGroup group)
        {
            switch (group) {
            case SemanticGroup::WeaponSwing: return EventType::WeaponSwing;
            case SemanticGroup::Hit:         return EventType::HitImpact;
            case SemanticGroup::Block:       return EventType::Block;
            case SemanticGroup::Footstep:    return EventType::Footstep;
            case SemanticGroup::Bow:         return EventType::BowRelease;
            case SemanticGroup::Voice:       return EventType::Shout;
            case SemanticGroup::UI:          return EventType::UI;
            case SemanticGroup::Music:       return EventType::Music;
            case SemanticGroup::Ambient:     return EventType::Ambient;
            default:                         return EventType::Unknown;
            }
        }

        bool IsBackgroundSemantic(SemanticGroup group)
        {
            return group == SemanticGroup::Ambient ||
                group == SemanticGroup::Music ||
                group == SemanticGroup::UI;
        }

        bool IsStrongForegroundSemantic(SemanticGroup group)
        {
            switch (group) {
            case SemanticGroup::WeaponSwing:
            case SemanticGroup::Hit:
            case SemanticGroup::Block:
            case SemanticGroup::Bow:
            case SemanticGroup::Voice:
                return true;
            default:
                return false;
            }
        }

        bool IsTraceSemanticallyReady(const TraceMeta& trace)
        {
            if (trace.preferredEvent != EventType::Unknown) {
                return true;
            }

            if (trace.semantic == SemanticGroup::Unknown) {
                return false;
            }

            // Background classes can be considered semantically complete with a moderate
            // confidence floor, while foreground classes keep a slightly stricter bar.
            if (IsBackgroundSemantic(trace.semantic)) {
                return trace.confidence >= 0.50f;
            }

            return trace.confidence >= 0.52f;
        }

        bool CanStopSemanticScan(const TraceMeta& trace)
        {
            return trace.sourceFormId != 0 &&
                trace.soundFormId != 0 &&
                IsTraceSemanticallyReady(trace);
        }

        bool CanSkipSubmitSemanticScan(const TraceMeta& trace)
        {
            const bool hasAnyForm = (trace.sourceFormId != 0) || (trace.soundFormId != 0);
            return hasAnyForm && IsTraceSemanticallyReady(trace);
        }

        bool IsTraceFootstepForBridge(const TraceMeta& trace)
        {
            if (trace.preferredEvent == EventType::Footstep) {
                return true;
            }
            return trace.semantic == SemanticGroup::Footstep && trace.confidence >= 0.40f;
        }

        void ObserveFootstepTraceBridge(
            const TraceMeta& trace,
            std::uintptr_t voicePtr,
            std::uint32_t generation,
            std::uint64_t nowUs,
            bool viaSubmit)
        {
            const auto& cfg = HapticsConfig::GetSingleton();
            if (!cfg.enableFootstepTruthBridgeShadow) {
                return;
            }
            if (voicePtr == 0 || trace.instanceId == 0 || !IsTraceFootstepForBridge(trace)) {
                return;
            }
            FootstepTruthBridge::GetSingleton().ObserveFootstepInstance(
                trace.instanceId,
                voicePtr,
                generation,
                nowUs,
                viaSubmit);
        }

        std::size_t GetReadableSpan(const void* p, std::size_t maxBytes)
        {
            if (!p || maxBytes == 0) {
                return 0;
            }

            MEMORY_BASIC_INFORMATION mbi{};
            if (::VirtualQuery(p, &mbi, sizeof(mbi)) != sizeof(mbi)) {
                return 0;
            }
            if (mbi.State != MEM_COMMIT) {
                return 0;
            }
            if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) {
                return 0;
            }

            const auto start = reinterpret_cast<std::uintptr_t>(p);
            const auto regionEnd = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
            if (start >= regionEnd) {
                return 0;
            }

            const auto avail = regionEnd - start;
            return static_cast<std::size_t>(
                std::min<std::uint64_t>(static_cast<std::uint64_t>(avail), static_cast<std::uint64_t>(maxBytes)));
        }

        template <class T>
        bool ReadValue(const std::uint8_t* base, std::size_t readableBytes, std::size_t offset, T& out)
        {
            if (!base || offset + sizeof(T) > readableBytes) {
                return false;
            }
            std::memcpy(&out, base + offset, sizeof(T));
            return true;
        }

        bool IsSameTrace(const TraceMeta& a, const TraceMeta& b)
        {
            return a.instanceId == b.instanceId &&
                a.soundFormId == b.soundFormId &&
                a.sourceFormId == b.sourceFormId &&
                a.preferredEvent == b.preferredEvent &&
                a.semantic == b.semantic &&
                a.confidence == b.confidence &&
                a.initObjectPtr == b.initObjectPtr &&
                a.flags == b.flags &&
                a.tsUs == b.tsUs;
        }

        bool TryResolveSemanticFormID(std::uint32_t formID, TraceMeta& io, bool preferSource, bool& changed)
        {
            changed = false;
            if (formID == 0) {
                return false;
            }

            FormSemanticMeta meta{};
            if (!FormSemanticCache::GetSingleton().TryGet(formID, meta)) {
                return false;
            }

            if (preferSource) {
                if (io.sourceFormId == 0) {
                    io.sourceFormId = formID;
                    changed = true;
                }
                else if (io.sourceFormId != formID && io.soundFormId == 0) {
                    io.soundFormId = formID;
                    changed = true;
                }
            }
            else {
                if (io.soundFormId == 0) {
                    io.soundFormId = formID;
                    changed = true;
                }
                else if (io.soundFormId != formID && io.sourceFormId == 0) {
                    io.sourceFormId = formID;
                    changed = true;
                }
            }

            if (io.sourceFormId == 0 && io.soundFormId != 0) {
                io.sourceFormId = io.soundFormId;
                changed = true;
            }

            const auto conf = std::clamp(meta.confidence, 0.0f, 1.0f);
            auto& cfg = HapticsConfig::GetSingleton();
            const bool isBackgroundSemantic = IsBackgroundSemantic(meta.group);
            if (isBackgroundSemantic && !cfg.traceAllowBackgroundEvent) {
                // Keep form IDs for later lookup, but avoid promoting noisy background semantics
                // into runtime event types.
                return true;
            }

            float minEventConfidence = std::clamp(
                isBackgroundSemantic ? cfg.traceBackgroundEventMinConfidence :
                cfg.tracePreferredEventMinConfidence,
                0.0f, 1.0f);
            if (!isBackgroundSemantic && IsStrongForegroundSemantic(meta.group)) {
                // Foreground combat-like semantics can be promoted with a softer threshold
                // to reduce Unknown dominance in runtime.
                minEventConfidence = std::min(
                    minEventConfidence,
                    std::min(0.52f, std::clamp(cfg.unknownSemanticMinConfidence, 0.0f, 1.0f)));
            }

            const auto previousConfidence = io.confidence;
            const bool incomingUnknown = (meta.group == SemanticGroup::Unknown);
            const bool currentUnknown = (io.semantic == SemanticGroup::Unknown);
            const bool currentBackground = IsBackgroundSemantic(io.semantic);
            const bool incomingForeground = !incomingUnknown && !isBackgroundSemantic;

            // Prevent generic Unknown hints (typically Sound/SoundRecord fallback)
            // from downgrading an already-resolved semantic group.
            bool shouldRefreshSemantic = false;
            if (incomingUnknown) {
                shouldRefreshSemantic = currentUnknown;
            } else {
                shouldRefreshSemantic =
                    currentUnknown ||
                    conf > io.confidence ||
                    (incomingForeground && currentBackground);
            }

            if (shouldRefreshSemantic &&
                (io.semantic != meta.group || io.confidence != conf)) {
                io.semantic = meta.group;
                io.confidence = conf;
                changed = true;
            }

            const auto evt = SemanticToEventType(meta.group);
            const bool canPromoteEvent = (evt != EventType::Unknown) && (conf >= minEventConfidence);
            if (canPromoteEvent) {
                const bool shouldSetEvent =
                    (io.preferredEvent == EventType::Unknown) ||
                    (conf > previousConfidence + 0.10f);
                if (shouldSetEvent && io.preferredEvent != evt) {
                    io.preferredEvent = evt;
                    changed = true;
                }
            }

            return true;
        }

        bool TryResolveFromTESFormPointer(std::uintptr_t ptr, std::uint32_t& outFormID)
        {
            outFormID = 0;
            if (ptr == 0) {
                return false;
            }

            const auto* bytes = reinterpret_cast<const std::uint8_t*>(ptr);
            const auto readable = GetReadableSpan(bytes, 0x1C);
            if (readable < 0x18) {
                return false;
            }

            std::uint32_t formID = 0;
            if (!ReadValue(bytes, readable, 0x14, formID) || formID == 0) {
                return false;
            }

            auto* lookedUp = RE::TESForm::LookupByID(static_cast<RE::FormID>(formID));
            if (!lookedUp || reinterpret_cast<std::uintptr_t>(lookedUp) != ptr) {
                return false;
            }

            outFormID = formID;
            return true;
        }

        std::uint32_t ProbeMemoryForSemanticForms(
            std::uintptr_t objectPtr,
            std::size_t maxBytes,
            TraceMeta& io,
            bool preferSource)
        {
            if (objectPtr == 0 || maxBytes == 0) {
                return 0;
            }

            std::uint32_t resolvedCount = 0;
            const auto* base = reinterpret_cast<const std::uint8_t*>(objectPtr);
            const auto readable = GetReadableSpan(base, maxBytes);
            if (readable < sizeof(std::uint32_t)) {
                return 0;
            }

            std::uint32_t directA = 0;
            bool changed = false;
            if (TryResolveFromTESFormPointer(objectPtr, directA) &&
                TryResolveSemanticFormID(directA, io, preferSource, changed)) {
                ++resolvedCount;
            }

            for (std::size_t off = 0; off + sizeof(std::uint32_t) <= readable; off += sizeof(std::uint32_t)) {
                std::uint32_t candidate = 0;
                if (!ReadValue(base, readable, off, candidate)) {
                    continue;
                }

                if (TryResolveSemanticFormID(candidate, io, preferSource, changed)) {
                    ++resolvedCount;
                    if (CanStopSemanticScan(io)) {
                        return resolvedCount;
                    }
                }
            }

            for (std::size_t off = 0; off + sizeof(std::uintptr_t) <= readable; off += sizeof(std::uintptr_t)) {
                std::uintptr_t ptr = 0;
                if (!ReadValue(base, readable, off, ptr)) {
                    continue;
                }

                if (ptr == 0 || ptr == objectPtr) {
                    continue;
                }

                std::uint32_t formID = 0;
                if (TryResolveFromTESFormPointer(ptr, formID)) {
                    if (TryResolveSemanticFormID(formID, io, preferSource, changed)) {
                        ++resolvedCount;
                    }
                }
                else {
                    const auto* nested = reinterpret_cast<const std::uint8_t*>(ptr);
                    const auto nestedReadable = GetReadableSpan(nested, kNestedScanBytes);
                    for (std::size_t noff = 0; noff + sizeof(std::uint32_t) <= nestedReadable; noff += sizeof(std::uint32_t)) {
                        std::uint32_t nestedCandidate = 0;
                        if (!ReadValue(nested, nestedReadable, noff, nestedCandidate)) {
                            continue;
                        }
                        if (TryResolveSemanticFormID(nestedCandidate, io, preferSource, changed)) {
                            ++resolvedCount;
                            break;
                        }
                    }
                }

                if (CanStopSemanticScan(io)) {
                    break;
                }
            }

            return resolvedCount;
        }

        std::uint32_t ProbePointerNeighborhoodForSemanticForms(
            std::uintptr_t rootPtr,
            std::size_t rootBytes,
            TraceMeta& io,
            bool preferSource)
        {
            if (rootPtr == 0 || rootBytes == 0) {
                return 0;
            }

            const auto* root = reinterpret_cast<const std::uint8_t*>(rootPtr);
            const auto rootReadable = GetReadableSpan(root, rootBytes);
            if (rootReadable < sizeof(std::uintptr_t)) {
                return 0;
            }

            std::uint32_t resolvedCount = 0;
            for (std::size_t off = 0; off + sizeof(std::uintptr_t) <= rootReadable; off += sizeof(std::uintptr_t)) {
                std::uintptr_t p1 = 0;
                if (!ReadValue(root, rootReadable, off, p1)) {
                    continue;
                }
                if (p1 == 0 || p1 == rootPtr) {
                    continue;
                }

                resolvedCount += ProbeMemoryForSemanticForms(
                    p1,
                    kSubmitContextScanBytes,
                    io,
                    preferSource);
                if (CanStopSemanticScan(io)) {
                    return resolvedCount;
                }

                const auto* lvl1 = reinterpret_cast<const std::uint8_t*>(p1);
                const auto lvl1Readable = GetReadableSpan(lvl1, kNestedScanBytes);
                for (std::size_t off1 = 0; off1 + sizeof(std::uintptr_t) <= lvl1Readable; off1 += sizeof(std::uintptr_t)) {
                    std::uintptr_t p2 = 0;
                    if (!ReadValue(lvl1, lvl1Readable, off1, p2)) {
                        continue;
                    }
                    if (p2 == 0 || p2 == rootPtr || p2 == p1) {
                        continue;
                    }

                    resolvedCount += ProbeMemoryForSemanticForms(
                        p2,
                        kSubmitContextScanBytes,
                        io,
                        preferSource);
                    if (CanStopSemanticScan(io)) {
                        return resolvedCount;
                    }
                }
            }

            return resolvedCount;
        }

        void UpsertTraceIfChanged(
            const std::optional<TraceMeta>& oldMeta,
            const TraceMeta& newMeta,
            std::atomic<std::uint64_t>& upsertCounter)
        {
            if (oldMeta.has_value() && IsSameTrace(*oldMeta, newMeta)) {
                return;
            }

            InstanceTraceCache::GetSingleton().Upsert(newMeta);
            upsertCounter.fetch_add(1, std::memory_order_relaxed);
        }
    }

    PlayPathHook& PlayPathHook::GetSingleton()
    {
        static PlayPathHook s;
        return s;
    }

    void PlayPathHook::OnInitSoundObject(
        std::uintptr_t initSoundObject,
        std::uintptr_t voicePtr,
        std::uint64_t instanceId,
        std::uint64_t nowUs)
    {
        const auto& cfg = HapticsConfig::GetSingleton();
        // Keep trace hydration available even when L1 form-semantic is temporarily disabled.
        if (!cfg.enableFormSemanticCache) {
            return;
        }

        _counters.initCalls.fetch_add(1, std::memory_order_relaxed);

        if (initSoundObject == 0 || voicePtr == 0 || instanceId == 0) {
            return;
        }

        auto oldMeta = InstanceTraceCache::GetSingleton().TryGet(instanceId);
        TraceMeta next = oldMeta.value_or(TraceMeta{});
        next.instanceId = instanceId;
        next.tsUs = nowUs;
        next.initObjectPtr = initSoundObject;
        next.flags |= kFlagInitScanned;

        const auto resolved = ProbeMemoryForSemanticForms(initSoundObject, kInitScanBytes, next, true);
        if (resolved > 0) {
            next.flags |= kFlagResolvedFromInit;
            _counters.initResolved.fetch_add(1, std::memory_order_relaxed);
        }
        else {
            _counters.initNoForm.fetch_add(1, std::memory_order_relaxed);
        }

        UpsertTraceIfChanged(oldMeta, next, _counters.traceUpserts);

        const auto binding = VoiceBindingMap::GetSingleton().TryGet(voicePtr);
        const auto generation =
            (binding.has_value() && binding->instanceId == instanceId) ? binding->generation : 0u;
        ObserveFootstepTraceBridge(next, voicePtr, generation, nowUs, false);
    }

    void PlayPathHook::OnSubmitContext(
        IXAudio2SourceVoice* voice,
        const XAUDIO2_BUFFER* buffer,
        std::uint64_t nowUs)
    {
        const auto& cfg = HapticsConfig::GetSingleton();
        // Submit-path trace hydration must not depend on L1 semantic toggle.
        if (!cfg.enableFormSemanticCache) {
            return;
        }

        _counters.submitCalls.fetch_add(1, std::memory_order_relaxed);

        const bool hasSubmitContext = (buffer != nullptr && buffer->pContext != nullptr);
        if (!hasSubmitContext) {
            _counters.submitNoContext.fetch_add(1, std::memory_order_relaxed);
        }
        if (!voice) {
            return;
        }

        const auto voicePtr = reinterpret_cast<std::uintptr_t>(voice);
        auto binding = VoiceBindingMap::GetSingleton().TryGet(voicePtr);
        if (!binding.has_value()) {
            _counters.bindingMisses.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        auto oldMeta = InstanceTraceCache::GetSingleton().TryGet(binding->instanceId);
        if (oldMeta.has_value()) {
            _counters.submitTraceMetaHit.fetch_add(1, std::memory_order_relaxed);
        }
        else {
            _counters.submitTraceMetaMiss.fetch_add(1, std::memory_order_relaxed);
        }

        TraceMeta next = oldMeta.value_or(TraceMeta{});
        next.instanceId = binding->instanceId;
        const auto prevTsUs = next.tsUs;
        next.tsUs = nowUs;

        const auto maxAttempts = static_cast<std::uint8_t>(
            std::clamp<std::uint32_t>(cfg.submitSemanticScanMaxAttempts, 1u, 15u));
        const auto retryIntervalUs =
            static_cast<std::uint64_t>(std::max<std::uint32_t>(1u, cfg.submitSemanticRetryIntervalMs)) * 1000ull;

        if (CanSkipSubmitSemanticScan(next)) {
            _counters.submitSkipResolved.fetch_add(1, std::memory_order_relaxed);
            if ((next.flags & kFlagResolvedFromInit) != 0) {
                _counters.submitSkipResolvedFromInit.fetch_add(1, std::memory_order_relaxed);
            }
            else if ((next.flags & kFlagResolvedFromSubmit) != 0) {
                _counters.submitSkipResolvedFromSubmit.fetch_add(1, std::memory_order_relaxed);
            }
            else {
                _counters.submitSkipResolvedOther.fetch_add(1, std::memory_order_relaxed);
            }
            ObserveFootstepTraceBridge(next, binding->voicePtr, binding->generation, nowUs, true);
            return;
        }

        std::uintptr_t scanPtr = 0;
        std::size_t scanBytes = kSubmitContextScanBytes;
        bool preferSource = false;
        bool usedNoContextFallback = false;
        if (hasSubmitContext) {
            scanPtr = reinterpret_cast<std::uintptr_t>(buffer->pContext);
        }
        else {
            if (!cfg.enableSubmitNoContextFallback) {
                return;
            }
            if (next.initObjectPtr == 0) {
                _counters.submitNoContextNoInitPtr.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            scanPtr = next.initObjectPtr;
            scanBytes = kInitScanBytes;
            preferSource = true;
            usedNoContextFallback = true;
            _counters.submitNoContextScan.fetch_add(1, std::memory_order_relaxed);
        }

        const auto attempts = GetSubmitScanAttempts(next.flags);
        if (attempts >= maxAttempts) {
            _counters.submitSkipMaxAttempts.fetch_add(1, std::memory_order_relaxed);
            _counters.submitSkipResolved.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (attempts > 0) {
            const auto deltaUs = (nowUs > prevTsUs) ? (nowUs - prevTsUs) : 0ull;
            if (deltaUs < retryIntervalUs) {
                _counters.submitSkipRateLimit.fetch_add(1, std::memory_order_relaxed);
                _counters.submitSkipResolved.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            _counters.submitRetryScans.fetch_add(1, std::memory_order_relaxed);
        }

        next.flags |= kFlagSubmitScanned;
        SetSubmitScanAttempts(next.flags, static_cast<std::uint8_t>(attempts + 1));
        _counters.submitScanExecuted.fetch_add(1, std::memory_order_relaxed);

        auto resolved = ProbeMemoryForSemanticForms(scanPtr, scanBytes, next, preferSource);
        if (resolved == 0 && usedNoContextFallback && cfg.enableSubmitNoContextDeepFallback) {
            _counters.submitNoContextDeepScan.fetch_add(1, std::memory_order_relaxed);
            resolved = ProbePointerNeighborhoodForSemanticForms(
                scanPtr,
                kInitScanBytes,
                next,
                preferSource);
            if (resolved > 0) {
                _counters.submitNoContextDeepResolved.fetch_add(1, std::memory_order_relaxed);
            }
        }

        if (resolved > 0) {
            next.flags |= kFlagResolvedFromSubmit;
            _counters.submitResolved.fetch_add(1, std::memory_order_relaxed);
            if (usedNoContextFallback) {
                _counters.submitNoContextResolved.fetch_add(1, std::memory_order_relaxed);
            }
            if (attempts > 0) {
                _counters.submitResolvedOnRetry.fetch_add(1, std::memory_order_relaxed);
            }
        }
        else {
            _counters.submitNoForm.fetch_add(1, std::memory_order_relaxed);
            if (attempts == 0) {
                _counters.submitNoFormFirstScan.fetch_add(1, std::memory_order_relaxed);
            }
            else {
                _counters.submitNoFormRetry.fetch_add(1, std::memory_order_relaxed);
            }
        }

        UpsertTraceIfChanged(oldMeta, next, _counters.traceUpserts);
        ObserveFootstepTraceBridge(next, binding->voicePtr, binding->generation, nowUs, true);
    }

    PlayPathHook::Stats PlayPathHook::GetStats() const
    {
        Stats s{};
        #define X(name) s.name = _counters.name.load(std::memory_order_relaxed);
        #include "haptics/PlayPathCounterFields.def"
        #undef X

        return s;
    }

    void PlayPathHook::ResetStats()
    {
        #define X(name) _counters.name.store(0, std::memory_order_relaxed);
        #include "haptics/PlayPathCounterFields.def"
        #undef X
    }
}
