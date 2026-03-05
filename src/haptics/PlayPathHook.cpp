#include "pch.h"
#include "haptics/PlayPathHook.h"

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
            if (io.semantic == SemanticGroup::Unknown || conf > io.confidence) {
                if (io.semantic != meta.group || io.confidence != conf) {
                    io.semantic = meta.group;
                    io.confidence = conf;
                    changed = true;
                }

                const auto evt = SemanticToEventType(meta.group);
                if (evt != EventType::Unknown && io.preferredEvent != evt) {
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
                    if (io.sourceFormId != 0 && io.soundFormId != 0) {
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

                if (io.sourceFormId != 0 && io.soundFormId != 0) {
                    break;
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

        if (!voice || !buffer || buffer->pContext == nullptr) {
            _counters.submitNoContext.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        const auto voicePtr = reinterpret_cast<std::uintptr_t>(voice);
        auto binding = VoiceBindingMap::GetSingleton().TryGet(voicePtr);
        if (!binding.has_value()) {
            _counters.bindingMisses.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        auto oldMeta = InstanceTraceCache::GetSingleton().TryGet(binding->instanceId);
        TraceMeta next = oldMeta.value_or(TraceMeta{});
        next.instanceId = binding->instanceId;
        next.tsUs = nowUs;

        if (next.sourceFormId != 0 || next.soundFormId != 0) {
            _counters.submitSkipResolved.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if ((next.flags & kFlagSubmitScanned) != 0) {
            _counters.submitSkipResolved.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        next.flags |= kFlagSubmitScanned;

        const auto resolved = ProbeMemoryForSemanticForms(
            reinterpret_cast<std::uintptr_t>(buffer->pContext),
            kSubmitContextScanBytes,
            next,
            false);

        if (resolved > 0) {
            next.flags |= kFlagResolvedFromSubmit;
            _counters.submitResolved.fetch_add(1, std::memory_order_relaxed);
        }
        else {
            _counters.submitNoForm.fetch_add(1, std::memory_order_relaxed);
        }

        UpsertTraceIfChanged(oldMeta, next, _counters.traceUpserts);
    }

    PlayPathHook::Stats PlayPathHook::GetStats() const
    {
        Stats s{};
        s.initCalls = _counters.initCalls.load(std::memory_order_relaxed);
        s.initResolved = _counters.initResolved.load(std::memory_order_relaxed);
        s.initNoForm = _counters.initNoForm.load(std::memory_order_relaxed);

        s.submitCalls = _counters.submitCalls.load(std::memory_order_relaxed);
        s.submitResolved = _counters.submitResolved.load(std::memory_order_relaxed);
        s.submitNoContext = _counters.submitNoContext.load(std::memory_order_relaxed);
        s.submitNoForm = _counters.submitNoForm.load(std::memory_order_relaxed);
        s.submitSkipResolved = _counters.submitSkipResolved.load(std::memory_order_relaxed);

        s.bindingMisses = _counters.bindingMisses.load(std::memory_order_relaxed);
        s.traceUpserts = _counters.traceUpserts.load(std::memory_order_relaxed);
        return s;
    }

    void PlayPathHook::ResetStats()
    {
        _counters.initCalls.store(0, std::memory_order_relaxed);
        _counters.initResolved.store(0, std::memory_order_relaxed);
        _counters.initNoForm.store(0, std::memory_order_relaxed);

        _counters.submitCalls.store(0, std::memory_order_relaxed);
        _counters.submitResolved.store(0, std::memory_order_relaxed);
        _counters.submitNoContext.store(0, std::memory_order_relaxed);
        _counters.submitNoForm.store(0, std::memory_order_relaxed);
        _counters.submitSkipResolved.store(0, std::memory_order_relaxed);

        _counters.bindingMisses.store(0, std::memory_order_relaxed);
        _counters.traceUpserts.store(0, std::memory_order_relaxed);
    }
}
