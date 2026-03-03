#include "pch.h"
#include "haptics/SemanticFingerprint.h"

#include <algorithm>

namespace dualpad::haptics
{
    namespace
    {
        constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
        constexpr std::uint64_t kFnvPrime = 1099511628211ull;

        inline void FnvMix(std::uint64_t& h, const void* data, std::size_t len)
        {
            const auto* p = reinterpret_cast<const std::uint8_t*>(data);
            for (std::size_t i = 0; i < len; ++i) {
                h ^= p[i];
                h *= kFnvPrime;
            }
        }

        inline void FnvMixU32(std::uint64_t& h, std::uint32_t v) { FnvMix(h, &v, sizeof(v)); }
        inline void FnvMixU64(std::uint64_t& h, std::uint64_t v) { FnvMix(h, &v, sizeof(v)); }

        inline void FnvMixStr(std::uint64_t& h, const std::string& s)
        {
            FnvMixU32(h, static_cast<std::uint32_t>(s.size()));
            if (!s.empty()) {
                FnvMix(h, s.data(), s.size());
            }
        }
    }

    std::uint64_t SemanticFingerprintBuilder::ComputeHash(const SemanticFingerprint& fp)
    {
        std::uint64_t h = kFnvOffset;

        FnvMixStr(h, fp.gameRuntime);
        FnvMixU32(h, fp.rulesVersion);

        std::vector<SemanticPluginState> sorted = fp.plugins;
        std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
            if (a.loadOrder != b.loadOrder) {
                return a.loadOrder < b.loadOrder;
            }
            return a.name < b.name;
            });

        FnvMixU32(h, static_cast<std::uint32_t>(sorted.size()));
        for (const auto& p : sorted) {
            FnvMixStr(h, p.name);
            FnvMixU32(h, p.loadOrder);
            FnvMixU64(h, p.fileSize);
            FnvMixU64(h, p.fileWriteTime);
        }

        return h;
    }

    SemanticFingerprint SemanticFingerprintBuilder::Build(
        std::string_view runtimeVersion,
        std::uint32_t rulesVersion,
        std::vector<SemanticPluginState> plugins)
    {
        SemanticFingerprint fp{};
        fp.gameRuntime = std::string(runtimeVersion);
        fp.rulesVersion = rulesVersion;
        fp.plugins = std::move(plugins);
        fp.hash = ComputeHash(fp);
        return fp;
    }
}