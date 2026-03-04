#include "pch.h"
#include "haptics/SemanticCacheIO.h"
#include "haptics/FileAtomicWriter.h"
#include <SKSE/SKSE.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>

namespace logger = SKSE::log;

namespace dualpad::haptics
{
#pragma pack(push, 1)
    struct DiskRecord
    {
        std::uint32_t formId{ 0 };
        std::uint8_t group{ 0 };
        std::uint16_t confidenceQ{ 0 };     // 0..1000
        std::uint16_t baseWeightQ{ 0 };     // 0..1000
        std::uint16_t texturePresetId{ 0 };
        std::uint16_t flags{ 0 };
    };
#pragma pack(pop)

    namespace
    {
        inline std::uint16_t Quantize01(float v)
        {
            v = std::clamp(v, 0.0f, 1.0f);
            return static_cast<std::uint16_t>(v * 1000.0f + 0.5f);
        }

        inline float Dequantize01(std::uint16_t q)
        {
            return static_cast<float>(q) / 1000.0f;
        }

        inline bool EnsureParentDir(const std::filesystem::path& p)
        {
            auto parent = p.parent_path();
            if (parent.empty()) {
                return true;
            }

            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
            return !ec;
        }

        inline bool ValidMagic(const SemanticCacheFileHeader& h)
        {
            static constexpr std::array<char, 8> kMagic{ 'D','P','H','S','E','M','\0','\0' };
            return std::memcmp(h.magic, kMagic.data(), kMagic.size()) == 0;
        }
    }

    bool SemanticCacheIO::SaveBinary(
        const std::filesystem::path& path,
        const SemanticCacheFileHeader& headerIn,
        const std::vector<FormSemanticRecord>& records)
    {
        if (!EnsureParentDir(path)) {
            logger::error("[Haptics][SemanticCacheIO] create dir failed: {}", path.string());
            return false;
        }

        SemanticCacheFileHeader header = headerIn;
        header.recordCount = static_cast<std::uint32_t>(records.size());

        return FileAtomicWriter::WriteBinary(path,
            [&](std::ostream& os) -> bool {
                auto& ofs = static_cast<std::ofstream&>(os);

                ofs.write(reinterpret_cast<const char*>(&header), sizeof(header));
                if (!ofs.good()) {
                    logger::error("[Haptics][SemanticCacheIO] write header failed");
                    return false;
                }

                for (const auto& r : records) {
                    DiskRecord d{};
                    d.formId = r.formId;
                    d.group = static_cast<std::uint8_t>(r.meta.group);
                    d.confidenceQ = Quantize01(r.meta.confidence);
                    d.baseWeightQ = Quantize01(r.meta.baseWeight);
                    d.texturePresetId = r.meta.texturePresetId;
                    d.flags = static_cast<std::uint16_t>(r.meta.flags);

                    ofs.write(reinterpret_cast<const char*>(&d), sizeof(d));
                    if (!ofs.good()) {
                        logger::error("[Haptics][SemanticCacheIO] write record failed");
                        return false;
                    }
                }
                return true;
            },
            "SemanticCacheBin");
    }

    bool SemanticCacheIO::LoadBinary(
        const std::filesystem::path& path,
        SemanticCacheFileHeader& outHeader,
        std::vector<FormSemanticRecord>& outRecords)
    {
        outRecords.clear();

        std::ifstream ifs(path, std::ios::binary);
        if (!ifs.is_open()) {
            return false;
        }

        SemanticCacheFileHeader header{};
        ifs.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (!ifs.good() || !ValidMagic(header) || header.schemaVersion != 1) {
            logger::warn("[Haptics][SemanticCacheIO] invalid header: {}", path.string());
            return false;
        }

        outRecords.reserve(header.recordCount);

        for (std::uint32_t i = 0; i < header.recordCount; ++i) {
            DiskRecord d{};
            ifs.read(reinterpret_cast<char*>(&d), sizeof(d));
            if (!ifs.good()) {
                logger::warn("[Haptics][SemanticCacheIO] truncated file: {}", path.string());
                return false;
            }

            FormSemanticRecord r{};
            r.formId = d.formId;
            r.meta.group = static_cast<SemanticGroup>(d.group);
            r.meta.confidence = Dequantize01(d.confidenceQ);
            r.meta.baseWeight = Dequantize01(d.baseWeightQ);
            r.meta.texturePresetId = d.texturePresetId;
            r.meta.flags = static_cast<SemanticFlags>(d.flags);

            outRecords.push_back(r);
        }

        outHeader = header;
        return true;
    }

    bool SemanticCacheIO::SaveMetaJson(
        const std::filesystem::path& path,
        const SemanticFingerprint& fp,
        std::size_t recordCount,
        float unknownRatio)
    {
        if (!EnsureParentDir(path)) {
            return false;
        }

        return FileAtomicWriter::WriteText(path,
            [&](std::ostream& os) -> bool {
                auto& ofs = static_cast<std::ofstream&>(os);

                ofs << "{\n";
                ofs << "  \"gameRuntime\": \"" << fp.gameRuntime << "\",\n";
                ofs << "  \"rulesVersion\": " << fp.rulesVersion << ",\n";
                ofs << "  \"fingerprintHash\": " << fp.hash << ",\n";
                ofs << "  \"recordCount\": " << recordCount << ",\n";
                ofs << "  \"unknownRatio\": " << unknownRatio << ",\n";
                ofs << "  \"plugins\": [\n";
                for (std::size_t i = 0; i < fp.plugins.size(); ++i) {
                    const auto& p = fp.plugins[i];
                    ofs << "    {\"name\":\"" << p.name
                        << "\",\"loadOrder\":" << p.loadOrder
                        << ",\"fileSize\":" << p.fileSize
                        << ",\"fileWriteTime\":" << p.fileWriteTime << "}";
                    if (i + 1 < fp.plugins.size()) {
                        ofs << ",";
                    }
                    ofs << "\n";
                }
                ofs << "  ]\n";
                ofs << "}\n";

                return ofs.good();
            },
            "SemanticCacheMeta");
    }
}