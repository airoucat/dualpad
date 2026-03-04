#include "pch.h"
#include "haptics/VoiceBindingMap.h"

namespace dualpad::haptics
{
    VoiceBindingMap& VoiceBindingMap::GetSingleton()
    {
        static VoiceBindingMap s;
        return s;
    }

    std::uint32_t VoiceBindingMap::BumpGeneration(std::uintptr_t voicePtr)
    {
        std::unique_lock lk(_mx);
        auto& e = _map[voicePtr];
        ++e.generation;
        _bumps.fetch_add(1, std::memory_order_relaxed);
        return e.generation;
    }

    void VoiceBindingMap::Bind(std::uintptr_t voicePtr, std::uint32_t generation, std::uint64_t instanceId, std::uint64_t nowUs)
    {
        std::unique_lock lk(_mx);
        auto& e = _map[voicePtr];
        e.generation = generation;
        e.instanceId = instanceId;
        e.tsUs = nowUs;
        _binds.fetch_add(1, std::memory_order_relaxed);
    }

    std::optional<VoiceBinding> VoiceBindingMap::TryGet(std::uintptr_t voicePtr) const
    {
        std::shared_lock lk(_mx);
        auto it = _map.find(voicePtr);
        if (it == _map.end()) {
            _misses.fetch_add(1, std::memory_order_relaxed);
            return std::nullopt;
        }
        _hits.fetch_add(1, std::memory_order_relaxed);
        return VoiceBinding{ voicePtr, it->second.generation, it->second.instanceId, it->second.tsUs };
    }

    void VoiceBindingMap::Unbind(std::uintptr_t voicePtr)
    {
        std::unique_lock lk(_mx);
        _map.erase(voicePtr);
    }

    void VoiceBindingMap::Clear()
    {
        std::unique_lock lk(_mx);
        _map.clear();
    }

    VoiceBindingMap::Stats VoiceBindingMap::GetStats() const
    {
        return {
            _bumps.load(std::memory_order_relaxed),
            _binds.load(std::memory_order_relaxed),
            _hits.load(std::memory_order_relaxed),
            _misses.load(std::memory_order_relaxed)
        };
    }
}