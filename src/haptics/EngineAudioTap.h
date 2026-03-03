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

            // Submit Â·¾¶
            std::uint64_t submitCalls{ 0 };
            std::uint64_t submitFeaturesPushed{ 0 };
            std::uint64_t submitCompressedSkipped{ 0 };

            // ¼æÈÝ¾É×Ö¶Î£¨ÒÑ·ÏÆú£¬¹Ì¶¨Îª0£©
            std::uint64_t attachAttempts{ 0 };
            std::uint64_t attachSuccess{ 0 };
            std::uint64_t attachFailed{ 0 };
        };

        static bool Install();
        static void Uninstall();
        static Stats GetStats();
    };
}