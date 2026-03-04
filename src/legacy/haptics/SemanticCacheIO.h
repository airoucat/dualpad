#pragma once

#include "haptics/SemanticCacheTypes.h"

#include <filesystem>
#include <string>
#include <vector>

namespace dualpad::haptics
{
    class SemanticCacheIO
    {
    public:
        static bool SaveBinary(
            const std::filesystem::path& path,
            const SemanticCacheFileHeader& header,
            const std::vector<FormSemanticRecord>& records);

        static bool LoadBinary(
            const std::filesystem::path& path,
            SemanticCacheFileHeader& outHeader,
            std::vector<FormSemanticRecord>& outRecords);

        static bool SaveMetaJson(
            const std::filesystem::path& path,
            const SemanticFingerprint& fp,
            std::size_t recordCount,
            float unknownRatio);
    };
}