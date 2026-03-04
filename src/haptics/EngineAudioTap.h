#pragma once
#include <cstdint>

namespace dualpad::haptics
{
    class EngineAudioTap
    {
    public:
        struct Stats
        {
            std::uint64_t hookHits{ 0 };

            // Submit path
            std::uint64_t submitCalls{ 0 };
            std::uint64_t submitFeaturesPushed{ 0 };
            std::uint64_t submitCompressedSkipped{ 0 };

            // Legacy fields (kept for compatibility, always 0)
            std::uint64_t attachAttempts{ 0 };
            std::uint64_t attachSuccess{ 0 };
            std::uint64_t attachFailed{ 0 };
        };

        static bool Install();
        static void Uninstall();
        static Stats GetStats();
    };
}