#include "pch.h"
#include "input/backend/KeyboardNativeBackend.h"
#include "input/backend/KeyboardNativeBridge.h"

#include <SKSE/Version.h>

#include "input/Action.h"
#include "input/InputContext.h"
#include "input/RuntimeConfig.h"
#include "input/injection/UpstreamGamepadHook.h"
#include "input/injection/PadEventSnapshotDispatcher.h"

#include <intrin.h>
#include <atomic>
#include <cctype>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace logger = SKSE::log;

namespace dualpad::input::backend
{
    namespace
    {
        using ContextID = RE::ControlMap::InputContextID;
        using KeyboardControlEvent_t = void(__fastcall*)(
            RE::BSWin32KeyboardDevice*,
            std::uint32_t,
            float,
            bool,
            bool);
        using KeyboardGetDeviceData_t = std::uint32_t(__fastcall*)(
            void*,
            REX::W32::IDirectInputDevice8A*,
            std::uint32_t*,
            REX::W32::DIDEVICEOBJECTDATA*);

        constexpr bool UsesContinuousState(ActionOutputContract contract)
        {
            return contract == ActionOutputContract::Hold;
        }

        constexpr bool UsesDebouncedPulse(ActionOutputContract contract)
        {
            return contract == ActionOutputContract::Pulse;
        }

        constexpr bool UsesScheduledPulseContract(ActionOutputContract contract)
        {
            return contract == ActionOutputContract::Toggle ||
                contract == ActionOutputContract::Repeat;
        }

        constexpr std::uint8_t GetScheduledPulseDownConsumes(ActionOutputContract contract)
        {
            switch (contract) {
            case ActionOutputContract::Toggle:
                return 2;
            case ActionOutputContract::Repeat:
                return 1;
            case ActionOutputContract::Pulse:
            case ActionOutputContract::Hold:
            case ActionOutputContract::Axis:
            case ActionOutputContract::None:
            default:
                return 1;
            }
        }

        constexpr std::uint8_t GetScheduledPulseGapConsumes(ActionOutputContract contract)
        {
            switch (contract) {
            case ActionOutputContract::Repeat:
                return 1;
            case ActionOutputContract::Toggle:
            case ActionOutputContract::Pulse:
            case ActionOutputContract::Hold:
            case ActionOutputContract::Axis:
            case ActionOutputContract::None:
            default:
                return 0;
            }
        }

        constexpr float kRepeatInitialDelaySeconds = 0.35f;
        constexpr float kRepeatIntervalSeconds = 0.08f;
        constexpr float kRepeatScheduleEpsilon = 0.0001f;

        constexpr auto kSupportedRuntime = SKSE::RUNTIME_SSE_1_5_97;
        constexpr std::uintptr_t kKeyboardPollRva = 0xC1A130;
        constexpr std::uintptr_t kKeyboardGetDeviceDataWrapperRva = 0xC166D0;
        constexpr std::uintptr_t kGlobalInputManagerPointerRva = 0x2F50B28;
        constexpr std::uintptr_t kPreprocessGlobalObjectPointerRva = 0x2EC5BD0;
        constexpr std::uintptr_t kDispatchOwnerGlobalObjectPointerRva = 0x2F257A8;
        constexpr std::uintptr_t kValidateCompareGlobalPointerRva = 0x2F25250;
        constexpr std::uintptr_t kClickFamilyStateByteRva = 0x2F4E650;
        constexpr std::ptrdiff_t kValidateCompareFieldOffset = 696;
        constexpr std::size_t kShadowVtableEntryCount = 32;
        constexpr std::ptrdiff_t kKeyboardPollPostEventOffset = 0x250;
        constexpr std::ptrdiff_t kKeyboardPollPostEventWindowOffset = 0x24B;
        constexpr std::size_t kDiObjDataCallSearchLength = 0x100;
        constexpr std::uintptr_t kKeyboardControlEventRva = 0xC190F0;
        constexpr std::ptrdiff_t kDeviceHashtableOffset = 0x38;
        constexpr std::ptrdiff_t kManagerRecordBufferOffset = 0x20;
        constexpr std::size_t kManagerRecordStride = 0x30;
        constexpr std::ptrdiff_t kManagerEventCountOffset = 0x04;
        constexpr std::ptrdiff_t kManagerQueueHeadOffset = 0x896;
        constexpr std::ptrdiff_t kManagerQueueTailOffset = 0x904;
        constexpr std::ptrdiff_t kQueueEventNextOffset = 0x10;
        constexpr std::ptrdiff_t kQueueEventDeviceIdOffset = 0x08;
        constexpr std::ptrdiff_t kQueueEventTypeOffset = 0x0C;
        constexpr std::ptrdiff_t kQueueEventTimeStampOffset = 0x18;
        constexpr std::ptrdiff_t kQueueEventScanCodeOffset = 0x20;
        constexpr std::ptrdiff_t kQueueEventParam4Offset = 0x28;
        constexpr std::ptrdiff_t kQueueEventHoldTimeOffset = 0x2C;
        constexpr std::array<std::uint8_t, 32> kExpectedPollPostEventWindow = {
            0x4C, 0x8B, 0x64, 0x24, 0x58, 0x4C, 0x8B, 0x6C,
            0x24, 0x50, 0x48, 0x8B, 0xAC, 0x24, 0x88, 0x00,
            0x00, 0x00, 0x0F, 0x1F, 0x00, 0x80, 0xBE, 0x00,
            0xFF, 0xFF, 0xFF, 0x00, 0x74, 0x1A, 0x80, 0x3E
        };
        static thread_local bool t_replayingDeferredActions = false;
        void LogJumpInjectionDecision(
            std::uint8_t scancode,
            const RE::BSWin32KeyboardDevice& device,
            const REX::W32::DIDEVICEOBJECTDATA* eventBuffer,
            std::uint32_t originalCount,
            std::uint32_t totalCount,
            std::uint8_t desiredCount,
            std::uint8_t pendingPulseCount,
            bool syntheticPrevDown,
            bool predictedCurrent,
            bool synthDesired,
            bool synthCurrent,
            bool injected,
            std::int32_t injectedSlot,
            bool nativeCandidate,
            bool pairedPulse,
            bool finalPredictedCurrent,
            bool textEntryActive);
        bool EventBufferContainsScancode(
            const REX::W32::DIDEVICEOBJECTDATA* eventBuffer,
            std::uint32_t count,
            std::uint8_t scancode);
        void LogDiObjDataBufferSummary(
            std::string_view phase,
            const REX::W32::DIDEVICEOBJECTDATA* eventBuffer,
            std::uint32_t count,
            std::uint8_t focusScancode);
        void LogDiObjDataBufferFull(
            std::string_view phase,
            const REX::W32::DIDEVICEOBJECTDATA* eventBuffer,
            std::uint32_t count,
            std::uint32_t maxEvents);
        void LogRuntimeGlobalObjectDebug(std::string_view phase);
        std::string DescribeStringPointer(std::uintptr_t address);
        bool TryReadQword(std::uintptr_t address, std::uint64_t& value);
        bool TryReadDword(std::uintptr_t address, std::uint32_t& value);
        bool TryReadByte(std::uintptr_t address, std::uint8_t& value);

        std::optional<std::uintptr_t> FindDirectCallSiteToTarget(
            std::uintptr_t start,
            std::size_t length,
            std::uintptr_t target,
            std::string_view label);
        void LogDirectCallTargetsInRange(
            std::string_view label,
            std::uintptr_t start,
            std::size_t length);

        std::uintptr_t GetInputManagerAddress()
        {
            const auto slot = reinterpret_cast<const std::uintptr_t*>(REL::Module::get().base() + kGlobalInputManagerPointerRva);
            return slot ? *slot : 0;
        }

        bool IsReadableProtection(const DWORD protection)
        {
            if ((protection & PAGE_GUARD) != 0 || (protection & PAGE_NOACCESS) != 0) {
                return false;
            }

            switch (protection & 0xFF) {
            case PAGE_READONLY:
            case PAGE_READWRITE:
            case PAGE_WRITECOPY:
            case PAGE_EXECUTE_READ:
            case PAGE_EXECUTE_READWRITE:
            case PAGE_EXECUTE_WRITECOPY:
                return true;
            default:
                return false;
            }
        }

        bool IsExecutableProtection(const DWORD protection)
        {
            if ((protection & PAGE_GUARD) != 0 || (protection & PAGE_NOACCESS) != 0) {
                return false;
            }

            switch (protection & 0xFF) {
            case PAGE_EXECUTE:
            case PAGE_EXECUTE_READ:
            case PAGE_EXECUTE_READWRITE:
            case PAGE_EXECUTE_WRITECOPY:
                return true;
            default:
                return false;
            }
        }

        bool TryReadQword(std::uintptr_t address, std::uint64_t& value)
        {
            MEMORY_BASIC_INFORMATION info{};
            if (::VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) == 0) {
                return false;
            }
            if (info.State != MEM_COMMIT || !IsReadableProtection(info.Protect)) {
                return false;
            }

            const auto regionEnd = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
            if (address + sizeof(std::uint64_t) > regionEnd) {
                return false;
            }

            value = *reinterpret_cast<const std::uint64_t*>(address);
            return true;
        }

        bool TryReadDword(std::uintptr_t address, std::uint32_t& value)
        {
            MEMORY_BASIC_INFORMATION info{};
            if (::VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) == 0) {
                return false;
            }
            if (info.State != MEM_COMMIT || !IsReadableProtection(info.Protect)) {
                return false;
            }

            const auto regionEnd = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
            if (address + sizeof(std::uint32_t) > regionEnd) {
                return false;
            }

