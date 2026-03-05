#pragma once

#include "haptics/HapticsTypes.h"
#include "haptics/InstanceTraceCache.h"
#include "haptics/VoiceBindingMap.h"

#include <atomic>
#include <optional>

namespace dualpad::haptics
{
    enum class NormalizeMissReason : std::uint8_t
    {
        None = 0,
        NoVoiceID,
        VoiceBindingMiss,
        TraceMetaMiss
    };

    struct NormalizedSource
    {
        HapticSourceMsg source{};
        std::optional<VoiceBinding> binding{};
        std::optional<TraceMeta> trace{};
        NormalizeMissReason missReason{ NormalizeMissReason::None };
    };

    class EventNormalizer
    {
    public:
        struct Stats
        {
            std::uint64_t inputs{ 0 };
            std::uint64_t noVoiceID{ 0 };
            std::uint64_t bindingHit{ 0 };
            std::uint64_t bindingMiss{ 0 };
            std::uint64_t traceHit{ 0 };
            std::uint64_t traceMiss{ 0 };
            std::uint64_t patchedFormID{ 0 };
            std::uint64_t patchedEventType{ 0 };
        };

        static EventNormalizer& GetSingleton();

        NormalizedSource Normalize(const HapticSourceMsg& in, std::uint64_t nowUs);

        Stats GetStats() const;
        void ResetStats();

    private:
        EventNormalizer() = default;

        std::atomic<std::uint64_t> _inputs{ 0 };
        std::atomic<std::uint64_t> _noVoiceID{ 0 };
        std::atomic<std::uint64_t> _bindingHit{ 0 };
        std::atomic<std::uint64_t> _bindingMiss{ 0 };
        std::atomic<std::uint64_t> _traceHit{ 0 };
        std::atomic<std::uint64_t> _traceMiss{ 0 };
        std::atomic<std::uint64_t> _patchedFormID{ 0 };
        std::atomic<std::uint64_t> _patchedEventType{ 0 };
    };
}
