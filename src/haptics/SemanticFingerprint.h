#pragma once

#include "haptics/SemanticCacheTypes.h"

#include <cstdint>
#include <string_view>

namespace dualpad::haptics
{
    class SemanticFingerprintBuilder
    {
    public:
        static std::uint64_t ComputeHash(const SemanticFingerprint& fp);
        static SemanticFingerprint Build(
            std::string_view runtimeVersion,
            std::uint32_t rulesVersion,
            std::vector<SemanticPluginState> plugins);
    };
}