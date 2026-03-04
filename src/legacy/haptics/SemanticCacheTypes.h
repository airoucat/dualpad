#pragma once

#include "haptics/HapticsTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace dualpad::haptics
{
    enum class SemanticFlags : std::uint16_t
    {
        None = 0,
        IsLoop = 1 << 0,
        IsAmbient = 1 << 1,
        IsVoice = 1 << 2,
        IsUI = 1 << 3,
        IsMusic = 1 << 4
    };

    inline SemanticFlags operator|(SemanticFlags a, SemanticFlags b)
    {
        return static_cast<SemanticFlags>(
            static_cast<std::uint16_t>(a) | static_cast<std::uint16_t>(b));
    }

    inline SemanticFlags operator&(SemanticFlags a, SemanticFlags b)
    {
        return static_cast<SemanticFlags>(
            static_cast<std::uint16_t>(a) & static_cast<std::uint16_t>(b));
    }

    struct SemanticMeta
    {
        SemanticGroup group{ SemanticGroup::Unknown };
        float confidence{ 0.5f };      // 0..1
        float baseWeight{ 0.5f };      // 0..1
        std::uint16_t texturePresetId{ 0 };
        SemanticFlags flags{ SemanticFlags::None };
    };

    struct FormSemanticRecord
    {
        std::uint32_t formId{ 0 };
        SemanticMeta meta{};
    };

    struct SemanticPluginState
    {
        std::string name;              // xxx.esp
        std::uint32_t loadOrder{ 0 };  // FE/xx 简化为 uint32
        std::uint64_t fileSize{ 0 };
        std::uint64_t fileWriteTime{ 0 }; // unix epoch sec
    };

    struct SemanticFingerprint
    {
        std::string gameRuntime;       // "1.5.97"
        std::uint32_t rulesVersion{ 1 };
        std::vector<SemanticPluginState> plugins;

        std::uint64_t hash{ 0 };       // FNV1a64 结果
    };

    struct SemanticCacheFileHeader
    {
        char magic[8]{ 'D','P','H','S','E','M','\0','\0' };
        std::uint32_t schemaVersion{ 1 };
        std::uint32_t recordCount{ 0 };
        std::uint64_t fingerprintHash{ 0 };
        std::uint32_t rulesVersion{ 0 };
        char runtime[16]{};
    };
}