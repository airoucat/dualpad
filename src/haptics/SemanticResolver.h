#pragma once

#include "haptics/SemanticRuleEngine.h"

#include <atomic>
#include <cstdint>

namespace dualpad::haptics
{
    enum class SemanticRejectReason : std::uint8_t
    {
        None = 0,
        NoFormID,
        CacheMiss,
        LowConfidence
    };

    struct SemanticResolveResult
    {
        bool matched{ false };
        std::uint32_t formID{ 0 };
        FormSemanticMeta meta{};
        SemanticRejectReason rejectReason{ SemanticRejectReason::None };
    };

    class SemanticResolver
    {
    public:
        struct Stats
        {
            std::uint64_t lookups{ 0 };
            std::uint64_t hits{ 0 };
            std::uint64_t noFormID{ 0 };
            std::uint64_t cacheMiss{ 0 };
            std::uint64_t lowConfidence{ 0 };
        };

        static SemanticResolver& GetSingleton();

        SemanticResolveResult Resolve(std::uint32_t formID, float minConfidence);

        Stats GetStats() const;
        void ResetStats();

    private:
        SemanticResolver() = default;

        std::atomic<std::uint64_t> _lookups{ 0 };
        std::atomic<std::uint64_t> _hits{ 0 };
        std::atomic<std::uint64_t> _noFormID{ 0 };
        std::atomic<std::uint64_t> _cacheMiss{ 0 };
        std::atomic<std::uint64_t> _lowConfidence{ 0 };
    };
}