            value = *reinterpret_cast<const std::uint32_t*>(address);
            return true;
        }

        bool TryReadByte(std::uintptr_t address, std::uint8_t& value)
        {
            MEMORY_BASIC_INFORMATION info{};
            if (::VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) == 0) {
                return false;
            }
            if (info.State != MEM_COMMIT || !IsReadableProtection(info.Protect)) {
                return false;
            }

            const auto regionEnd = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
            if (address + sizeof(std::uint8_t) > regionEnd) {
                return false;
            }

            value = *reinterpret_cast<const std::uint8_t*>(address);
            return true;
        }

        bool TryReadMemory(std::uintptr_t address, std::byte* buffer, std::size_t size)
        {
            if (buffer == nullptr || size == 0) {
                return false;
            }

            MEMORY_BASIC_INFORMATION info{};
            if (::VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) == 0) {
                return false;
            }
            if (info.State != MEM_COMMIT || !IsReadableProtection(info.Protect)) {
                return false;
            }

            const auto regionBase = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
            const auto regionEnd = regionBase + info.RegionSize;
            if (address < regionBase || size > regionEnd - address) {
                return false;
            }

            std::memcpy(buffer, reinterpret_cast<const void*>(address), size);
            return true;
        }

        std::string DescribeAddressModule(std::uintptr_t address)
        {
            if (address == 0) {
                return "null";
            }

            MEMORY_BASIC_INFORMATION info{};
            if (::VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) == 0) {
                return "unmapped";
            }

            const auto allocationBase = reinterpret_cast<std::uintptr_t>(info.AllocationBase);
            if (info.Type == MEM_IMAGE && info.AllocationBase != nullptr) {
                std::array<char, MAX_PATH> path{};
                const auto copied = ::GetModuleFileNameA(
                    static_cast<HMODULE>(info.AllocationBase),
                    path.data(),
                    static_cast<DWORD>(path.size()));
                if (copied != 0) {
                    std::string_view view(path.data(), copied);
                    const auto separator = view.find_last_of("\\/");
                    const auto fileName =
                        separator == std::string_view::npos ? view : view.substr(separator + 1);
                    return std::format("{}@0x{:X}", fileName, allocationBase);
                }
            }

            return std::format("type=0x{:X}@0x{:X}", info.Type, allocationBase);
        }

        std::string DescribeClickFamilyStateByte()
        {
            std::uint8_t value = 0;
            const auto address = REL::Module::get().base() + kClickFamilyStateByteRva;
            if (!TryReadByte(address, value)) {
                return std::format("unreadable(addr=0x{:X})", address);
            }

            return std::format("{}(addr=0x{:X})", static_cast<std::uint32_t>(value), address);
        }

        bool TryCallEventSourceAccessor(std::uintptr_t functionAddress, std::uintptr_t eventNode, std::uintptr_t& result)
        {
            MEMORY_BASIC_INFORMATION info{};
            if (::VirtualQuery(reinterpret_cast<LPCVOID>(functionAddress), &info, sizeof(info)) == 0) {
                return false;
            }
            if (info.State != MEM_COMMIT || !IsExecutableProtection(info.Protect)) {
                return false;
            }

            using EventSourceAccessor_t = std::uintptr_t(__fastcall*)(std::uintptr_t);
            const auto function = reinterpret_cast<EventSourceAccessor_t>(functionAddress);

            __try {
                result = function(eventNode);
                return true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        std::uintptr_t ReadGlobalObjectPointer(std::uintptr_t slotRva)
        {
            const auto slotAddress = REL::Module::get().base() + slotRva;
            std::uint64_t value = 0;
            return TryReadQword(slotAddress, value) ? static_cast<std::uintptr_t>(value) : 0;
        }

        bool TryReadExpectedEventSource(std::uintptr_t& compareGlobalAddress, std::uintptr_t& expectedValue)
        {
            compareGlobalAddress = ReadGlobalObjectPointer(kValidateCompareGlobalPointerRva);
            if (compareGlobalAddress == 0) {
                return false;
            }

            std::uint64_t rawExpectedValue = 0;
            if (!TryReadQword(compareGlobalAddress + kValidateCompareFieldOffset, rawExpectedValue)) {
                return false;
            }

            expectedValue = static_cast<std::uintptr_t>(rawExpectedValue);
            return expectedValue != 0;
        }

        std::optional<std::uintptr_t> FindDirectCallSiteToTarget(
            std::uintptr_t start,
            std::size_t length,
            std::uintptr_t target,
            std::string_view label)
        {
            std::optional<std::uintptr_t> match;
            std::uint32_t hits = 0;

            for (std::size_t offset = 0; offset + 5 <= length; ++offset) {
                const auto candidate = start + offset;
                const auto* bytes = reinterpret_cast<const std::uint8_t*>(candidate);
                if (bytes[0] != 0xE8) {
                    continue;
                }

                const auto displacement = *reinterpret_cast<const std::int32_t*>(candidate + 1);
                const auto resolvedTarget = candidate + 5 + displacement;
                if (resolvedTarget != target) {
                    continue;
                }

                if (!match.has_value()) {
                    match = candidate;
                }

                ++hits;
                logger::info(
                    "[DualPad][KeyboardNative] {} call candidate callSite=0x{:X} target=0x{:X}",
                    label,
                    candidate,
                    resolvedTarget);
            }

            if (hits > 1) {
                logger::warn(
                    "[DualPad][KeyboardNative] {} found {} matching direct calls; using first candidate 0x{:X}",
                    label,
                    hits,
                    match.value_or(0));
            }

            return match;
        }

        std::optional<std::uintptr_t> FindNthDirectCallSiteToTarget(
            std::uintptr_t start,
            std::size_t length,
            std::uintptr_t target,
            std::uint32_t ordinal,
            std::string_view label)
        {
            std::uint32_t hitIndex = 0;
            std::optional<std::uintptr_t> match;

            for (std::size_t offset = 0; offset + 5 <= length; ++offset) {
                const auto candidate = start + offset;
                const auto* bytes = reinterpret_cast<const std::uint8_t*>(candidate);
                if (bytes[0] != 0xE8) {
                    continue;
                }

                const auto displacement = *reinterpret_cast<const std::int32_t*>(candidate + 1);
                const auto resolvedTarget = candidate + 5 + displacement;
                if (resolvedTarget != target) {
                    continue;
                }

                ++hitIndex;
                if (hitIndex == ordinal) {
                    match = candidate;
                    logger::info(
                        "[DualPad][KeyboardNative] {} ordinal {} selected callSite=0x{:X} target=0x{:X}",
                        label,
                        ordinal,
                        candidate,
                        resolvedTarget);
                    break;
                }
            }

            if (!match.has_value()) {
                logger::warn(
                    "[DualPad][KeyboardNative] {} ordinal {} not found for target=0x{:X}",
                    label,
                    ordinal,
                    target);
            }

            return match;
        }

        void LogDirectCallTargetsInRange(
            std::string_view label,
            std::uintptr_t start,
            std::size_t length)
        {
            logger::info(
                "[DualPad][KeyboardNative] {} probing direct calls start=0x{:X} length=0x{:X}",
                label,
                start,
                length);

            std::uint32_t hits = 0;
            for (std::size_t offset = 0; offset + 5 <= length; ++offset) {
                const auto candidate = start + offset;
                const auto* bytes = reinterpret_cast<const std::uint8_t*>(candidate);
                if (bytes[0] != 0xE8) {
                    continue;
                }

                const auto displacement = *reinterpret_cast<const std::int32_t*>(candidate + 1);
                const auto resolvedTarget = candidate + 5 + displacement;
                logger::info(
                    "[DualPad][KeyboardNative] {} callSite=0x{:X} target=0x{:X}",
                    label,
                    candidate,
                    resolvedTarget);
                ++hits;
            }

            if (hits == 0) {
                logger::info("[DualPad][KeyboardNative] {} no direct calls found", label);
            }
        }

        void LogObjectFieldSnapshot(std::string_view label, std::uintptr_t object)
        {
            if (object == 0) {
                logger::info("[DualPad][KeyboardNative] {} object=null", label);
                return;
            }

            std::uint64_t q00 = 0;
            std::uint64_t q08 = 0;
            std::uint32_t d10 = 0;
            std::uint64_t q18 = 0;
            std::uint64_t q20 = 0;
            std::uint32_t d28 = 0;

            const auto hasQ00 = TryReadQword(object + 0x00, q00);
            const auto hasQ08 = TryReadQword(object + 0x08, q08);
            const auto hasD10 = TryReadDword(object + 0x10, d10);
            const auto hasQ18 = TryReadQword(object + 0x18, q18);
            const auto hasQ20 = TryReadQword(object + 0x20, q20);
            const auto hasD28 = TryReadDword(object + 0x28, d28);

            logger::info(
                "[DualPad][KeyboardNative] {} object=0x{:X} +00=0x{:X}{} +08=0x{:X}{} +10={}{} +18=0x{:X}{} +20=0x{:X}{} +28={}{}",
                label,
                object,
                q00,
                hasQ00 ? "" : " (unreadable)",
                q08,
                hasQ08 ? "" : " (unreadable)",
                d10,
                hasD10 ? "" : " (unreadable)",
                q18,
                hasQ18 ? "" : " (unreadable)",
                q20,
                hasQ20 ? "" : " (unreadable)",
                d28,
                hasD28 ? "" : " (unreadable)");
        }

        void LogQueueSnapshotForObject(std::string_view label, std::uintptr_t object)
        {
            if (object == 0) {
                logger::info("[DualPad][KeyboardNative] {} object=null", label);
                return;
            }

            std::uint32_t eventCount = 0;
            std::uint64_t queueHead = 0;
            std::uint64_t queueTail = 0;
            const auto hasEventCount = TryReadDword(object + kManagerEventCountOffset, eventCount);
            const auto hasQueueHead = TryReadQword(object + kManagerQueueHeadOffset, queueHead);
            const auto hasQueueTail = TryReadQword(object + kManagerQueueTailOffset, queueTail);

            logger::info(
                "[DualPad][KeyboardNative] {} object=0x{:X} eventCount={}{} queueHead=0x{:X}{} queueTail=0x{:X}{}",
                label,
                object,
                eventCount,
                hasEventCount ? "" : " (unreadable)",
                queueHead,
                hasQueueHead ? "" : " (unreadable)",
                queueTail,
                hasQueueTail ? "" : " (unreadable)");

            auto node = static_cast<std::uintptr_t>(queueHead);
            for (std::uint32_t i = 0; node != 0 && i < 10; ++i) {
                std::uint32_t code = 0;
                std::uint64_t field18 = 0;
                std::uint64_t next = 0;
                const auto hasCode = TryReadDword(node + 0x20, code);
                const auto hasField18 = TryReadQword(node + 0x18, field18);
                const auto hasNext = TryReadQword(node + 0x10, next);
                logger::info(
                    "[DualPad][KeyboardNative]   {}[{}] node=0x{:X} code=0x{:X}{} control=0x{:X}{}({}) next=0x{:X}{}",
                    label,
                    i,
                    node,
                    code,
                    hasCode ? "" : " (unreadable)",
                    field18,
                    hasField18 ? "" : " (unreadable)",
                    hasField18 ? DescribeStringPointer(static_cast<std::uintptr_t>(field18)) : "unknown",
                    next,
                    hasNext ? "" : " (unreadable)");
                node = static_cast<std::uintptr_t>(next);
            }
        }

        void LogRuntimeGlobalObjectDebug(std::string_view phase)
        {
            logger::info("[DualPad][KeyboardNative] === GLOBAL OBJECTS DEBUG ({}) ===", phase);

            const auto imageBase = REL::Module::get().base();
            const auto slot25250Address = imageBase + kValidateCompareGlobalPointerRva;
            const auto slot50B28Address = imageBase + kGlobalInputManagerPointerRva;
            const auto object25250 = ReadGlobalObjectPointer(kValidateCompareGlobalPointerRva);
            const auto object50B28 = ReadGlobalObjectPointer(kGlobalInputManagerPointerRva);

            logger::info(
                "[DualPad][KeyboardNative] Address of qword_142F25250: 0x{:X}",
                slot25250Address);
            logger::info(
                "[DualPad][KeyboardNative] Address of qword_142F50B28: 0x{:X}",
                slot50B28Address);
            logger::info(
                "[DualPad][KeyboardNative] qword_142F25250 points to: 0x{:X}",
                object25250);
            logger::info(
                "[DualPad][KeyboardNative] qword_142F50B28 points to: 0x{:X}",
                object50B28);
            logger::info(
                "[DualPad][KeyboardNative] sameObject={}",
                object25250 == object50B28 ? "true" : "false");

            LogObjectFieldSnapshot("Global25250", object25250);
            if (object50B28 != object25250) {
                LogObjectFieldSnapshot("Global50B28", object50B28);
            }

            LogQueueSnapshotForObject("Queue25250", object25250);
            LogQueueSnapshotForObject("Queue50B28", object50B28);
        }

        void LogGlobalObjectComparisonIfNeeded(
            RE::ControlMap* preprocessArg,
            void* dispatchOwnerArg)
        {
            static std::once_flag globalsOnce;
            static std::mutex stateMutex;
            static std::uintptr_t preprocessArgSeen = 0;
            static std::uintptr_t dispatchOwnerArgSeen = 0;
            static bool comparisonLogged = false;

            const auto preprocessArgAddress = reinterpret_cast<std::uintptr_t>(preprocessArg);
            const auto dispatchOwnerAddress = reinterpret_cast<std::uintptr_t>(dispatchOwnerArg);

            std::call_once(globalsOnce, [&]() {
                const auto preprocessGlobal = ReadGlobalObjectPointer(kPreprocessGlobalObjectPointerRva);
                const auto dispatchGlobal = ReadGlobalObjectPointer(kDispatchOwnerGlobalObjectPointerRva);

                logger::info("[DualPad][KeyboardNative] === Global Object Slots ===");
                logger::info(
                    "[DualPad][KeyboardNative] qword_142EC5BD0 -> 0x{:X} qword_142F257A8 -> 0x{:X} sameObject={}",
                    preprocessGlobal,
                    dispatchGlobal,
                    preprocessGlobal == dispatchGlobal ? "true" : "false");
                LogObjectFieldSnapshot("PreprocessGlobal", preprocessGlobal);
                if (dispatchGlobal != preprocessGlobal) {
                    LogObjectFieldSnapshot("DispatchGlobal", dispatchGlobal);
                }
            });

            {
                const std::lock_guard lock(stateMutex);
                if (preprocessArgAddress != 0) {
                    preprocessArgSeen = preprocessArgAddress;
                }
                if (dispatchOwnerAddress != 0) {
                    dispatchOwnerArgSeen = dispatchOwnerAddress;
                }
                if (!comparisonLogged && preprocessArgSeen != 0 && dispatchOwnerArgSeen != 0) {
                    comparisonLogged = true;

                    const auto preprocessGlobal = ReadGlobalObjectPointer(kPreprocessGlobalObjectPointerRva);
                    const auto dispatchGlobal = ReadGlobalObjectPointer(kDispatchOwnerGlobalObjectPointerRva);

                    logger::info("[DualPad][KeyboardNative] === Global Object Runtime Comparison ===");
                    logger::info(
                        "[DualPad][KeyboardNative] runtime args preprocess=0x{:X} dispatchOwner=0x{:X} preprocessArgMatches={} dispatchOwnerMatches={} runtimeArgsSame={}",
                        preprocessArgSeen,
                        dispatchOwnerArgSeen,
                        preprocessArgSeen == preprocessGlobal ? "true" : "false",
                        dispatchOwnerArgSeen == dispatchGlobal ? "true" : "false",
                        preprocessArgSeen == dispatchOwnerArgSeen ? "true" : "false");

                    if (preprocessArgSeen != preprocessGlobal && preprocessArgSeen != dispatchGlobal) {
                        LogObjectFieldSnapshot("PreprocessArg", preprocessArgSeen);
                    }
                    if (dispatchOwnerArgSeen != preprocessGlobal && dispatchOwnerArgSeen != dispatchGlobal) {
                        LogObjectFieldSnapshot("DispatchOwnerArg", dispatchOwnerArgSeen);
                    }
                }
            }
        }

        std::string DescribeStringPointer(std::uintptr_t address)
        {
            if (address == 0) {
                return "null";
            }

            MEMORY_BASIC_INFORMATION info{};
            if (::VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) == 0) {
                return "unmapped";
            }
            if (info.State != MEM_COMMIT || !IsReadableProtection(info.Protect)) {
                return "unreadable";
            }

            const auto regionEnd = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
            const auto maxChars = (std::min<std::size_t>)(48, regionEnd > address ? regionEnd - address : 0);
            if (maxChars == 0) {
                return "empty-region";
            }

            const auto* text = reinterpret_cast<const unsigned char*>(address);
            std::string preview;
            preview.reserve(maxChars);
            for (std::size_t i = 0; i < maxChars; ++i) {
                const auto ch = text[i];
                if (ch == '\0') {
                    break;
                }
                if (std::isprint(ch) == 0) {
                    return preview.empty() ? "non-printable" : preview + "...";
                }
                preview.push_back(static_cast<char>(ch));
            }

            if (preview.empty()) {
                return "empty";
            }

            return preview;
        }

        std::atomic_uint32_t g_syntheticKeyboardEventTimeStamp{ 0x100000 };
        std::atomic_uint32_t g_syntheticKeyboardEventSequence{ 0x200000 };
        void LogJumpInjectionDecision(
            const std::uint8_t scancode,
            const RE::BSWin32KeyboardDevice& device,
            const REX::W32::DIDEVICEOBJECTDATA* eventBuffer,
            const std::uint32_t originalCount,
            const std::uint32_t totalCount,
            const std::uint8_t desiredCount,
            const std::uint8_t pendingPulseCount,
            const std::uint8_t pendingTransactionalPulseCount,
            const bool syntheticPrevDown,
            const bool predictedCurrent,
            const bool synthDesired,
            const bool synthCurrent,
            const bool injected,
            const std::int32_t injectedSlot,
            const bool nativeCandidate,
            const bool pairedPulse,
            const bool finalPredictedCurrent,
            const bool textEntryActive)
        {
            if (!RuntimeConfig::GetSingleton().LogKeyboardInjection() || eventBuffer == nullptr) {
                return;
            }

            const auto prevDown = (device.prevState[scancode] & 0x80u) != 0;
            const auto curDown = (device.curState[scancode] & 0x80u) != 0;
            std::size_t matchingOriginalEvents = 0;
            std::size_t matchingTotalEvents = 0;

            for (std::uint32_t i = 0; i < totalCount; ++i) {
                const auto eventScancode = static_cast<std::uint8_t>(eventBuffer[i].ofs & 0xFFu);
                if (eventScancode != scancode) {
                    continue;
                }

                if (i < originalCount) {
                    ++matchingOriginalEvents;
                }
                ++matchingTotalEvents;
            }

            logger::info(
                "[DualPad][KeyboardNative] JumpInjectionDecision scancode=0x{:02X} prevDown={} curDown={} desiredCount={} pulseCount={} transactionalPulseCount={} synthPrev={} predictedCurrent={} synthDesired={} synthCurrent={} nativeCandidate={} injected={} injectedSlot={} pairedPulse={} finalPredictedCurrent={} originalCount={} totalCount={} matchingOriginalEvents={} matchingTotalEvents={} textEntry={}",
                static_cast<std::uint32_t>(scancode),
                prevDown,
                curDown,
                desiredCount,
                pendingPulseCount,
                pendingTransactionalPulseCount,
                syntheticPrevDown,
                predictedCurrent,
                synthDesired,
                synthCurrent,
                nativeCandidate,
                injected,
                injectedSlot,
                pairedPulse,
                finalPredictedCurrent,
                originalCount,
                totalCount,
                matchingOriginalEvents,
                matchingTotalEvents,
                textEntryActive);

            std::size_t logged = 0;
            for (std::uint32_t i = 0; i < totalCount && logged < 4; ++i) {
                const auto eventScancode = static_cast<std::uint8_t>(eventBuffer[i].ofs & 0xFFu);
                if (eventScancode != scancode) {
                    continue;
                }

                logger::info(
                    "[DualPad][KeyboardNative]   JumpInjectionEvent[{}] source={} data=0x{:02X} timeStamp=0x{:X} sequence=0x{:X}",
                    i,
                    i < originalCount ? "native" : "synthetic",
                    eventBuffer[i].data,
                    eventBuffer[i].timeStamp,
                    eventBuffer[i].sequence);
                ++logged;
            }
        }

        bool EventBufferContainsScancode(
            const REX::W32::DIDEVICEOBJECTDATA* eventBuffer,
            const std::uint32_t count,
            const std::uint8_t scancode)
        {
            if (eventBuffer == nullptr) {
                return false;
            }

            for (std::uint32_t i = 0; i < count; ++i) {
                if (static_cast<std::uint8_t>(eventBuffer[i].ofs & 0xFFu) == scancode) {
                    return true;
                }
            }

            return false;
        }

        void LogDiObjDataBufferSummary(
            std::string_view phase,
            const REX::W32::DIDEVICEOBJECTDATA* eventBuffer,
            const std::uint32_t count,
            const std::uint8_t focusScancode)
        {
            if (!RuntimeConfig::GetSingleton().LogKeyboardInjection()) {
                return;
            }

            logger::info(
                "[DualPad][KeyboardNative] {} count={} focusScancode=0x{:02X}",
                phase,
                count,
                static_cast<std::uint32_t>(focusScancode));

            if (eventBuffer == nullptr || count == 0) {
                logger::info(
                    "[DualPad][KeyboardNative]   {} buffer empty",
                    phase);
                return;
            }

            std::size_t logged = 0;
            for (std::uint32_t i = 0; i < count && logged < 6; ++i) {
                const auto eventScancode = static_cast<std::uint8_t>(eventBuffer[i].ofs & 0xFFu);
                if (eventScancode != focusScancode) {
                    continue;
                }

                logger::info(
                    "[DualPad][KeyboardNative]   {} event[{}] ofs=0x{:02X} data=0x{:02X} ts=0x{:X} seq=0x{:X} app=0x{:X}",
                    phase,
                    i,
                    static_cast<std::uint32_t>(eventScancode),
                    eventBuffer[i].data & 0xFFu,
                    eventBuffer[i].timeStamp,
                    eventBuffer[i].sequence,
                    eventBuffer[i].appData);
                ++logged;
            }

            if (logged == 0) {
                logger::info(
                    "[DualPad][KeyboardNative]   {} had no focus-scancode events",
                    phase);
            }
        }

        void LogDiObjDataBufferFull(
            std::string_view phase,
            const REX::W32::DIDEVICEOBJECTDATA* eventBuffer,
            const std::uint32_t count,
            const std::uint32_t maxEvents)
        {
            if (!RuntimeConfig::GetSingleton().LogKeyboardInjection()) {
                return;
            }

            logger::info(
                "[DualPad][KeyboardNative] {} count={}",
                phase,
                count);

            if (eventBuffer == nullptr || count == 0) {
                logger::info(
                    "[DualPad][KeyboardNative]   {} buffer empty",
                    phase);
                return;
            }

            const auto cappedCount = (std::min)(count, maxEvents);
            for (std::uint32_t i = 0; i < cappedCount; ++i) {
                logger::info(
                    "[DualPad][KeyboardNative]   {} event[{}] ofs=0x{:02X} data=0x{:02X} ts=0x{:X} seq=0x{:X} app=0x{:X}",
                    phase,
                    i,
                    static_cast<std::uint32_t>(eventBuffer[i].ofs & 0xFFu),
                    eventBuffer[i].data & 0xFFu,
                    eventBuffer[i].timeStamp,
                    eventBuffer[i].sequence,
                    eventBuffer[i].appData);
            }
        }

        std::span<const ContextID> ResolveContextSearchOrder(InputContext context)
        {
            static constexpr std::array kGameplay = { ContextID::kGameplay };
            static constexpr std::array kMenu = { ContextID::kMenuMode };
            static constexpr std::array kInventory = { ContextID::kInventory, ContextID::kItemMenu, ContextID::kMenuMode };
            static constexpr std::array kFavorites = { ContextID::kFavorites, ContextID::kMenuMode };
            static constexpr std::array kMap = { ContextID::kMap, ContextID::kMenuMode };
            static constexpr std::array kJournal = { ContextID::kJournal, ContextID::kMenuMode };
            static constexpr std::array kStats = { ContextID::kStats, ContextID::kMenuMode };
            static constexpr std::array kBook = { ContextID::kBook, ContextID::kMenuMode };
            static constexpr std::array kConsole = { ContextID::kConsole };
            static constexpr std::array kLockpicking = { ContextID::kLockpicking, ContextID::kMenuMode };

            switch (context) {
            case InputContext::Gameplay:
            case InputContext::Combat:
            case InputContext::Sneaking:
            case InputContext::Riding:
            case InputContext::Werewolf:
            case InputContext::VampireLord:
            case InputContext::Death:
            case InputContext::Bleedout:
            case InputContext::Ragdoll:
            case InputContext::KillMove:
                return kGameplay;

            case InputContext::InventoryMenu:
            case InputContext::MagicMenu:
                return kInventory;

            case InputContext::FavoritesMenu:
                return kFavorites;

            case InputContext::MapMenu:
            case InputContext::MapMenuContext:
                return kMap;

            case InputContext::JournalMenu:
                return kJournal;

            case InputContext::StatsMenu:
            case InputContext::SkillMenu:
            case InputContext::Stats:
                return kStats;

            case InputContext::BookMenu:
            case InputContext::Book:
                return kBook;

            case InputContext::Console:
                return kConsole;

            case InputContext::Lockpicking:
                return kLockpicking;

            default:
                return kMenu;
            }
        }

        std::span<const std::string_view> ResolveUserEventCandidates(std::string_view actionId)
        {
            static constexpr std::array kJump = { "Jump"sv };
            static constexpr std::array kActivate = { "Activate"sv };
            static constexpr std::array kSprint = { "Sprint"sv };
            static constexpr std::array kSneak = { "Sneak"sv };
            static constexpr std::array kShout = { "Shout"sv };
            static constexpr std::array kAccept = { "Accept"sv };
            static constexpr std::array kCancel = { "Cancel"sv };
            static constexpr std::array kUp = { "Up"sv };
            static constexpr std::array kDown = { "Down"sv };
            static constexpr std::array kPrevPage = { "PrevPage"sv };
            static constexpr std::array kNextPage = { "NextPage"sv };

            if (actionId == actions::Jump) {
                return kJump;
            }
            if (actionId == actions::Activate) {
                return kActivate;
            }
            if (actionId == actions::Sprint) {
                return kSprint;
            }
            if (actionId == actions::Sneak) {
                return kSneak;
            }
            if (actionId == actions::Shout) {
                return kShout;
            }
            if (actionId == actions::MenuConfirm || actionId == actions::ConsoleExecute) {
                return kAccept;
            }
            if (actionId == actions::MenuCancel || actionId == actions::BookClose) {
                return kCancel;
            }
            if (actionId == actions::MenuScrollUp ||
                actionId == actions::DialoguePreviousOption ||
                actionId == actions::FavoritesPreviousItem ||
                actionId == actions::ConsoleHistoryUp) {
                return kUp;
            }
            if (actionId == actions::MenuScrollDown ||
                actionId == actions::DialogueNextOption ||
                actionId == actions::FavoritesNextItem ||
                actionId == actions::ConsoleHistoryDown) {
                return kDown;
            }
            if (actionId == actions::MenuPageUp ||
                actionId == actions::BookPreviousPage ||
                actionId == actions::MenuSortByName) {
                return kPrevPage;
            }
            if (actionId == actions::MenuPageDown ||
                actionId == actions::BookNextPage ||
                actionId == actions::MenuSortByValue) {
                return kNextPage;
            }

            return {};
        }

        std::optional<std::uint8_t> ResolveFallbackScancode(std::string_view actionId)
        {
            using namespace REX::W32;

            if (actionId == actions::Jump) {
                return static_cast<std::uint8_t>(DIK_SPACE);
            }
            if (actionId == actions::Activate) {
                return static_cast<std::uint8_t>(DIK_E);
            }
            if (actionId == actions::Sprint) {
                return static_cast<std::uint8_t>(DIK_LMENU);
            }
            if (actionId == actions::Sneak) {
                return static_cast<std::uint8_t>(DIK_LCONTROL);
            }
            if (actionId == actions::Shout) {
                return static_cast<std::uint8_t>(DIK_Z);
            }
            if (actionId == actions::MenuConfirm || actionId == actions::ConsoleExecute) {
                return static_cast<std::uint8_t>(DIK_RETURN);
            }
            if (actionId == actions::MenuCancel || actionId == actions::BookClose) {
                return static_cast<std::uint8_t>(DIK_TAB);
            }
            if (actionId == actions::MenuScrollUp ||
                actionId == actions::DialoguePreviousOption ||
                actionId == actions::FavoritesPreviousItem ||
                actionId == actions::ConsoleHistoryUp) {
                return static_cast<std::uint8_t>(DIK_UP);
            }
            if (actionId == actions::MenuScrollDown ||
                actionId == actions::DialogueNextOption ||
                actionId == actions::FavoritesNextItem ||
                actionId == actions::ConsoleHistoryDown) {
                return static_cast<std::uint8_t>(DIK_DOWN);
            }
            if (actionId == actions::MenuPageUp ||
                actionId == actions::BookPreviousPage ||
                actionId == actions::MenuSortByName) {
                return static_cast<std::uint8_t>(DIK_PRIOR);
            }
            if (actionId == actions::MenuPageDown ||
                actionId == actions::BookNextPage ||
                actionId == actions::MenuSortByValue) {
                return static_cast<std::uint8_t>(DIK_NEXT);
            }

            return std::nullopt;
        }

        std::optional<std::uint8_t> ResolveCanonicalUiScancode(std::string_view actionId)
        {
            if (actionId == actions::MenuConfirm ||
                actionId == actions::MenuCancel ||
                actionId == actions::MenuScrollUp ||
                actionId == actions::MenuScrollDown ||
                actionId == actions::MenuPageUp ||
                actionId == actions::MenuPageDown ||
                actionId == actions::DialoguePreviousOption ||
                actionId == actions::DialogueNextOption ||
                actionId == actions::FavoritesPreviousItem ||
                actionId == actions::FavoritesNextItem ||
                actionId == actions::BookPreviousPage ||
                actionId == actions::BookNextPage ||
                actionId == actions::BookClose ||
                actionId == actions::MenuSortByName ||
                actionId == actions::MenuSortByValue ||
                actionId == actions::ConsoleExecute ||
                actionId == actions::ConsoleHistoryUp ||
                actionId == actions::ConsoleHistoryDown) {
                return ResolveFallbackScancode(actionId);
            }

            return std::nullopt;
        }

        constexpr bool IsCoexistenceValidationAction(std::string_view actionId)
        {
            return actionId == actions::Jump ||
                actionId == actions::Activate ||
                actionId == actions::Sprint ||
                actionId == actions::Sneak;
        }

        void LogCoexistenceValidationProducer(
            std::string_view phase,
            std::string_view actionId,
            std::uint8_t scancode,
            bool viaBridge,
            std::uint32_t count,
            InputContext context)
        {
            static_cast<void>(phase);
            static_cast<void>(actionId);
            static_cast<void>(scancode);
            static_cast<void>(viaBridge);
            static_cast<void>(count);
            static_cast<void>(context);
        }

        bool ShouldDispatchTransition(const RE::BSWin32KeyboardDevice& device, std::uint8_t scancode)
        {
            using namespace REX::W32;

            if (scancode != static_cast<std::uint8_t>(DIK_TAB)) {
                return true;
            }

            return (device.curState[DIK_LMENU] & 0x80u) == 0 &&
                (device.curState[DIK_RMENU] & 0x80u) == 0 &&
                (device.curState[DIK_LSHIFT] & 0x80u) == 0 &&
                (device.curState[DIK_RSHIFT] & 0x80u) == 0;
        }

        struct PollPostEventHook
        {
            static void __fastcall Helper(RE::BSWin32KeyboardDevice* keyboard, float timeDelta)
            {
                if (!keyboard) {
                    return;
                }

                auto& backend = KeyboardNativeBackend::GetSingleton();
                if (backend.GetHookMode() == UpstreamKeyboardHookMode::SemanticMid) {
                    PadEventSnapshotDispatcher::GetSingleton().DrainOnMainThread();
                    backend.InjectControlSemantics(*keyboard, timeDelta);
                } else {
                    backend.ObservePostEventState(*keyboard);
                }
            }

            static inline KeyboardControlEvent_t _dispatchControl{ nullptr };
            static inline std::uintptr_t _returnAddress{ 0 };
            static inline std::byte* _stub{ nullptr };
        };

        struct DiObjDataHook
        {
            static std::uint32_t __fastcall Thunk(
                void* manager,
                REX::W32::IDirectInputDevice8A* directInputDevice,
                std::uint32_t* numEvents,
                REX::W32::DIDEVICEOBJECTDATA* eventBuffer)
            {
                PadEventSnapshotDispatcher::GetSingleton().DrainOnMainThread();

                const bool logEnabled = RuntimeConfig::GetSingleton().LogKeyboardInjection();
                constexpr auto kFocusScancode = static_cast<std::uint8_t>(REX::W32::DIK::DIK_SPACE);
                auto* keyboard = eventBuffer != nullptr ?
                    reinterpret_cast<RE::BSWin32KeyboardDevice*>(
                        reinterpret_cast<std::byte*>(eventBuffer) - offsetof(RE::BSWin32KeyboardDevice, diObjData)) :
                    nullptr;

                const auto result = _original ?
                    _original(manager, directInputDevice, numEvents, eventBuffer) :
                    std::numeric_limits<std::uint32_t>::max();

                if (result != 0 || !numEvents || !eventBuffer) {
                    return result;
                }

                const auto originalCount =
                    (std::min)(*numEvents, static_cast<std::uint32_t>(std::size(keyboard->diObjData)));

                KeyboardNativeBackend::GetSingleton().InjectDiObjDataEvents(
                    *keyboard,
                    directInputDevice,
                    *numEvents,
                    eventBuffer);

                const auto finalCount =
                    (std::min)(*numEvents, static_cast<std::uint32_t>(std::size(keyboard->diObjData)));

                const bool nativeBufferHasFocus =
                    EventBufferContainsScancode(eventBuffer, originalCount, kFocusScancode);
                const bool finalBufferHasFocus =
                    EventBufferContainsScancode(eventBuffer, finalCount, kFocusScancode);
                const auto recentBridgeProducer = KeyboardNativeBridge::GetSingleton().HasRecentProducerHeartbeat();
                const bool shouldLog =
                    logEnabled &&
                    _logCount < 64 &&
                    (nativeBufferHasFocus || finalBufferHasFocus || recentBridgeProducer);

                if (shouldLog) {
                    logger::info(
                        "[DualPad][KeyboardNative] GetDeviceData envelope result={} originalCount={} finalCount={} nativeFocus={} finalFocus={} recentBridgeProducer={} directInput=0x{:X} device=0x{:X}",
                        result,
                        originalCount,
                        finalCount,
                        nativeBufferHasFocus,
                        finalBufferHasFocus,
                        recentBridgeProducer,
                        reinterpret_cast<std::uintptr_t>(directInputDevice),
                        reinterpret_cast<std::uintptr_t>(keyboard));
                    LogDiObjDataBufferSummary("GETDEVICEDATA WRAPPER BUFFER", eventBuffer, originalCount, kFocusScancode);
                    LogDiObjDataBufferSummary("GETDEVICEDATA RETURN BUFFER", eventBuffer, finalCount, kFocusScancode);

                    ++_logCount;
                }
                return result;
            }

            static inline KeyboardGetDeviceData_t _original{ nullptr };
            static inline std::uint32_t _logCount{ 0 };
        };
        void WriteAbsoluteJumpStub(std::byte* stub)
        {
            auto* out = reinterpret_cast<std::uint8_t*>(stub);
            std::size_t i = 0;

            auto emit = [&](std::initializer_list<std::uint8_t> bytes) {
                for (const auto byte : bytes) {
                    out[i++] = byte;
                }
            };
            auto emit64 = [&](std::uintptr_t value) {
                std::memcpy(out + i, &value, sizeof(value));
                i += sizeof(value);
            };

            emit({ 0x4C, 0x8B, 0x6C, 0x24, 0x50 });  // mov r13, [rsp+50h]
            emit({ 0x48, 0x83, 0xEC, 0x20 });        // sub rsp, 20h
            emit({ 0x48, 0x89, 0xD9 });              // mov rcx, rbx
            emit({ 0x0F, 0x28, 0xCE });              // movaps xmm1, xmm6
            emit({ 0x48, 0xB8 });                    // mov rax, imm64
            emit64(reinterpret_cast<std::uintptr_t>(&PollPostEventHook::Helper));
            emit({ 0xFF, 0xD0 });                    // call rax
            emit({ 0x48, 0x83, 0xC4, 0x20 });        // add rsp, 20h
            emit({ 0x48, 0xB8 });                    // mov rax, imm64
            emit64(PollPostEventHook::_returnAddress);
            emit({ 0xFF, 0xE0 });                    // jmp rax

            while (i < 64) {
                out[i++] = 0xCC;
            }
        }

    }

    KeyboardNativeBackend& KeyboardNativeBackend::GetSingleton()
    {
        static KeyboardNativeBackend instance;
        return instance;
    }

    void KeyboardNativeBackend::Install()
    {
        if (_installed || _attemptedInstall) {
            return;
        }
        _attemptedInstall = true;

        const auto& config = RuntimeConfig::GetSingleton();
        if (!config.UseUpstreamKeyboardHook()) {
            return;
        }

        if (REL::Module::get().version() != kSupportedRuntime) {
            if (!_loggedUnsupportedRuntime) {
                logger::error(
                    "[DualPad][KeyboardNative] Unsupported runtime {}; keyboard semantic route is only enabled on Skyrim SE 1.5.97",
                    REL::Module::get().version().string());
                _loggedUnsupportedRuntime = true;
            }
            return;
        }

        const auto base = REL::Module::get().base();
        const auto pollAddress = base + kKeyboardPollRva;
        if (config.LogKeyboardInjection()) {
            logger::info(
                "[DualPad][KeyboardNative] Legacy preprocess/family-dispatch diagnostics remain disabled on the default install path");
        }

        if (GetHookMode() == UpstreamKeyboardHookMode::DiObjDataCall) {
            const auto targetAddress = base + kKeyboardGetDeviceDataWrapperRva;
            std::uintptr_t callSite = 0;
            for (std::size_t offset = 0; offset < kDiObjDataCallSearchLength; ++offset) {
                const auto candidate = pollAddress + offset;
                const auto* bytes = reinterpret_cast<const std::uint8_t*>(candidate);
                if (bytes[0] != 0xE8) {
                    continue;
                }

                const auto displacement = *reinterpret_cast<const std::int32_t*>(candidate + 1);
                const auto resolvedTarget = candidate + 5 + displacement;
                if (resolvedTarget == targetAddress) {
                    callSite = candidate;
                    break;
                }
            }

            if (callSite == 0) {
                logger::error(
                    "[DualPad][KeyboardNative] Failed to find keyboard GetDeviceData call-site poll=0x{:X} target=0x{:X}",
                    pollAddress,
                    targetAddress);
                return;
            }

            DiObjDataHook::_original = reinterpret_cast<KeyboardGetDeviceData_t>(
                SKSE::GetTrampoline().write_call<5>(
                    callSite,
                    reinterpret_cast<std::uintptr_t>(&DiObjDataHook::Thunk)));
            _installed = DiObjDataHook::_original != nullptr;

            if (!_installed) {
                logger::error(
                    "[DualPad][KeyboardNative] Failed to install keyboard diObjData call-site hook poll=0x{:X} callSite=0x{:X}",
                    pollAddress,
                    callSite);
                return;
            }

            logger::info(
                "[DualPad][KeyboardNative] Installed keyboard diObjData call-site hook poll=0x{:X} callSite=0x{:X} originalTarget=0x{:X}",
                pollAddress,
                callSite,
                targetAddress);

            const auto windowAddress = pollAddress + kKeyboardPollPostEventWindowOffset;
            if (REL::verify_code(windowAddress, kExpectedPollPostEventWindow)) {
                const auto hookAddress = pollAddress + kKeyboardPollPostEventOffset;
                PollPostEventHook::_returnAddress = hookAddress + 5;
                PollPostEventHook::_dispatchControl = reinterpret_cast<KeyboardControlEvent_t>(base + kKeyboardControlEventRva);
                PollPostEventHook::_stub = static_cast<std::byte*>(SKSE::GetTrampoline().allocate(64));
                WriteAbsoluteJumpStub(PollPostEventHook::_stub);
                SKSE::GetTrampoline().write_branch<5>(hookAddress, reinterpret_cast<std::uintptr_t>(PollPostEventHook::_stub));
                logger::info(
                    "[DualPad][KeyboardNative] Installed post-loop observation hook poll=0x{:X} hookSite=0x{:X}",
                    pollAddress,
                    hookAddress);
            } else {
                logger::warn(
                    "[DualPad][KeyboardNative] Post-loop observation hook verification failed at poll=0x{:X}; continuing with diObjData call-site only",
                    pollAddress);
            }
            return;
        }

        const auto windowAddress = pollAddress + kKeyboardPollPostEventWindowOffset;
        if (!REL::verify_code(windowAddress, kExpectedPollPostEventWindow)) {
            logger::error(
                "[DualPad][KeyboardNative] Poll semantic hook verification failed at poll=0x{:X}; falling back to compatibility route",
                pollAddress);
            return;
        }

        const auto hookAddress = pollAddress + kKeyboardPollPostEventOffset;
        PollPostEventHook::_returnAddress = hookAddress + 5;
        PollPostEventHook::_dispatchControl = reinterpret_cast<KeyboardControlEvent_t>(base + kKeyboardControlEventRva);
        PollPostEventHook::_stub = static_cast<std::byte*>(SKSE::GetTrampoline().allocate(64));
        WriteAbsoluteJumpStub(PollPostEventHook::_stub);
        SKSE::GetTrampoline().write_branch<5>(hookAddress, reinterpret_cast<std::uintptr_t>(PollPostEventHook::_stub));
        _installed = true;

        logger::info(
            "[DualPad][KeyboardNative] Installed keyboard semantic mid-hook poll=0x{:X} hookSite=0x{:X} controlDispatch=0x{:X}",
            pollAddress,
            hookAddress,
            base + kKeyboardControlEventRva);
    }

    bool KeyboardNativeBackend::IsInstalled() const
    {
        return _installed;
    }

    bool KeyboardNativeBackend::IsRouteActive() const
    {
        return _installed && RuntimeConfig::GetSingleton().UseUpstreamKeyboardHook();
    }

    UpstreamKeyboardHookMode KeyboardNativeBackend::GetHookMode() const
    {
        return RuntimeConfig::GetSingleton().GetUpstreamKeyboardHookMode();
    }

    void KeyboardNativeBackend::Reset()
    {
        {
            std::scoped_lock lock(_mutex);
            _localDesiredState.Clear();
            _bridgeDesiredRefCounts.fill(0);
            _syntheticLatchedDown.fill(false);
            _activeActionScancodes.clear();
            _deferredActions.clear();
        }

        if (KeyboardNativeBridge::GetSingleton().HasConsumerHeartbeat()) {
            (void)KeyboardNativeBridge::GetSingleton().EnqueueReset();
        }

    }

    void KeyboardNativeBackend::ConsumeDesiredStateLocked(
        DesiredKeyboardState& desiredState,
        std::array<bool, 256>& syntheticPrevDown)
    {
        desiredState = _localDesiredState;
        syntheticPrevDown = _syntheticLatchedDown;
        AppendScheduledPulseStateLocked(desiredState);
        _localDesiredState.pendingPulseCounts.fill(0);
        _localDesiredState.pendingTransactionalPulseCounts.fill(0);
        AdvanceScheduledPulseActionsLocked();
    }

    void KeyboardNativeBackend::MarkProbeScancodeLocked(std::uint8_t scancode)
    {
        _pendingPostEventProbe[scancode] = true;
    }

    bool KeyboardNativeBackend::CanHandleAction(std::string_view actionId) const
    {
        return ResolveUserEventCandidates(actionId).size() != 0 || ResolveFallbackScancode(actionId).has_value();
    }

    bool KeyboardNativeBackend::CanEmitActionNow(std::string_view actionId, InputContext context) const
    {
        static_cast<void>(actionId);
        static_cast<void>(context);
        return true;
    }

    void KeyboardNativeBackend::StageDeferredTriggerLocked(
        std::string_view actionId,
        ActionOutputContract contract,
        InputContext context)
    {
        auto& deferred = _deferredActions[std::string(actionId)];
        deferred.contract = contract;
        deferred.context = context;
        deferred.pendingTriggerPulse = true;

        if (IsDebugLoggingEnabled()) {
            logger::info(
                "[DualPad][KeyboardNative] deferred trigger action={} contract={} context={}",
                actionId,
                ToString(contract),
                ToString(context));
        }
    }

    void KeyboardNativeBackend::StageDeferredStateLocked(
        std::string_view actionId,
        ActionOutputContract contract,
        bool pressed,
        float heldSeconds,
        InputContext context)
    {
        if (!pressed) {
            _deferredActions.erase(actionId);
            if (IsDebugLoggingEnabled()) {
                logger::info(
                    "[DualPad][KeyboardNative] cleared deferred state action={} contract={} context={}",
                    actionId,
                    ToString(contract),
                    ToString(context));
            }
            return;
        }

        auto& deferred = _deferredActions[std::string(actionId)];
        deferred.contract = contract;
        deferred.context = context;
        deferred.sourceDown = true;
        deferred.heldSeconds = heldSeconds;
        deferred.pendingTriggerPulse = false;

        if (IsDebugLoggingEnabled()) {
            logger::info(
                "[DualPad][KeyboardNative] deferred state action={} contract={} held={:.3f} context={}",
                actionId,
                ToString(contract),
                heldSeconds,
                ToString(context));
        }
    }

    void KeyboardNativeBackend::SuspendActiveActionLocked(std::string_view actionId)
    {
        const auto it = _activeActionScancodes.find(actionId);
        if (it == _activeActionScancodes.end()) {
            return;
        }

        const auto action = it->second;
        if (action.viaBridge &&
            UsesContinuousState(action.contract) &&
            _bridgeDesiredRefCounts[action.scancode] > 0) {
            --_bridgeDesiredRefCounts[action.scancode];
            if (_bridgeDesiredRefCounts[action.scancode] == 0) {
                (void)KeyboardNativeBridge::GetSingleton().EnqueueRelease(action.scancode);
            }
        }
        else if (!action.viaBridge &&
            UsesContinuousState(action.contract) &&
            _localDesiredState.desiredRefCounts[action.scancode] > 0) {
            --_localDesiredState.desiredRefCounts[action.scancode];
        }

        _activeActionScancodes.erase(it);
    }

    void KeyboardNativeBackend::FlushDeferredActionsIfReady()
    {
        if (t_replayingDeferredActions) {
            return;
        }

        struct DeferredReplay
        {
            std::string actionId;
            DeferredKeyboardAction action;
        };

        std::vector<DeferredReplay> readyActions;
        {
            std::scoped_lock lock(_mutex);
            for (auto it = _deferredActions.begin(); it != _deferredActions.end();) {
                if (!CanEmitActionNow(it->first, it->second.context)) {
                    ++it;
                    continue;
                }

                readyActions.push_back(DeferredReplay{ it->first, it->second });
                it = _deferredActions.erase(it);
            }
        }

        if (readyActions.empty()) {
            return;
        }

        t_replayingDeferredActions = true;
        for (const auto& replay : readyActions) {
            if (IsDebugLoggingEnabled()) {
                logger::info(
                    "[DualPad][KeyboardNative] replay deferred action={} contract={} pulse={} sourceDown={} held={:.3f} context={}",
                    replay.actionId,
                    ToString(replay.action.contract),
                    replay.action.pendingTriggerPulse,
                    replay.action.sourceDown,
                    replay.action.heldSeconds,
                    ToString(replay.action.context));
            }

            if (replay.action.pendingTriggerPulse) {
                (void)TriggerAction(replay.actionId, replay.action.contract, replay.action.context);
                continue;
            }

            if (replay.action.sourceDown) {
                (void)SubmitActionState(
                    replay.actionId,
                    replay.action.contract,
                    true,
                    replay.action.heldSeconds,
                    replay.action.context);
            }
        }
        t_replayingDeferredActions = false;
    }

    bool KeyboardNativeBackend::TriggerAction(
        std::string_view actionId,
        ActionOutputContract contract,
        InputContext context)
    {
        if (!IsRouteActive()) {
            return false;
        }

        if (!t_replayingDeferredActions) {
            FlushDeferredActionsIfReady();
        }

        if (!CanEmitActionNow(actionId, context)) {
            std::scoped_lock lock(_mutex);
            StageDeferredTriggerLocked(actionId, contract, context);
            return true;
        }

        const auto scancode = ResolveScancode(actionId, context);
        if (!scancode) {
            return false;
        }

        const bool useTransactionalTriggerPulse =
            contract == ActionOutputContract::Pulse &&
            GetHookMode() == UpstreamKeyboardHookMode::DiObjDataCall;
        const auto bridgeActive =
            !useTransactionalTriggerPulse &&
            KeyboardNativeBridge::GetSingleton().HasConsumerHeartbeat();
        if (bridgeActive && KeyboardNativeBridge::GetSingleton().EnqueuePulse(*scancode)) {
            LogCoexistenceValidationProducer("pulse", actionId, *scancode, true, 1, context);
            if (IsDebugLoggingEnabled()) {
                logger::info(
                    "[DualPad][KeyboardNative] bridged pulse action={} contract={} scancode=0x{:02X} context={}",
                    actionId,
                    ToString(contract),
                    *scancode,
                    ToString(context));
            }
            return true;
        }

        std::scoped_lock lock(_mutex);
        if (useTransactionalTriggerPulse) {
            StageTransactionalPulseLocked(*scancode);
        } else {
            StageWindowedPulseLocked(*scancode);
        }
        LogCoexistenceValidationProducer("pulse", actionId, *scancode, false, 1, context);

        if (IsDebugLoggingEnabled()) {
            logger::info(
                "[DualPad][KeyboardNative] staged pulse action={} contract={} scancode=0x{:02X} context={} delivery={}",
                actionId,
                ToString(contract),
                *scancode,
                ToString(context),
                useTransactionalTriggerPulse ? "transactional" : "windowed");
        }

        return true;
    }

    bool KeyboardNativeBackend::SubmitActionState(
        std::string_view actionId,
        ActionOutputContract contract,
        bool pressed,
        float heldSeconds,
        InputContext context)
    {
        if (!IsRouteActive()) {
            return false;
        }

        if (!t_replayingDeferredActions) {
            FlushDeferredActionsIfReady();
        }

        if (!CanEmitActionNow(actionId, context)) {
            std::scoped_lock lock(_mutex);
            SuspendActiveActionLocked(actionId);
            StageDeferredStateLocked(actionId, contract, pressed, heldSeconds, context);
            return true;
        }

        if (UsesScheduledPulseContract(contract)) {
            return SubmitScheduledPulseActionState(actionId, contract, pressed, heldSeconds, context);
        }

        if (UsesDebouncedPulse(contract)) {
            if (!pressed) {
                std::scoped_lock lock(_mutex);
                const auto it = _activeActionScancodes.find(actionId);
                if (it == _activeActionScancodes.end()) {
                    return false;
                }

                _activeActionScancodes.erase(it);
                if (IsDebugLoggingEnabled()) {
                    logger::info(
                        "[DualPad][KeyboardNative] cleared source lifecycle action={} contract={} context={}",
                        actionId,
                        ToString(contract),
                        ToString(context));
                }
                return true;
            }

            {
                std::scoped_lock lock(_mutex);
                if (_activeActionScancodes.find(actionId) != _activeActionScancodes.end()) {
                    return true;
                }
            }

            const auto scancode = ResolveScancode(actionId, context);
            if (!scancode) {
                return false;
            }

            const auto bridgeActive = KeyboardNativeBridge::GetSingleton().HasConsumerHeartbeat();
            bool viaBridge = false;
            if (bridgeActive && KeyboardNativeBridge::GetSingleton().EnqueuePulse(*scancode)) {
                viaBridge = true;
            }
            else {
                std::scoped_lock lock(_mutex);
                StageWindowedPulseLocked(*scancode);
            }

            std::scoped_lock lock(_mutex);
            _activeActionScancodes[std::string(actionId)] = ActiveKeyboardAction{ *scancode, viaBridge, contract };
            if (IsDebugLoggingEnabled()) {
                logger::info(
                    "[DualPad][KeyboardNative] latched source lifecycle action={} contract={} scancode=0x{:02X} viaBridge={} context={} delivery={}",
                    actionId,
                    ToString(contract),
                    *scancode,
                    viaBridge,
                    ToString(context),
                    viaBridge ? "bridge-windowed" : "local-windowed");
            }

            return true;
        }

        if (!UsesContinuousState(contract)) {
            return pressed ? TriggerAction(actionId, contract, context) : true;
        }

        const auto bridgeActive = KeyboardNativeBridge::GetSingleton().HasConsumerHeartbeat();
        std::scoped_lock lock(_mutex);
        if (pressed) {
            if (_activeActionScancodes.find(actionId) != _activeActionScancodes.end()) {
                return true;
            }

            const auto scancode = ResolveScancode(actionId, context);
            if (!scancode) {
                return false;
            }

            if (bridgeActive) {
                const auto previousBridgeCount = _bridgeDesiredRefCounts[*scancode];
                if (_bridgeDesiredRefCounts[*scancode] != std::numeric_limits<std::uint8_t>::max()) {
                    ++_bridgeDesiredRefCounts[*scancode];
                }

                const auto needsBridgePress = previousBridgeCount == 0;
                const auto bridgeQueued =
                    !needsBridgePress || KeyboardNativeBridge::GetSingleton().EnqueuePress(*scancode);
                if (bridgeQueued) {
                    _activeActionScancodes[std::string(actionId)] = ActiveKeyboardAction{ *scancode, true, contract };
                    LogCoexistenceValidationProducer(
                        "down",
                        actionId,
                        *scancode,
                        true,
                        _bridgeDesiredRefCounts[*scancode],
                        context);

                    if (IsDebugLoggingEnabled()) {
                        logger::info(
                            "[DualPad][KeyboardNative] bridged down action={} contract={} scancode=0x{:02X} count={} context={}",
                            actionId,
                            ToString(contract),
                            *scancode,
                            _bridgeDesiredRefCounts[*scancode],
                            ToString(context));
                    }

                    return true;
                }

                if (_bridgeDesiredRefCounts[*scancode] > 0) {
                    --_bridgeDesiredRefCounts[*scancode];
                }
            }

            if (_localDesiredState.desiredRefCounts[*scancode] != std::numeric_limits<std::uint8_t>::max()) {
                ++_localDesiredState.desiredRefCounts[*scancode];
            }
            _activeActionScancodes[std::string(actionId)] = ActiveKeyboardAction{ *scancode, false, contract };
            LogCoexistenceValidationProducer(
                "down",
                actionId,
                *scancode,
                false,
                _localDesiredState.desiredRefCounts[*scancode],
                context);

            if (IsDebugLoggingEnabled()) {
                logger::info(
                    "[DualPad][KeyboardNative] queued down action={} contract={} scancode=0x{:02X} count={} context={}",
                    actionId,
                    ToString(contract),
                    *scancode,
                    _localDesiredState.desiredRefCounts[*scancode],
                    ToString(context));
            }

            return true;
        }

        const auto it = _activeActionScancodes.find(actionId);
        if (it == _activeActionScancodes.end()) {
            return false;
        }

        const auto activeAction = it->second;
        if (activeAction.viaBridge) {
            bool bridgeQueued = true;
            if (_bridgeDesiredRefCounts[activeAction.scancode] > 0) {
                --_bridgeDesiredRefCounts[activeAction.scancode];
                if (_bridgeDesiredRefCounts[activeAction.scancode] == 0) {
                    bridgeQueued = KeyboardNativeBridge::GetSingleton().EnqueueRelease(activeAction.scancode);
                    if (!bridgeQueued) {
                        ++_bridgeDesiredRefCounts[activeAction.scancode];
                    }
                }
            }

            if (!bridgeQueued) {
                return false;
            }

            _activeActionScancodes.erase(it);
            LogCoexistenceValidationProducer(
                "up",
                actionId,
                activeAction.scancode,
                true,
                _bridgeDesiredRefCounts[activeAction.scancode],
                context);

            if (IsDebugLoggingEnabled()) {
                logger::info(
                    "[DualPad][KeyboardNative] bridged up action={} contract={} scancode=0x{:02X} count={} context={}",
                    actionId,
                    ToString(activeAction.contract),
                    activeAction.scancode,
                    _bridgeDesiredRefCounts[activeAction.scancode],
                    ToString(context));
            }

            return true;
        }

        const auto scancode = activeAction.scancode;
        if (_localDesiredState.desiredRefCounts[scancode] > 0) {
            --_localDesiredState.desiredRefCounts[scancode];
        }
        _activeActionScancodes.erase(it);
        LogCoexistenceValidationProducer(
            "up",
            actionId,
            scancode,
            false,
            _localDesiredState.desiredRefCounts[scancode],
            context);

        if (IsDebugLoggingEnabled()) {
            logger::info(
                "[DualPad][KeyboardNative] queued up action={} contract={} scancode=0x{:02X} count={} context={}",
                actionId,
                ToString(activeAction.contract),
                scancode,
                _localDesiredState.desiredRefCounts[scancode],
                ToString(context));
        }

        return true;
    }

    void KeyboardNativeBackend::InjectControlSemantics(RE::BSWin32KeyboardDevice& device, float timeDelta)
    {
        if (!IsRouteActive() ||
            GetHookMode() != UpstreamKeyboardHookMode::SemanticMid ||
            !device.dInputDevice ||
            !PollPostEventHook::_dispatchControl) {
            return;
        }

        FlushDeferredActionsIfReady();

        std::array<std::uint8_t, 0x100> physicalState{};
        if (auto* inputManager = RE::BSDirectInputManager::GetSingleton(); inputManager) {
            inputManager->GetDeviceState(
                reinterpret_cast<REX::W32::IDirectInputDevice8A*>(device.dInputDevice),
                static_cast<std::uint32_t>(physicalState.size()),
                physicalState.data());
        }

        DesiredKeyboardState desiredState{};
        std::array<bool, 256> syntheticPrevDown{};
        {
            std::scoped_lock lock(_mutex);
            ConsumeDesiredStateLocked(desiredState, syntheticPrevDown);
        }

        const bool textEntryActive = IsTextEntryActive();
        std::size_t updatedCount = 0;
        std::size_t transitionCount = 0;

        for (std::size_t scancode = 0; scancode < desiredState.desiredRefCounts.size(); ++scancode) {
            const bool synthDesired =
                desiredState.desiredRefCounts[scancode] > 0 ||
                desiredState.pendingPulseCounts[scancode] > 0;
            const bool synthCurrent = synthDesired &&
                (!textEntryActive || IsTextSafeScancode(static_cast<std::uint8_t>(scancode)));
            const bool synthPrevious = syntheticPrevDown[scancode];
            const bool effectivePrevious = (device.prevState[scancode] & 0x80u) != 0;
            const bool physicalCurrent = (physicalState[scancode] & 0x80u) != 0;
            const bool effectiveCurrent = physicalCurrent || synthCurrent;
            const auto nextValue = static_cast<std::uint8_t>(effectiveCurrent ? 0x80u : 0x00u);

            if (device.curState[scancode] != nextValue) {
                ++updatedCount;
            }
            device.curState[scancode] = nextValue;

            if (synthPrevious != synthCurrent &&
                effectivePrevious != effectiveCurrent &&
                ShouldDispatchTransition(device, static_cast<std::uint8_t>(scancode))) {
                PollPostEventHook::_dispatchControl(
                    &device,
                    static_cast<std::uint32_t>(scancode),
                    timeDelta,
                    effectivePrevious,
                    effectiveCurrent);
                ++transitionCount;

                if (IsDebugLoggingEnabled()) {
                    logger::info(
                        "[DualPad][KeyboardNative] dispatched transition scancode=0x{:02X} prev={} cur={} physicalCur={} synthPrev={} synthCur={} textEntry={}",
                        static_cast<std::uint32_t>(scancode),
                        effectivePrevious,
                        effectiveCurrent,
                        physicalCurrent,
                        synthPrevious,
                        synthCurrent,
                        textEntryActive);
                }
            }

            syntheticPrevDown[scancode] = synthCurrent;
        }

        {
            std::scoped_lock lock(_mutex);
            _syntheticLatchedDown = syntheticPrevDown;
        }

        if (IsDebugLoggingEnabled() && (updatedCount != 0 || transitionCount != 0)) {
            logger::info(
                "[DualPad][KeyboardNative] semantic pass updated={} transitions={} textEntry={}",
                updatedCount,
                transitionCount,
                textEntryActive);
        }
    }

        void KeyboardNativeBackend::InjectDiObjDataEvents(
            RE::BSWin32KeyboardDevice& device,
            REX::W32::IDirectInputDevice8A* directInputDevice,
            std::uint32_t& numEvents,
            REX::W32::DIDEVICEOBJECTDATA* eventBuffer)
    {
        if (!IsRouteActive() ||
            GetHookMode() != UpstreamKeyboardHookMode::DiObjDataCall ||
            !eventBuffer) {
            return;
        }

        FlushDeferredActionsIfReady();

        if (reinterpret_cast<void*>(device.dInputDevice) != reinterpret_cast<void*>(directInputDevice)) {
            logger::warn(
                "[DualPad][KeyboardNative] diObjData injection skipped due to device mismatch deviceObj=0x{:X} hookDevice=0x{:X}",
                reinterpret_cast<std::uintptr_t>(device.dInputDevice),
                reinterpret_cast<std::uintptr_t>(directInputDevice));
            return;
        }

        const auto clampedOriginalCount = (std::min)(numEvents, static_cast<std::uint32_t>(std::size(device.diObjData)));
        const auto deviceAddress = reinterpret_cast<std::uintptr_t>(&device);
        const auto deviceId = *reinterpret_cast<const std::uint32_t*>(deviceAddress + 0x08);

        if (IsDebugLoggingEnabled() && clampedOriginalCount > 0) {
            logger::info(
                "[DualPad][KeyboardNative] native device object=0x{:X} deviceObject+0x08={} numEvents={}",
                deviceAddress,
                deviceId,
                clampedOriginalCount);

            const auto nativeToLog = (std::min)(clampedOriginalCount, 4u);
            for (std::uint32_t i = 0; i < nativeToLog; ++i) {
                logger::info(
                    "[DualPad][KeyboardNative]   NativeEvent[{}] scancode=0x{:02X} data=0x{:02X}",
                    i,
                    eventBuffer[i].ofs,
                    eventBuffer[i].data);
            }

            LogRuntimeGlobalObjectDebug("BEFORE native keyboard processing");

            {
                std::scoped_lock lock(_mutex);
                _pendingNativeGlobalDebug = true;
            }
        }

        DesiredKeyboardState desiredState{};
        std::array<bool, 256> syntheticPrevDown{};
        {
            std::scoped_lock lock(_mutex);
            ConsumeDesiredStateLocked(desiredState, syntheticPrevDown);
        }

        const bool textEntryActive = IsTextEntryActive();
        std::array<std::uint8_t, 256> predictedState{};
        std::array<bool, 256> syntheticNextDown{};
        std::array<bool, 256> nativeCandidateByScancode{};
        std::memcpy(predictedState.data(), device.curState, predictedState.size());
        syntheticNextDown = syntheticPrevDown;

        for (std::uint32_t i = 0; i < clampedOriginalCount; ++i) {
            const auto scancode = static_cast<std::uint8_t>(eventBuffer[i].ofs & 0xFFu);
            predictedState[scancode] = static_cast<std::uint8_t>(eventBuffer[i].data & 0x80u);
            nativeCandidateByScancode[scancode] = true;
        }

        std::uint32_t injectedCount = 0;
        const auto remainingCapacity = static_cast<std::uint32_t>(std::size(device.diObjData)) - clampedOriginalCount;
        const auto appendSyntheticEvent = [&](const std::uint8_t scancode, const bool down) -> std::int32_t {
            if (injectedCount >= remainingCapacity) {
                return -1;
            }

            auto& syntheticEvent = eventBuffer[clampedOriginalCount + injectedCount];
            syntheticEvent.ofs = static_cast<std::uint32_t>(scancode);
            syntheticEvent.data = down ? 0x80u : 0x00u;
            syntheticEvent.timeStamp = 0;
            syntheticEvent.sequence = 0;
            syntheticEvent.appData = 0;

            predictedState[scancode] = static_cast<std::uint8_t>(syntheticEvent.data);
            ++injectedCount;
            return static_cast<std::int32_t>(clampedOriginalCount + injectedCount - 1);
        };
        const auto applyNativeLikeMetadata = [&](const std::int32_t slot, const std::uint32_t timeStamp, const std::uint32_t sequence) {
            if (slot < 0) {
                return;
            }

            auto& syntheticEvent = eventBuffer[slot];
            syntheticEvent.timeStamp = static_cast<decltype(syntheticEvent.timeStamp)>(timeStamp);
            syntheticEvent.sequence = static_cast<decltype(syntheticEvent.sequence)>(sequence);
            syntheticEvent.appData = static_cast<decltype(syntheticEvent.appData)>(std::numeric_limits<std::uintptr_t>::max());
        };
        const auto currentContext = input::ContextManager::GetSingleton().GetCurrentContext();
        const auto saturatingAddPulseCount = [](std::uint8_t& target, const std::uint8_t value) {
            const auto sum = static_cast<std::uint16_t>(target) + value;
            target = sum > 0x00FFu ?
                static_cast<std::uint8_t>(0xFFu) :
                static_cast<std::uint8_t>(sum);
        };
        auto jumpScancode = ResolveScancode(actions::Jump, currentContext);
        if (!jumpScancode) {
            jumpScancode = ResolveScancode(actions::Jump, InputContext::Gameplay);
        }
        std::uint8_t jumpDesiredCount = 0;
        std::uint8_t jumpPendingPulseCount = 0;
        std::uint8_t jumpPendingTransactionalPulseCount = 0;
        bool jumpSyntheticPrevDown = false;
        bool jumpPredictedCurrent = false;
        bool jumpSynthDesired = false;
        bool jumpSynthCurrent = false;
        bool jumpInjected = false;
        std::int32_t jumpInjectedSlot = -1;
        bool nativeJumpCandidate = false;
        bool jumpPairedPulseInjected = false;
        if (jumpScancode) {
            jumpDesiredCount = desiredState.desiredRefCounts[*jumpScancode];
            jumpPendingPulseCount = desiredState.pendingPulseCounts[*jumpScancode];
            jumpPendingTransactionalPulseCount = desiredState.pendingTransactionalPulseCounts[*jumpScancode];
            jumpSyntheticPrevDown = syntheticPrevDown[*jumpScancode];
            jumpPredictedCurrent = (predictedState[*jumpScancode] & 0x80u) != 0;
            const bool jumpWindowedDesired = jumpDesiredCount > 0 || jumpPendingPulseCount > 0;
            jumpSynthDesired = jumpWindowedDesired || jumpPendingTransactionalPulseCount > 0;
            jumpSynthCurrent = jumpWindowedDesired &&
                (!textEntryActive || IsTextSafeScancode(*jumpScancode));
            nativeJumpCandidate = nativeCandidateByScancode[*jumpScancode];
        }
        for (std::size_t scancode = 0; scancode < desiredState.pendingTransactionalPulseCounts.size(); ++scancode) {
            auto remainingTransactionalPulses = desiredState.pendingTransactionalPulseCounts[scancode];
            if (remainingTransactionalPulses == 0) {
                continue;
            }

            const auto scancodeByte = static_cast<std::uint8_t>(scancode);
            const bool textSafe = !textEntryActive || IsTextSafeScancode(scancodeByte);
            const bool predictedCurrent = (predictedState[scancode] & 0x80u) != 0;
            const bool synthPrevious = syntheticPrevDown[scancode];
            const bool canInjectTransactionalPair =
                textSafe &&
                !nativeCandidateByScancode[scancode] &&
                !predictedCurrent &&
                !synthPrevious;

            if (canInjectTransactionalPair) {
                while (remainingTransactionalPulses > 0 &&
                    (remainingCapacity - injectedCount) >= 2) {
                    const auto pressSlot = appendSyntheticEvent(scancodeByte, true);
                    const auto releaseSlot = appendSyntheticEvent(scancodeByte, false);
                    if (pressSlot < 0 || releaseSlot < 0) {
                        break;
                    }

                    const auto syntheticTimeBase = g_syntheticKeyboardEventTimeStamp.fetch_add(2, std::memory_order_relaxed);
                    const auto syntheticSequenceBase = g_syntheticKeyboardEventSequence.fetch_add(2, std::memory_order_relaxed);
                    applyNativeLikeMetadata(pressSlot, syntheticTimeBase, syntheticSequenceBase);
                    applyNativeLikeMetadata(releaseSlot, syntheticTimeBase + 1, syntheticSequenceBase + 1);
                    --remainingTransactionalPulses;

                    {
                        std::scoped_lock lock(_mutex);
                        MarkProbeScancodeLocked(scancodeByte);
                    }

                    if (jumpScancode && scancodeByte == *jumpScancode) {
                        jumpInjected = true;
                        jumpInjectedSlot = pressSlot;
                        jumpPairedPulseInjected = true;
                    }

                    if (IsDebugLoggingEnabled()) {
                        logger::info(
                            "[DualPad][KeyboardNative] injected transactional pulse diObjData scancode=0x{:02X} pressSlot={} releaseSlot={} pressTs=0x{:X} releaseTs=0x{:X} pressSeq=0x{:X} releaseSeq=0x{:X} originalCount={} textEntry={}",
                            static_cast<std::uint32_t>(scancodeByte),
                            pressSlot,
                            releaseSlot,
                            syntheticTimeBase,
                            syntheticTimeBase + 1,
                            syntheticSequenceBase,
                            syntheticSequenceBase + 1,
                            clampedOriginalCount,
                            textEntryActive);
                    }
                }
            }

            if (remainingTransactionalPulses != 0) {
                saturatingAddPulseCount(desiredState.pendingPulseCounts[scancode], remainingTransactionalPulses);
                if (IsDebugLoggingEnabled()) {
                    logger::info(
                        "[DualPad][KeyboardNative] transactional pulse fallback to windowed scancode=0x{:02X} remaining={} nativeCandidate={} predictedCurrent={} synthPrev={} textSafe={} capacityLeft={}",
                        static_cast<std::uint32_t>(scancodeByte),
                        remainingTransactionalPulses,
                        nativeCandidateByScancode[scancode],
                        predictedCurrent,
                        synthPrevious,
                        textSafe,
                        remainingCapacity - injectedCount);
                }
            }
        }

        for (std::size_t scancode = 0; scancode < desiredState.desiredRefCounts.size(); ++scancode) {
            if (injectedCount >= remainingCapacity) {
                break;
            }

            const bool synthDesired =
                desiredState.desiredRefCounts[scancode] > 0 ||
                desiredState.pendingPulseCounts[scancode] > 0;
            const bool synthCurrent = synthDesired &&
                (!textEntryActive || IsTextSafeScancode(static_cast<std::uint8_t>(scancode)));
            const bool synthManaged = syntheticPrevDown[scancode] || synthCurrent;
            if (!synthManaged) {
                continue;
            }

            const bool predictedCurrent = (predictedState[scancode] & 0x80u) != 0;
            if (predictedCurrent == synthCurrent) {
                syntheticNextDown[scancode] = (predictedState[scancode] & 0x80u) != 0;
                continue;
            }

            const auto injectedSlot = appendSyntheticEvent(static_cast<std::uint8_t>(scancode), synthCurrent);
            if (injectedSlot < 0) {
                break;
            }

            syntheticNextDown[scancode] = (predictedState[scancode] & 0x80u) != 0;

            if (jumpScancode && static_cast<std::uint8_t>(scancode) == *jumpScancode) {
                jumpInjected = true;
                jumpInjectedSlot = injectedSlot;
            }

            {
                std::scoped_lock lock(_mutex);
                MarkProbeScancodeLocked(static_cast<std::uint8_t>(scancode));
            }

            if (IsDebugLoggingEnabled()) {
                logger::info(
                    "[DualPad][KeyboardNative] injected diObjData scancode=0x{:02X} down={} originalCount={} slot={} textEntry={}",
                    static_cast<std::uint32_t>(scancode),
                    synthCurrent,
                    clampedOriginalCount,
                    injectedSlot,
                    textEntryActive);
            }
        }

        {
            std::scoped_lock lock(_mutex);
            _syntheticLatchedDown = syntheticNextDown;
        }

        if (jumpScancode && IsDebugLoggingEnabled()) {
            const bool shouldLogJumpDecision =
                nativeJumpCandidate ||
                jumpDesiredCount != 0 ||
                jumpPendingPulseCount != 0 ||
                jumpPendingTransactionalPulseCount != 0 ||
                jumpSyntheticPrevDown ||
                jumpPredictedCurrent ||
                jumpInjected;

            if (shouldLogJumpDecision) {
                LogJumpInjectionDecision(
                    *jumpScancode,
                    device,
                    eventBuffer,
                    clampedOriginalCount,
                    clampedOriginalCount + injectedCount,
                    jumpDesiredCount,
                    jumpPendingPulseCount,
                    jumpPendingTransactionalPulseCount,
                    jumpSyntheticPrevDown,
                    jumpPredictedCurrent,
                    jumpSynthDesired,
                    jumpSynthCurrent,
                    jumpInjected,
                    jumpInjectedSlot,
                    nativeJumpCandidate,
                    jumpPairedPulseInjected,
                    (predictedState[*jumpScancode] & 0x80u) != 0,
                    textEntryActive);
            }
        }

        if (injectedCount == 0) {
            return;
        }

        numEvents = clampedOriginalCount + injectedCount;

        if (IsDebugLoggingEnabled()) {
            logger::info(
                "[DualPad][KeyboardNative] diObjData injection original={} injected={} total={} deviceObject+0x08={} textEntry={}",
                clampedOriginalCount,
                injectedCount,
                numEvents,
                deviceId,
                textEntryActive);
        }
    }

    void KeyboardNativeBackend::ObservePostEventState(RE::BSWin32KeyboardDevice& device)
    {
        if (!IsRouteActive() ||
            GetHookMode() != UpstreamKeyboardHookMode::DiObjDataCall ||
            !IsDebugLoggingEnabled()) {
            return;
        }

        static_cast<void>(device);

        std::array<bool, 256> probeFlags{};
        bool logNativeGlobalDebug = false;
        {
            std::scoped_lock lock(_mutex);
            probeFlags = _pendingPostEventProbe;
            _pendingPostEventProbe.fill(false);
            logNativeGlobalDebug = _pendingNativeGlobalDebug;
            _pendingNativeGlobalDebug = false;
        }

        static_cast<void>(probeFlags);
        static_cast<void>(device);

        if (logNativeGlobalDebug) {
            LogRuntimeGlobalObjectDebug("AFTER native keyboard processing");
        }
    }

    std::optional<std::uint8_t> KeyboardNativeBackend::ResolveScancode(
        std::string_view actionId,
        InputContext context) const
    {
        if (const auto canonicalUiScancode = ResolveCanonicalUiScancode(actionId)) {
            return canonicalUiScancode;
        }

        auto* controlMap = RE::ControlMap::GetSingleton();
        const auto candidates = ResolveUserEventCandidates(actionId);
        if (controlMap && !candidates.empty()) {
            const auto searchOrder = ResolveContextSearchOrder(context);
            for (const auto nativeContext : searchOrder) {
                for (const auto candidate : candidates) {
                    const auto mapped = controlMap->GetMappedKey(candidate, RE::INPUT_DEVICE::kKeyboard, nativeContext);
                    if (mapped != RE::ControlMap::kInvalid && mapped != 0xFFu && mapped < 0x100u) {
                        return static_cast<std::uint8_t>(mapped);
                    }
                }
            }
        }

        return ResolveFallbackScancode(actionId);
    }

    bool KeyboardNativeBackend::IsTextEntryActive() const
    {
        const auto context = input::ContextManager::GetSingleton().GetCurrentContext();
        if (context == InputContext::Console || context == InputContext::DebugText) {
            return true;
        }

        if (auto* ui = RE::UI::GetSingleton(); ui) {
            if (ui->IsMenuOpen("Console")) {
                return true;
            }
        }

        return false;
    }

    bool KeyboardNativeBackend::IsTextSafeScancode(std::uint8_t scancode) const
    {
        using namespace REX::W32;

        switch (scancode) {
        case DIK_ESCAPE:
        case DIK_RETURN:
        case DIK_NUMPADENTER:
        case DIK_TAB:
        case DIK_BACK:
        case DIK_UP:
        case DIK_DOWN:
        case DIK_LEFT:
        case DIK_RIGHT:
        case DIK_PRIOR:
        case DIK_NEXT:
        case DIK_HOME:
        case DIK_END:
        case DIK_INSERT:
        case DIK_DELETE:
        case DIK_F1:
        case DIK_F2:
        case DIK_F3:
        case DIK_F4:
        case DIK_F5:
        case DIK_F6:
        case DIK_F7:
        case DIK_F8:
        case DIK_F9:
        case DIK_F10:
        case DIK_F11:
        case DIK_F12:
        case DIK_F13:
        case DIK_F14:
        case DIK_F15:
            return true;
        default:
            return false;
        }
    }

    void KeyboardNativeBackend::StageWindowedPulseLocked(std::uint8_t scancode)
    {
        _localDesiredState.pendingPulseCounts[scancode] = 1;
    }

    void KeyboardNativeBackend::StageTransactionalPulseLocked(std::uint8_t scancode)
    {
        _localDesiredState.pendingTransactionalPulseCounts[scancode] = 1;
    }

    bool KeyboardNativeBackend::SubmitScheduledPulseActionState(
        std::string_view actionId,
        ActionOutputContract contract,
        bool pressed,
        float heldSeconds,
        InputContext context)
    {
        const auto describePhase = [](ScheduledPulsePhase phase) -> std::string_view {
            switch (phase) {
            case ScheduledPulsePhase::Down:
                return "Down";
            case ScheduledPulsePhase::Gap:
                return "Gap";
            case ScheduledPulsePhase::Idle:
            default:
                return "Idle";
            }
        };

        const auto scancode = ResolveScancode(actionId, context);
        if (!scancode) {
            return false;
        }

        std::scoped_lock lock(_mutex);
        if (!pressed) {
            const auto it = _activeActionScancodes.find(actionId);
            if (it == _activeActionScancodes.end()) {
                return false;
            }

            auto& action = it->second;
            action.sourceDown = false;
            if (contract == ActionOutputContract::Repeat) {
                action.nextRepeatAtHeldSeconds = 0.0f;
            }

            const auto phase = action.scheduledPulsePhase;
            const auto remaining = action.scheduledPhaseRemainingConsumes;
            const auto queued = action.queuedScheduledPulses;
            if (action.scheduledPulsePhase == ScheduledPulsePhase::Idle &&
                action.queuedScheduledPulses == 0) {
                _activeActionScancodes.erase(it);
            }

            if (IsDebugLoggingEnabled()) {
                logger::info(
                    "[DualPad][KeyboardNative] released scheduled contract action={} contract={} scancode=0x{:02X} phase={} remaining={} queued={} context={}",
                    actionId,
                    ToString(contract),
                    *scancode,
                    describePhase(phase),
                    remaining,
                    queued,
                    ToString(context));
            }

            return true;
        }

        auto [it, inserted] = _activeActionScancodes.try_emplace(std::string(actionId));
        auto& action = it->second;
        if (inserted) {
            action.scancode = *scancode;
            action.contract = contract;
        } else {
            action.scancode = *scancode;
            action.contract = contract;
        }

        if (pressed) {
            const bool pressEdge = !action.sourceDown;
            action.sourceDown = true;

            if (contract == ActionOutputContract::Toggle) {
                if (pressEdge) {
                    QueueScheduledPulseLocked(action);
                }
            } else if (contract == ActionOutputContract::Repeat) {
                if (pressEdge) {
                    QueueScheduledPulseLocked(action);
                    action.nextRepeatAtHeldSeconds = kRepeatInitialDelaySeconds;
                } else {
                    while ((heldSeconds + kRepeatScheduleEpsilon) >= action.nextRepeatAtHeldSeconds) {
                        QueueScheduledPulseLocked(action);
                        action.nextRepeatAtHeldSeconds += kRepeatIntervalSeconds;
                    }
                }
            }

            if (IsDebugLoggingEnabled()) {
                logger::info(
                    "[DualPad][KeyboardNative] scheduled contract action={} contract={} scancode=0x{:02X} sourceDown={} phase={} remaining={} queued={} held={:.3f} nextRepeatAt={:.3f} context={}",
                    actionId,
                    ToString(contract),
                    action.scancode,
                    action.sourceDown,
                    describePhase(action.scheduledPulsePhase),
                    action.scheduledPhaseRemainingConsumes,
                    action.queuedScheduledPulses,
                    heldSeconds,
                    action.nextRepeatAtHeldSeconds,
                    ToString(context));
            }

            return true;
        }
        return true;
    }

    void KeyboardNativeBackend::QueueScheduledPulseLocked(ActiveKeyboardAction& action)
    {
        if (!UsesScheduledPulseContract(action.contract)) {
            return;
        }

        if (action.scheduledPulsePhase == ScheduledPulsePhase::Idle) {
            action.scheduledPulsePhase = ScheduledPulsePhase::Down;
            action.scheduledPhaseRemainingConsumes = GetScheduledPulseDownConsumes(action.contract);
            return;
        }

        if (action.queuedScheduledPulses != std::numeric_limits<std::uint8_t>::max()) {
            ++action.queuedScheduledPulses;
        }
    }

    void KeyboardNativeBackend::AppendScheduledPulseStateLocked(DesiredKeyboardState& desiredState)
    {
        for (const auto& [actionId, action] : _activeActionScancodes) {
            static_cast<void>(actionId);
            if (!UsesScheduledPulseContract(action.contract) ||
                action.scheduledPulsePhase != ScheduledPulsePhase::Down) {
                continue;
            }

            auto& pulseCount = desiredState.pendingPulseCounts[action.scancode];
            if (pulseCount != std::numeric_limits<std::uint8_t>::max()) {
                ++pulseCount;
            }
        }
    }

    void KeyboardNativeBackend::AdvanceScheduledPulseActionsLocked()
    {
        for (auto it = _activeActionScancodes.begin(); it != _activeActionScancodes.end();) {
            auto& action = it->second;
            if (!UsesScheduledPulseContract(action.contract)) {
                ++it;
                continue;
            }

            if (action.scheduledPulsePhase != ScheduledPulsePhase::Idle &&
                action.scheduledPhaseRemainingConsumes != 0) {
                --action.scheduledPhaseRemainingConsumes;
            }

            if (action.scheduledPulsePhase != ScheduledPulsePhase::Idle &&
                action.scheduledPhaseRemainingConsumes == 0) {
                switch (action.scheduledPulsePhase) {
                case ScheduledPulsePhase::Down:
                    if (action.queuedScheduledPulses != 0) {
                        const auto gapConsumes = GetScheduledPulseGapConsumes(action.contract);
                        if (gapConsumes != 0) {
                            action.scheduledPulsePhase = ScheduledPulsePhase::Gap;
                            action.scheduledPhaseRemainingConsumes = gapConsumes;
                        } else {
                            --action.queuedScheduledPulses;
                            action.scheduledPulsePhase = ScheduledPulsePhase::Down;
                            action.scheduledPhaseRemainingConsumes = GetScheduledPulseDownConsumes(action.contract);
                        }
                    } else {
                        action.scheduledPulsePhase = ScheduledPulsePhase::Idle;
                    }
                    break;
                case ScheduledPulsePhase::Gap:
                    if (action.queuedScheduledPulses != 0) {
                        --action.queuedScheduledPulses;
                        action.scheduledPulsePhase = ScheduledPulsePhase::Down;
                        action.scheduledPhaseRemainingConsumes = GetScheduledPulseDownConsumes(action.contract);
                    } else {
                        action.scheduledPulsePhase = ScheduledPulsePhase::Idle;
                    }
                    break;
                case ScheduledPulsePhase::Idle:
                default:
                    break;
                }
            }

            if (!action.sourceDown &&
                action.scheduledPulsePhase == ScheduledPulsePhase::Idle &&
                action.queuedScheduledPulses == 0) {
                it = _activeActionScancodes.erase(it);
                continue;
            }

            ++it;
        }
    }

    bool KeyboardNativeBackend::IsDebugLoggingEnabled() const
    {
        return RuntimeConfig::GetSingleton().LogKeyboardInjection();
    }
}
