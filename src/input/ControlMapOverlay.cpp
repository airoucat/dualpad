#include "pch.h"
#include "input/ControlMapOverlay.h"

#include <SKSE/SKSE.h>

#include <array>
#include <string>

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        enum class OverlayStage : std::uint32_t
        {
            Idle = 0,
            ClearLinkedMappings,
            ParseControlMap,
            ResolveLinkedMappings,
            CountMappings,
            Complete
        };

        using ControlMap = RE::ControlMap;

        using ParseControlMap_t = std::int64_t(__fastcall*)(ControlMap*, const char*);
        using ResolveLinkedMappings_t = char(__fastcall*)(ControlMap*);
        using FreeBSTArray_t = std::int64_t(__fastcall*)(void*);

        constexpr auto kParseControlMapOffset = REL::Offset(0x0C13570);
        constexpr auto kResolveLinkedMappingsOffset = REL::Offset(0x0C120B0);
        constexpr auto kFreeBSTArrayOffset = REL::Offset(0x0C04EC0);

        thread_local OverlayStage g_overlayStage = OverlayStage::Idle;

        const char* ToString(OverlayStage stage)
        {
            switch (stage) {
            case OverlayStage::ClearLinkedMappings:
                return "clear-linked-mappings";
            case OverlayStage::ParseControlMap:
                return "parse-controlmap";
            case OverlayStage::ResolveLinkedMappings:
                return "resolve-linked-mappings";
            case OverlayStage::CountMappings:
                return "count-mappings";
            case OverlayStage::Complete:
                return "complete";
            case OverlayStage::Idle:
            default:
                return "idle";
            }
        }

        void ClearLinkedMappings(ControlMap& controlMap)
        {
            auto& linkedMappings = controlMap.GetRuntimeData().linkedMappings;
            if (!linkedMappings.empty()) {
                static REL::Relocation<FreeBSTArray_t> freeArray{ kFreeBSTArrayOffset };
                freeArray(std::addressof(linkedMappings));
            }
        }

        bool ApplyOverlayInternal(const std::filesystem::path& overlayPath)
        {
            auto* controlMap = ControlMap::GetSingleton();
            if (!controlMap) {
                logger::error("[DualPad][ControlMapOverlay] RE::ControlMap singleton unavailable");
                return false;
            }

            const auto overlayPathUtf8 = overlayPath.generic_string();
            logger::info(
                "[DualPad][ControlMapOverlay] Reloading ControlMap from {} via native parser",
                overlayPathUtf8);

            g_overlayStage = OverlayStage::ClearLinkedMappings;
            ClearLinkedMappings(*controlMap);

            g_overlayStage = OverlayStage::ParseControlMap;
            static REL::Relocation<ParseControlMap_t> parseControlMap{ kParseControlMapOffset };
            parseControlMap(controlMap, overlayPathUtf8.c_str());

            g_overlayStage = OverlayStage::ResolveLinkedMappings;
            static REL::Relocation<ResolveLinkedMappings_t> resolveLinkedMappings{ kResolveLinkedMappingsOffset };
            resolveLinkedMappings(controlMap);

            g_overlayStage = OverlayStage::CountMappings;
            std::size_t contextCount = 0;
            std::size_t mappingCount = 0;
            constexpr auto kContextCount = static_cast<std::size_t>(ControlMap::InputContextID::kTotal);
            for (std::size_t contextIndex = 0; contextIndex < kContextCount; ++contextIndex) {
                auto* context = controlMap->controlMap[contextIndex];
                if (!context) {
                    continue;
                }

                ++contextCount;
                for (std::uint32_t device = 0; device < RE::INPUT_DEVICE::kTotal; ++device) {
                    mappingCount += context->deviceMappings[device].size();
                }
            }

            logger::info(
                "[DualPad][ControlMapOverlay] Applied native runtime controlmap reload: contexts={} mappings={} customRemapSkipped=true",
                contextCount,
                mappingCount);
            logger::info(
                "[DualPad][ControlMapOverlay] ControlMap mappings are now owned by {}",
                overlayPathUtf8);

            g_overlayStage = OverlayStage::Complete;
            return true;
        }

#ifdef _MSC_VER
        bool ApplyOverlayWithSeh(const std::filesystem::path& overlayPath)
        {
            g_overlayStage = OverlayStage::Idle;
            bool result = false;
            __try {
                result = ApplyOverlayInternal(overlayPath);
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                logger::error(
                    "[DualPad][ControlMapOverlay] Structured exception 0x{:08X} during stage {}",
                    static_cast<std::uint32_t>(GetExceptionCode()),
                    ToString(g_overlayStage));
                result = false;
            }
            return result;
        }
#endif
    }

    ControlMapOverlay& ControlMapOverlay::GetSingleton()
    {
        static ControlMapOverlay instance;
        return instance;
    }

    std::filesystem::path ControlMapOverlay::GetDefaultPath()
    {
        return "Data/SKSE/Plugins/DualPadControlMap.txt";
    }

    bool ControlMapOverlay::Apply(const std::filesystem::path& path)
    {
        _overlayPath = path.empty() ? GetDefaultPath() : path;

#ifdef _MSC_VER
        return ApplyOverlayWithSeh(_overlayPath);
#else
        return ApplyOverlayInternal(_overlayPath);
#endif
    }

    bool ControlMapOverlay::Reapply()
    {
        return Apply(_overlayPath);
    }
}
