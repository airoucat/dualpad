#pragma once

#include "haptics/SemanticCacheTypes.h"
#include <filesystem>

namespace dualpad::haptics
{
    class SemanticWarmupService
    {
    public:
        static SemanticWarmupService& GetSingleton();

        bool Boot();  // 启动预热入口

    private:
        SemanticWarmupService() = default;

        SemanticFingerprint BuildCurrentFingerprint() const;
        bool TryLoadCacheAndInstall(const SemanticFingerprint& fp) const;
        bool RebuildAndInstall(const SemanticFingerprint& fp) const;

        static std::filesystem::path CacheBinPath();
        static std::filesystem::path CacheMetaPath();
    };
}