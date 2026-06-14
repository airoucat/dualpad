#include "pch.h"
#include "input/ControlMapOverlay.h"

#include <SKSE/SKSE.h>
#include <SKSE/Version.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

#include "input/RuntimeConfig.h"

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

        constexpr auto kSupportedRuntime = SKSE::RUNTIME_SSE_1_5_97;
        constexpr auto kParseControlMapOffset = REL::Offset(0x0C13570);
        constexpr auto kResolveLinkedMappingsOffset = REL::Offset(0x0C120B0);
        constexpr auto kFreeBSTArrayOffset = REL::Offset(0x0C04EC0);
        constexpr std::uint64_t kMaxCredibleControlMapMappings = 100'000;
        constexpr std::array<std::uint32_t, 3> kCountedDeviceSlots{ 0, 1, 2 };

        thread_local OverlayStage g_overlayStage = OverlayStage::Idle;

        struct MappingCountSummary
        {
            std::uint32_t contextCount{ 0 };
            std::uint64_t mappingCount{ 0 };
            bool impossible{ false };
            std::size_t contextIndex{ 0 };
            std::uint32_t deviceSlot{ 0 };
        };

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

        bool IsHotkey3To8EventName(std::string_view eventName)
        {
            return eventName == "Hotkey3" ||
                eventName == "Hotkey4" ||
                eventName == "Hotkey5" ||
                eventName == "Hotkey6" ||
                eventName == "Hotkey7" ||
                eventName == "Hotkey8";
        }

        void ClearLinkedMappings(ControlMap& controlMap)
        {
            auto& linkedMappings = controlMap.GetRuntimeData().linkedMappings;
            if (!linkedMappings.empty()) {
                static REL::Relocation<FreeBSTArray_t> freeArray{ kFreeBSTArrayOffset };
                freeArray(std::addressof(linkedMappings));
            }
        }

        std::size_t ApplyRuntimeFeatureGates(ControlMap& controlMap)
        {
            if (RuntimeConfig::GetSingleton().EnableComboNativeHotkeys3To8()) {
                return 0;
            }

            auto* gameplayContext =
                controlMap.controlMap[static_cast<std::size_t>(ControlMap::InputContextID::kGameplay)];
            if (!gameplayContext) {
                return 0;
            }

            auto& gamepadMappings = gameplayContext->deviceMappings[RE::INPUT_DEVICE::kGamepad];
            std::size_t removedCount = 0;
            for (auto it = gamepadMappings.begin(); it != gamepadMappings.end();) {
                const auto eventName = std::string_view(it->eventID.c_str());
                if (IsHotkey3To8EventName(eventName)) {
                    it = gamepadMappings.erase(it);
                    ++removedCount;
                    continue;
                }

                ++it;
            }

            return removedCount;
        }

        MappingCountSummary CountControlMapMappings(ControlMap& controlMap)
        {
            MappingCountSummary summary{};
            constexpr auto kContextCount = static_cast<std::size_t>(ControlMap::InputContextID::kTotal);
            for (std::size_t contextIndex = 0; contextIndex < kContextCount; ++contextIndex) {
                auto* context = controlMap.controlMap[contextIndex];
                if (!context) {
                    continue;
                }

                ++summary.contextCount;
                for (const auto deviceSlot : kCountedDeviceSlots) {
                    const auto deviceMappings = static_cast<std::uint64_t>(
                        context->deviceMappings[deviceSlot].size());
                    if (deviceMappings > kMaxCredibleControlMapMappings ||
                        summary.mappingCount > kMaxCredibleControlMapMappings - deviceMappings) {
                        summary.impossible = true;
                        summary.contextIndex = contextIndex;
                        summary.deviceSlot = deviceSlot;
                        summary.mappingCount = (std::max)(summary.mappingCount, deviceMappings);
                        return summary;
                    }
                    summary.mappingCount += deviceMappings;
                }
            }
            return summary;
        }

        bool ApplyOverlayInternal(const std::filesystem::path& overlayPath, std::size_t& filteredHotkeyMappings)
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

            filteredHotkeyMappings = ApplyRuntimeFeatureGates(*controlMap);
            if (filteredHotkeyMappings != 0) {
                logger::info(
                    "[DualPad][ControlMapOverlay] Runtime gate removed {} gameplay Hotkey3-8 mappings",
                    filteredHotkeyMappings);
            }

            g_overlayStage = OverlayStage::CountMappings;
            const auto mappingSummary = CountControlMapMappings(*controlMap);
            if (mappingSummary.impossible ||
                mappingSummary.mappingCount > kMaxCredibleControlMapMappings) {
                logger::error(
                    "[DualPad][ControlMapOverlay] impossible mapping count contexts={} mappings={} max={} contextIndex={} deviceSlot={} customRemapSkipped=true failClosed=true",
                    mappingSummary.contextCount,
                    mappingSummary.mappingCount,
                    kMaxCredibleControlMapMappings,
                    mappingSummary.contextIndex,
                    mappingSummary.deviceSlot);
                return false;
            }

            logger::info(
                "[DualPad][ControlMapOverlay] Applied native runtime controlmap reload: contexts={} mappings={} customRemapSkipped=true",
                mappingSummary.contextCount,
                mappingSummary.mappingCount);
            logger::info(
                "[DualPad][ControlMapOverlay] ControlMap mappings are now owned by {}",
                overlayPathUtf8);

            g_overlayStage = OverlayStage::Complete;
            return true;
        }

#ifdef _MSC_VER
        bool ApplyOverlayWithSeh(const std::filesystem::path& overlayPath, std::size_t& filteredHotkeyMappings)
        {
            g_overlayStage = OverlayStage::Idle;
            bool result = false;
            __try {
                result = ApplyOverlayInternal(overlayPath, filteredHotkeyMappings);
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
        _applySucceeded = false;

        if (REL::Module::get().version() != kSupportedRuntime) {
            if (!_loggedUnsupportedRuntime) {
                logger::warn(
                    "[DualPad][ControlMapOverlay] Skipping runtime overlay on unsupported runtime {}; only Skyrim SE 1.5.97 is supported",
                    REL::Module::get().version().string());
                _loggedUnsupportedRuntime = true;
            }
            return false;
        }

        if (!std::filesystem::exists(_overlayPath)) {
            if (!_loggedMissingOverlay) {
                logger::warn(
                    "[DualPad][ControlMapOverlay] Overlay file missing at {}; combo-native gamepad ABI will remain unavailable",
                    _overlayPath.string());
                _loggedMissingOverlay = true;
            }
            return false;
        }

        std::size_t filteredHotkeyMappings = 0;

#ifdef _MSC_VER
        _applySucceeded = ApplyOverlayWithSeh(_overlayPath, filteredHotkeyMappings);
#else
        _applySucceeded = ApplyOverlayInternal(_overlayPath, filteredHotkeyMappings);
#endif
        return _applySucceeded;
    }

    bool ControlMapOverlay::Reapply()
    {
        return Apply(_overlayPath);
    }
}
