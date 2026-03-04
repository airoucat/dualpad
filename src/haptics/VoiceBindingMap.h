#pragma once
#include <atomic>
#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <unordered_map>

namespace dualpad::haptics
{
    struct VoiceBinding
    {
        std::uintptr_t voicePtr{ 0 };
        std::uint32_t generation{ 0 };
        std::uint64_t instanceId{ 0 };
        std::uint64_t tsUs{ 0 };
    };

    class VoiceBindingMap
    {
    public:
        static VoiceBindingMap& GetSingleton();

        std::uint32_t BumpGeneration(std::uintptr_t voicePtr);
        void Bind(std::uintptr_t voicePtr, std::uint32_t generation, std::uint64_t instanceId, std::uint64_t nowUs);
        std::optional<VoiceBinding> TryGet(std::uintptr_t voicePtr) const;
        void Unbind(std::uintptr_t voicePtr);
        void Clear();

        struct Stats {
            std::uint64_t bumps{ 0 };
            std::uint64_t binds{ 0 };
            std::uint64_t hits{ 0 };
            std::uint64_t misses{ 0 };
        };
        Stats GetStats() const;

    private:
        struct Entry {
            std::uint32_t generation{ 0 };
            std::uint64_t instanceId{ 0 };
            std::uint64_t tsUs{ 0 };
        };

        VoiceBindingMap() = default;

        mutable std::shared_mutex _mx;
        std::unordered_map<std::uintptr_t, Entry> _map;

        mutable std::atomic<std::uint64_t> _bumps{ 0 };
        mutable std::atomic<std::uint64_t> _binds{ 0 };
        mutable std::atomic<std::uint64_t> _hits{ 0 };
        mutable std::atomic<std::uint64_t> _misses{ 0 };
    };
}