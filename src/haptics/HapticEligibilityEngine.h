#pragma once

#include "haptics/HapticsTypes.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace dualpad::haptics
{
    enum class GateRejectReason : std::uint8_t
    {
        None = 0,
        UnknownBlocked,
        BackgroundBlocked,
        NoTraceContext,
        LowSemanticConfidence,
        LowRelativeEnergy,
        RefractoryBlocked
    };

    struct GateContext
    {
        bool hasBinding{ false };
        bool hasTrace{ false };
        EventType tracePreferredEvent{ EventType::Unknown };
        float traceConfidence{ 0.0f };
    };

    struct GateResult
    {
        bool accepted{ true };
        GateRejectReason reason{ GateRejectReason::None };
        HapticSourceMsg adjustedSource{};
    };

    class HapticEligibilityEngine
    {
    public:
        struct Stats
        {
            std::uint64_t accepted{ 0 };
            std::uint64_t rejected{ 0 };
            std::uint64_t rejectUnknownBlocked{ 0 };
            std::uint64_t rejectBackgroundBlocked{ 0 };
            std::uint64_t rejectNoTraceContext{ 0 };
            std::uint64_t rejectLowSemanticConfidence{ 0 };
            std::uint64_t rejectLowRelativeEnergy{ 0 };
            std::uint64_t rejectRefractoryBlocked{ 0 };
            std::uint64_t refractoryWindowHit{ 0 };
            std::uint64_t refractorySoftSuppressed{ 0 };
            std::uint64_t refractoryHardDropped{ 0 };
        };

        static HapticEligibilityEngine& GetSingleton();

        GateResult Evaluate(
            const HapticSourceMsg& in,
            const GateContext& ctx,
            std::uint64_t nowUs);

        Stats GetStats() const;
        void ResetStats();

    private:
        HapticEligibilityEngine() = default;

        static bool IsBackgroundEvent(EventType type);
        static EventType SemanticToEventType(SemanticGroup group);
        static bool IsStrongSemanticForUnknown(SemanticGroup group, bool allowFootstep);
        static std::uint8_t ClassifyRefractoryFamily(EventType type);

        GateResult Reject(const HapticSourceMsg& in, GateRejectReason reason);
        bool IsRefractoryBlocked(HapticSourceMsg& src, std::uint64_t nowUs);
        std::uint64_t BuildRefractoryKey(std::uint8_t family, const HapticSourceMsg& src) const;
        void CleanupRefractoryMapLocked(std::uint64_t nowUs);

        std::atomic<std::uint64_t> _accepted{ 0 };
        std::atomic<std::uint64_t> _rejected{ 0 };
        std::atomic<std::uint64_t> _rejectUnknownBlocked{ 0 };
        std::atomic<std::uint64_t> _rejectBackgroundBlocked{ 0 };
        std::atomic<std::uint64_t> _rejectNoTraceContext{ 0 };
        std::atomic<std::uint64_t> _rejectLowSemanticConfidence{ 0 };
        std::atomic<std::uint64_t> _rejectLowRelativeEnergy{ 0 };
        std::atomic<std::uint64_t> _rejectRefractoryBlocked{ 0 };
        std::atomic<std::uint64_t> _refractoryWindowHit{ 0 };
        std::atomic<std::uint64_t> _refractorySoftSuppressed{ 0 };
        std::atomic<std::uint64_t> _refractoryHardDropped{ 0 };

        static constexpr std::uint64_t kRefractoryCleanupIntervalUs = 4'000'000ull;
        static constexpr std::uint64_t kRefractoryStaleUs = 2'000'000ull;

        mutable std::mutex _refractoryMx;
        std::unordered_map<std::uint64_t, std::uint64_t> _refractoryLastAcceptedUs;
        std::uint64_t _lastRefractoryCleanupUs{ 0 };
    };
}
