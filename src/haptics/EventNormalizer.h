#pragma once

#include "haptics/HapticsTypes.h"
#include "haptics/InstanceTraceCache.h"
#include "haptics/VoiceBindingMap.h"

#include <atomic>
#include <array>
#include <mutex>
#include <optional>
#include <unordered_map>

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
        struct UnknownTopEntry
        {
            std::uint32_t formID{ 0 };
            std::uint32_t hits{ 0 };
            SemanticGroup semantic{ SemanticGroup::Unknown };
            float semanticConfidence{ 0.0f };
        };

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
            std::uint64_t patchedEventTypeConservative{ 0 };
            std::uint64_t unknownAfterNormalize{ 0 };
            std::uint64_t unknownWithFormID{ 0 };
            std::uint64_t unknownNoFormID{ 0 };
            std::uint64_t unknownMapOverflow{ 0 };
            std::uint32_t unknownTopCount{ 0 };
            std::array<UnknownTopEntry, 5> unknownTop{};
        };

        static EventNormalizer& GetSingleton();

        NormalizedSource Normalize(const HapticSourceMsg& in, std::uint64_t nowUs);

        Stats GetStats() const;
        void ResetStats();

    private:
        EventNormalizer() = default;
        void RecordUnknownForm(std::uint32_t formID);

        std::atomic<std::uint64_t> _inputs{ 0 };
        std::atomic<std::uint64_t> _noVoiceID{ 0 };
        std::atomic<std::uint64_t> _bindingHit{ 0 };
        std::atomic<std::uint64_t> _bindingMiss{ 0 };
        std::atomic<std::uint64_t> _traceHit{ 0 };
        std::atomic<std::uint64_t> _traceMiss{ 0 };
        std::atomic<std::uint64_t> _patchedFormID{ 0 };
        std::atomic<std::uint64_t> _patchedEventType{ 0 };
        std::atomic<std::uint64_t> _patchedEventTypeConservative{ 0 };
        std::atomic<std::uint64_t> _unknownAfterNormalize{ 0 };
        std::atomic<std::uint64_t> _unknownWithFormID{ 0 };
        std::atomic<std::uint64_t> _unknownNoFormID{ 0 };
        std::atomic<std::uint64_t> _unknownMapOverflow{ 0 };
        mutable std::mutex _unknownMx;
        std::unordered_map<std::uint32_t, std::uint32_t> _unknownFormHits;
    };
}
