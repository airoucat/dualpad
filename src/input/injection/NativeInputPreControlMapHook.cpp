#include "pch.h"
#include "input/injection/NativeInputPreControlMapHook.h"

#include <SKSE/Version.h>

#include "input/RuntimeConfig.h"
#include "input/injection/PadEventSnapshotDispatcher.h"
#include "input/injection/PadEventSnapshotProcessor.h"
#include "input/injection/SeInputEventQueueAccess.h"

#include <array>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        using ControlMapConsume_t = void(RE::ControlMap*, RE::InputEvent*);
        using InputDispatch_t = void(void*, RE::InputEvent**);
        using GameplayValidate_t = bool(__fastcall*)(void*, RE::InputEvent*);
        using GameplayRootAllowGate_t = bool(__fastcall*)(void*);
        using GameplayQueryBool0_t = bool(__fastcall*)();
        using GameplayQueryBool1_t = bool(__fastcall*)(void*);
        using GameplayQueryBool2_t = bool(__fastcall*)(void*, void*);
        using GameplayQueryBool2State_t = bool(__fastcall*)(void*, std::uint32_t*);
        using GameplayRootProcess_t = RE::BSEventNotifyControl(__fastcall*)(
            void*,
            RE::InputEvent* const*,
            RE::BSTEventSource<RE::InputEvent*>*);
        using NativeButtonHookMode = dualpad::input::NativeButtonHookMode;

        constexpr auto kSupportedRuntime = SKSE::RUNTIME_SSE_1_5_97;
        constexpr std::ptrdiff_t kPollCallControlMapOffset = 0x53;
        constexpr std::ptrdiff_t kDispatchCallOffset = 0x7B;
        constexpr std::uintptr_t kGameplayRootProcessRva = 0x704DE0;
        constexpr std::uintptr_t kGameplayRootAllowGateRva = 0x706AF0;
        constexpr std::ptrdiff_t kGameplayRootAllowGateCallOffset = 0x2E;
        constexpr std::ptrdiff_t kGameplayRootAllowGateCallIsBlockedOffset = 0x2B;
        constexpr std::ptrdiff_t kGameplayRootAllowGateCallInputBusyOffset = 0x99;
        constexpr std::ptrdiff_t kGameplayRootAllowGateCallInputAllowedOffset = 0xA9;
        constexpr std::ptrdiff_t kGameplayRootAllowGateCallInputSuppressedOffset = 0xB9;
        constexpr std::ptrdiff_t kGameplayRootAllowGateCallStateMatchOffset = 0xD8;
        constexpr std::ptrdiff_t kGameplayRootAllowGateCallUiBlockedOffset = 0xF4;
        constexpr std::uintptr_t kGameplayRootGlobalObjectPointerRva = 0x2EC5BD8;
        constexpr std::uint32_t kJumpGamepadId = 0x8000;
        constexpr std::size_t kJumpHandlerObjectSlotIndex = 53;
        constexpr std::size_t kShadowVtableEntryCount = 32;
        constexpr std::array<std::uint8_t, 5> kExpectedCallInstruction = {
            0xE8, 0xF8, 0xC4, 0xFF, 0xFF
        };
        constexpr std::array<std::uint8_t, 5> kExpectedDispatchCallInstruction = {
            0xE8, 0xD0, 0x0C, 0x00, 0x00
        };
        constexpr std::array<std::uint8_t, 5> kExpectedGameplayRootAllowGateCallInstruction = {
            0xE8, 0xDD, 0x1C, 0x00, 0x00
        };
        constexpr std::uintptr_t kGateHashCrcTableRva = 0x175BF90;
        constexpr std::array<std::uint8_t, 5> kExpectedGateCallIsBlockedInstruction = {
            0xE8, 0x30, 0x76, 0x7B, 0x00
        };
        constexpr std::array<std::uint8_t, 5> kExpectedGateCallInputBusyInstruction = {
            0xE8, 0x72, 0x8B, 0xFA, 0xFF
        };
        constexpr std::array<std::uint8_t, 5> kExpectedGateCallInputAllowedInstruction = {
            0xE8, 0x22, 0xEC, 0xEF, 0xFF
        };
        constexpr std::array<std::uint8_t, 5> kExpectedGateCallInputSuppressedInstruction = {
            0xE8, 0xE2, 0x8A, 0xFA, 0xFF
        };
        constexpr std::array<std::uint8_t, 5> kExpectedGateCallStateMatchInstruction = {
            0xE8, 0x13, 0x6D, 0xDE, 0xFF
        };
        constexpr std::array<std::uint8_t, 5> kExpectedGateCallUiBlockedInstruction = {
            0xE8, 0xD7, 0xDD, 0xEC, 0xFF
        };

        struct JumpRootInvocationContext
        {
            bool active{ false };
            std::uintptr_t self{ 0 };
            RE::InputEvent* head{ nullptr };
        };

        thread_local JumpRootInvocationContext g_jumpRootInvocationContext{};

        struct ScopedJumpRootInvocationContext
        {
            ScopedJumpRootInvocationContext(void* self, RE::InputEvent* const* head, bool active) :
                _previous(g_jumpRootInvocationContext)
            {
                if (active) {
                    g_jumpRootInvocationContext.active = true;
                    g_jumpRootInvocationContext.self = reinterpret_cast<std::uintptr_t>(self);
                    g_jumpRootInvocationContext.head = head ? *head : nullptr;
                }
            }

            ~ScopedJumpRootInvocationContext()
            {
                g_jumpRootInvocationContext = _previous;
            }

        private:
            JumpRootInvocationContext _previous{};
        };

        const char* ToModeString(NativeButtonHookMode mode)
        {
            switch (mode) {
            case NativeButtonHookMode::Inject:
                return "inject";
            case NativeButtonHookMode::HeadPrepend:
                return "head-prepend";
            case NativeButtonHookMode::AppendProbe:
                return "append-probe";
            case NativeButtonHookMode::Append:
                return "append";
            case NativeButtonHookMode::EngineCache:
                return "engine-cache";
            case NativeButtonHookMode::DropProbe:
            default:
                return "drop";
            }
        }

        bool TryReadQword(std::uintptr_t address, std::uint64_t& value)
        {
            __try {
                value = *reinterpret_cast<const std::uint64_t*>(address);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                value = 0;
                return false;
            }
        }

        bool TryReadByte(std::uintptr_t address, std::uint8_t& value)
        {
            __try {
                value = *reinterpret_cast<const std::uint8_t*>(address);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                value = 0;
                return false;
            }
        }

        bool TryReadDword(std::uintptr_t address, std::uint32_t& value)
        {
            __try {
                value = *reinterpret_cast<const std::uint32_t*>(address);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                value = 0;
                return false;
            }
        }

        std::uintptr_t ReadGlobalObjectPointer(std::uintptr_t slotRva)
        {
            const auto slotAddress = REL::Module::get().base() + slotRva;
            std::uint64_t value = 0;
            return TryReadQword(slotAddress, value) ? static_cast<std::uintptr_t>(value) : 0;
        }

        const char* DescribeInputContext(RE::ControlMap::InputContextID context)
        {
            using ContextID = RE::ControlMap::InputContextID;

            switch (context) {
            case ContextID::kGameplay:
                return "Gameplay";
            case ContextID::kMenuMode:
                return "Menu";
            case ContextID::kConsole:
                return "Console";
            case ContextID::kItemMenu:
                return "ItemMenu";
            case ContextID::kInventory:
                return "Inventory";
            case ContextID::kFavorites:
                return "Favorites";
            case ContextID::kMap:
                return "Map";
            case ContextID::kStats:
                return "Stats";
            case ContextID::kCursor:
                return "Cursor";
            case ContextID::kBook:
                return "Book";
            case ContextID::kDebugText:
                return "DebugText";
            case ContextID::kJournal:
                return "Journal";
            case ContextID::kTFCMode:
                return "TFC";
            case ContextID::kMapDebug:
                return "MapDebug";
            case ContextID::kLockpicking:
                return "Lockpicking";
            case ContextID::kFavor:
                return "Favor";
            case ContextID::kDebugOverlay:
                return "DebugOverlay";
            default:
                return "Other";
            }
        }

        std::string_view DescribeQueueRegion(std::uintptr_t queueBase, std::uintptr_t address)
        {
            if (!queueBase || !address || address < queueBase) {
                return "external";
            }

            const auto offset = address - queueBase;
            if (offset >= 0x20 && offset < 0x200) {
                return "buttonCache";
            }
            if (offset >= 0x200 && offset < 0x2A0) {
                return "charCache";
            }
            if (offset >= 0x2A0 && offset < 0x2D0) {
                return "mouseCache";
            }
            if (offset >= 0x2D0 && offset < 0x330) {
                return "thumbstickCache";
            }
            if (offset >= 0x330 && offset < 0x350) {
                return "connectCache";
            }
            if (offset >= 0x350 && offset < 0x380) {
                return "kinectCache";
            }
            if (offset == 0x380) {
                return "queueHeadField";
            }
            if (offset == 0x388) {
                return "queueTailField";
            }
            return "external";
        }

        void LogInputHeadSample(RE::InputEvent* head, RE::BSInputEventQueue* queue)
        {
            if (!head || !queue) {
                return;
            }

            static std::uint32_t sampleLogs = 0;
            if (sampleLogs >= 4) {
                return;
            }
            ++sampleLogs;

            const auto queueBase = reinterpret_cast<std::uintptr_t>(queue);
            auto* node = head;
            for (std::size_t index = 0; node && index < 4; ++index) {
                const auto address = reinterpret_cast<std::uintptr_t>(node);
                const auto offset = address >= queueBase ? address - queueBase : 0;
                const auto region = DescribeQueueRegion(queueBase, address);

                std::uint32_t idCode = 0;
                const char* userEvent = "";
                if (auto* idEvent = node->AsIDEvent(); idEvent) {
                    idCode = idEvent->idCode;
                    userEvent = idEvent->userEvent.empty() ? "" : idEvent->userEvent.c_str();
                }

                logger::info(
                    "[DualPad][NativePreControlMap] AppendProbe node#{} ptr=0x{:X} off=0x{:X} region={} type={} device={} next=0x{:X} id=0x{:X} event={}",
                    index + 1,
                    address,
                    offset,
                    region,
                    static_cast<std::uint32_t>(node->GetEventType()),
                    static_cast<std::uint32_t>(node->GetDevice()),
                    reinterpret_cast<std::uintptr_t>(node->next),
                    idCode,
                    userEvent);
                node = node->next;
            }
        }

        void LogJumpDispatchEnvironment(RE::ControlMap* controlMap)
        {
            auto* inputManager = RE::BSInputDeviceManager::GetSingleton();
            auto* gamepadHandler = inputManager ? inputManager->GetGamepadHandler() : nullptr;
            const auto delegate = gamepadHandler ?
                gamepadHandler->GetRuntimeData().currentPCGamePadDelegate :
                nullptr;

            bool gamepadEnabled = false;
            bool gamepadConnected = false;
            bool handlerEnabled = false;
            if (inputManager) {
                gamepadEnabled = inputManager->IsGamepadEnabled();
                gamepadConnected = inputManager->IsGamepadConnected();
            }
            if (gamepadHandler) {
                handlerEnabled = gamepadHandler->IsEnabled();
            }

            const auto* map = controlMap ? controlMap : RE::ControlMap::GetSingleton();
            auto context = RE::ControlMap::InputContextID::kGameplay;
            bool ignoreKeyboardMouse = false;
            bool jumpEnabled = false;
            bool movementEnabled = false;
            bool menuEnabled = false;
            bool fightingEnabled = false;
            std::uint32_t gamepadMapType = 0;
            std::string_view mappedJump = {};

            if (map) {
                const auto& runtime = map->GetRuntimeData();
                if (!runtime.contextPriorityStack.empty()) {
                    context = runtime.contextPriorityStack.back();
                }

                ignoreKeyboardMouse = runtime.ignoreKeyboardMouse;
                jumpEnabled = map->IsJumpingControlsEnabled();
                movementEnabled = map->IsMovementControlsEnabled();
                menuEnabled = map->IsMenuControlsEnabled();
                fightingEnabled = map->IsFightingControlsEnabled();
                gamepadMapType = static_cast<std::uint32_t>(map->GetGamePadType());
                mappedJump = map->GetUserEventName(
                    kJumpGamepadId,
                    RE::INPUT_DEVICE::kGamepad,
                    context);
            }

            logger::info(
                "[DualPad][NativePreControlMap] JumpDispatchState context={}({}) mappedJump={} gpEnabled={} gpConnected={} handlerEnabled={} delegate=0x{:X} ignoreKM={} jumpEnabled={} movementEnabled={} menuEnabled={} fightingEnabled={} gpMapType={}",
                DescribeInputContext(context),
                static_cast<std::uint32_t>(context),
                mappedJump,
                gamepadEnabled,
                gamepadConnected,
                handlerEnabled,
                reinterpret_cast<std::uintptr_t>(delegate),
                ignoreKeyboardMouse,
                jumpEnabled,
                movementEnabled,
                menuEnabled,
                fightingEnabled,
                gamepadMapType);
        }

        void LogJumpDispatchSample(RE::ControlMap* controlMap, RE::InputEvent* head)
        {
            if (!head) {
                return;
            }

            static std::uint32_t jumpSamples = 0;
            if (jumpSamples >= 8) {
                return;
            }

            bool foundJump = false;
            auto* node = head;
            for (std::size_t index = 0; node && index < 6; ++index) {
                const auto* idEvent = node->AsIDEvent();
                if (!idEvent || idEvent->idCode != kJumpGamepadId) {
                    node = node->next;
                    continue;
                }

                foundJump = true;
                const auto rawField18 = *reinterpret_cast<const std::uintptr_t*>(
                    reinterpret_cast<const std::byte*>(node) + 0x18);
                float value = 0.0f;
                float held = 0.0f;
                if (const auto* buttonEvent = node->AsButtonEvent(); buttonEvent) {
                    value = buttonEvent->Value();
                    held = buttonEvent->HeldDuration();
                }

                logger::info(
                    "[DualPad][NativePreControlMap] JumpDispatch node#{} ptr=0x{:X} type={} device={} next=0x{:X} id=0x{:04X} event={} rawField18=0x{:X} value={:.3f} held={:.3f}",
                    index + 1,
                    reinterpret_cast<std::uintptr_t>(node),
                    static_cast<std::uint32_t>(node->GetEventType()),
                    static_cast<std::uint32_t>(node->GetDevice()),
                    reinterpret_cast<std::uintptr_t>(node->next),
                    idEvent->idCode,
                    idEvent->userEvent.empty() ? "" : idEvent->userEvent.c_str(),
                    rawField18,
                    value,
                    held);
                node = node->next;
            }

            if (foundJump) {
                LogJumpDispatchEnvironment(controlMap);
                ++jumpSamples;
            }
        }

        bool HeadContainsJumpEvent(RE::InputEvent* head)
        {
            auto* node = head;
            for (std::size_t index = 0; node && index < 8; ++index) {
                const auto* idEvent = node->AsIDEvent();
                if (idEvent && idEvent->idCode == kJumpGamepadId) {
                    return true;
                }
                node = node->next;
            }

            return false;
        }

        RE::InputEvent* FindJumpEvent(RE::InputEvent* head)
        {
            auto* node = head;
            for (std::size_t index = 0; node && index < 8; ++index) {
                const auto* idEvent = node->AsIDEvent();
                if (idEvent && idEvent->idCode == kJumpGamepadId) {
                    return node;
                }
                node = node->next;
            }

            return nullptr;
        }

        bool ComputeGateHash(std::uint64_t key, std::uint32_t& hash)
        {
            const auto tableBase = REL::Module::get().base() + kGateHashCrcTableRva;
            auto readTable = [&](std::uint8_t index, std::uint32_t& value) {
                return TryReadDword(tableBase + static_cast<std::uintptr_t>(index) * sizeof(std::uint32_t), value);
            };

            std::uint32_t crc0 = 0;
            if (!readTable(static_cast<std::uint8_t>(key), crc0)) {
                hash = 0;
                return false;
            }

            std::uint32_t crc1 = 0;
            if (!readTable(static_cast<std::uint8_t>(crc0 ^ static_cast<std::uint8_t>(key >> 8)), crc1)) {
                hash = 0;
                return false;
            }
            std::uint32_t v6 = (crc0 >> 8) ^ crc1;

            std::uint32_t crc2 = 0;
            if (!readTable(static_cast<std::uint8_t>(v6 ^ static_cast<std::uint8_t>(key >> 16)), crc2)) {
                hash = 0;
                return false;
            }
            std::uint32_t v9 = (v6 >> 8) ^ crc2;

            std::uint32_t crc3 = 0;
            if (!readTable(static_cast<std::uint8_t>(v9 ^ static_cast<std::uint8_t>(key >> 24)), crc3)) {
                hash = 0;
                return false;
            }
            std::uint32_t v12 = (v9 >> 8) ^ crc3;

            std::uint32_t crc4 = 0;
            if (!readTable(static_cast<std::uint8_t>(v12 ^ static_cast<std::uint8_t>(key >> 32)), crc4)) {
                hash = 0;
                return false;
            }
            std::uint32_t v15 = (v12 >> 8) ^ crc4;

            std::uint32_t crc5 = 0;
            if (!readTable(static_cast<std::uint8_t>(v15 ^ static_cast<std::uint8_t>(key >> 40)), crc5)) {
                hash = 0;
                return false;
            }
            std::uint32_t v16 = (v15 >> 8) ^ crc5;

            std::uint32_t crc6 = 0;
            if (!readTable(static_cast<std::uint8_t>(v16 ^ static_cast<std::uint8_t>(key >> 48)), crc6)) {
                hash = 0;
                return false;
            }
            std::uint32_t v17 = (v16 >> 8) ^ crc6;

            std::uint32_t crc7 = 0;
            if (!readTable(static_cast<std::uint8_t>(v17 ^ static_cast<std::uint8_t>(key >> 56)), crc7)) {
                hash = 0;
                return false;
            }

            hash = (v17 >> 8) ^ crc7;
            return true;
        }

        void LogJumpFamilyDispatchSample(std::string_view phase, void* owner, RE::InputEvent* head)
        {
            if (!head || !RuntimeConfig::GetSingleton().LogNativeInjection()) {
                return;
            }

            static std::uint32_t familySamples = 0;
            if (familySamples >= 12) {
                return;
            }

            auto* node = head;
            for (std::size_t index = 0; node && index < 6; ++index) {
                const auto* idEvent = node->AsIDEvent();
                if (!idEvent || idEvent->idCode != kJumpGamepadId) {
                    node = node->next;
                    continue;
                }

                const auto rawField18 = *reinterpret_cast<const std::uintptr_t*>(
                    reinterpret_cast<const std::byte*>(node) + 0x18);
                float value = 0.0f;
                float held = 0.0f;
                if (const auto* buttonEvent = node->AsButtonEvent(); buttonEvent) {
                    value = buttonEvent->Value();
                    held = buttonEvent->HeldDuration();
                }

                logger::info(
                    "[DualPad][NativeDispatch] JumpFamily {} owner=0x{:X} node#{} ptr=0x{:X} next=0x{:X} id=0x{:04X} event={} rawField18=0x{:X} value={:.3f} held={:.3f}",
                    phase,
                    reinterpret_cast<std::uintptr_t>(owner),
                    index + 1,
                    reinterpret_cast<std::uintptr_t>(node),
                    reinterpret_cast<std::uintptr_t>(node->next),
                    idEvent->idCode,
                    idEvent->userEvent.empty() ? "" : idEvent->userEvent.c_str(),
                    rawField18,
                    value,
                    held);
                ++familySamples;
                break;
            }
        }

        void LogGameplayRootProcessSample(
            std::string_view phase,
            void* self,
            RE::InputEvent* const* head,
            RE::BSTEventSource<RE::InputEvent*>* source,
            RE::BSEventNotifyControl result,
            bool hasResult)
        {
            if (!head || !*head || !RuntimeConfig::GetSingleton().LogNativeInjection()) {
                return;
            }

            static std::uint32_t rootSamples = 0;
            if (rootSamples >= 12) {
                return;
            }

            auto* node = *head;
            for (std::size_t index = 0; node && index < 6; ++index) {
                const auto* idEvent = node->AsIDEvent();
                if (!idEvent || idEvent->idCode != kJumpGamepadId) {
                    node = node->next;
                    continue;
                }

                std::uint8_t rootFlag80 = 0;
                const auto rootFlagReadable =
                    TryReadByte(reinterpret_cast<std::uintptr_t>(self) + 80, rootFlag80);
                const auto rawField18 = *reinterpret_cast<const std::uintptr_t*>(
                    reinterpret_cast<const std::byte*>(node) + 0x18);
                float value = 0.0f;
                float held = 0.0f;
                if (const auto* buttonEvent = node->AsButtonEvent(); buttonEvent) {
                    value = buttonEvent->Value();
                    held = buttonEvent->HeldDuration();
                }

                logger::info(
                    "[DualPad][NativeDispatch] JumpRoot {} self=0x{:X} headPtr=0x{:X} head=0x{:X} source=0x{:X} root+80={}{} node#{} id=0x{:04X} event={} field18=0x{:X} value={:.3f} held={:.3f} result={}{}",
                    phase,
                    reinterpret_cast<std::uintptr_t>(self),
                    reinterpret_cast<std::uintptr_t>(head),
                    reinterpret_cast<std::uintptr_t>(*head),
                    reinterpret_cast<std::uintptr_t>(source),
                    rootFlagReadable ? static_cast<std::uint32_t>(rootFlag80) : 0u,
                    rootFlagReadable ? "" : "(unreadable)",
                    index + 1,
                    idEvent->idCode,
                    idEvent->userEvent.empty() ? "" : idEvent->userEvent.c_str(),
                    rawField18,
                    value,
                    held,
                    hasResult ? static_cast<std::uint32_t>(result) : 0u,
                    hasResult ? "" : "(n/a)");
                ++rootSamples;
                break;
            }
        }

        void LogGameplayRootGateSample(void* self, bool result)
        {
            if (!RuntimeConfig::GetSingleton().LogNativeInjection()) {
                return;
            }

            static std::uint32_t gateSamples = 0;
            if (gateSamples >= 12) {
                return;
            }

            const auto& context = g_jumpRootInvocationContext;
            if (!context.active || context.self != reinterpret_cast<std::uintptr_t>(self) || !context.head) {
                return;
            }

            auto* node = FindJumpEvent(context.head);
            if (!node) {
                return;
            }

            std::uint8_t rootFlag80 = 0;
            const auto rootFlagReadable =
                TryReadByte(reinterpret_cast<std::uintptr_t>(self) + 80, rootFlag80);
            const auto* idEvent = node->AsIDEvent();
            const auto rawField18 = *reinterpret_cast<const std::uintptr_t*>(
                reinterpret_cast<const std::byte*>(node) + 0x18);
            float value = 0.0f;
            float held = 0.0f;
            if (const auto* buttonEvent = node->AsButtonEvent(); buttonEvent) {
                value = buttonEvent->Value();
                held = buttonEvent->HeldDuration();
            }

            constexpr std::uintptr_t kGateManager1PtrRva = 0x1EBEB20;
            constexpr std::uintptr_t kGateManager2PtrRva = 0x2F26EF8;
            constexpr std::uintptr_t kGateManager3PtrRva = 0x2EC5C60;
            constexpr std::uintptr_t kGateManager4PtrRva = 0x2EFF990;
            constexpr std::uintptr_t kGateStateDwordRva = 0x2F26EF4;
            constexpr std::uintptr_t kGateInlineTableRva = 0x1EC47C0;

            const auto gateManager1 = ReadGlobalObjectPointer(kGateManager1PtrRva);
            const auto gateManager2 = ReadGlobalObjectPointer(kGateManager2PtrRva);
            const auto gateManager3 = ReadGlobalObjectPointer(kGateManager3PtrRva);
            const auto gateManager4 = ReadGlobalObjectPointer(kGateManager4PtrRva);

            std::uint32_t gateManager1Field352 = 0;
            const auto gateManager1Field352Readable =
                gateManager1 != 0 && TryReadDword(gateManager1 + 352, gateManager1Field352);

            std::uint32_t gateManager2Field192 = 0;
            const auto gateManager2Field192Readable =
                gateManager2 != 0 && TryReadDword(gateManager2 + 192, gateManager2Field192);

            std::uint32_t gateManager2Field488 = 0;
            const auto gateManager2Field488Readable =
                gateManager2 != 0 && TryReadDword(gateManager2 + 488, gateManager2Field488);

            const auto gateInlineIndex = gateManager2Field488Readable ?
                (gateManager2Field488 & 0xFFFFF) :
                0u;
            const auto gateInlineEntryAddress =
                REL::Module::get().base() + kGateInlineTableRva + static_cast<std::uintptr_t>(gateInlineIndex) * 16u;
            std::uint32_t gateInlineFlags = 0;
            const auto gateInlineFlagsReadable =
                TryReadDword(gateInlineEntryAddress, gateInlineFlags);

            std::uint64_t gateInlineLink = 0;
            const auto gateInlineLinkReadable =
                TryReadQword(gateInlineEntryAddress + 8, gateInlineLink);

            std::uint32_t gateInlineLinkField8 = 0;
            const auto gateInlineLinkField8Readable =
                gateInlineLinkReadable && gateInlineLink != 0 &&
                TryReadDword(static_cast<std::uintptr_t>(gateInlineLink) + 8, gateInlineLinkField8);

            std::uint32_t gateManager3Field32 = 0;
            const auto gateManager3Field32Readable =
                gateManager3 != 0 && TryReadDword(gateManager3 + 32, gateManager3Field32);

            std::uint32_t gateStateDword = 0;
            const auto gateStateDwordReadable =
                TryReadDword(REL::Module::get().base() + kGateStateDwordRva, gateStateDword);

            logger::info(
                "[DualPad][NativeDispatch] JumpRootGate self=0x{:X} head=0x{:X} node=0x{:X} id=0x{:04X} event={} field18=0x{:X} value={:.3f} held={:.3f} root+80={}{} result={}",
                reinterpret_cast<std::uintptr_t>(self),
                reinterpret_cast<std::uintptr_t>(context.head),
                reinterpret_cast<std::uintptr_t>(node),
                idEvent ? idEvent->idCode : 0u,
                idEvent && !idEvent->userEvent.empty() ? idEvent->userEvent.c_str() : "",
                rawField18,
                value,
                held,
                rootFlagReadable ? static_cast<std::uint32_t>(rootFlag80) : 0u,
                rootFlagReadable ? "" : "(unreadable)",
                result);
            logger::info(
                "[DualPad][NativeDispatch] JumpRootGateState mgr1=0x{:X} f352={}{} mgr2=0x{:X} f192=0x{:X}{} f488=0x{:X}{} mgr3=0x{:X} f32={}{} mgr4=0x{:X} stateDword=0x{:X}{}",
                gateManager1,
                gateManager1Field352Readable ? gateManager1Field352 : 0u,
                gateManager1Field352Readable ? "" : "(unreadable)",
                gateManager2,
                gateManager2Field192Readable ? gateManager2Field192 : 0u,
                gateManager2Field192Readable ? "" : "(unreadable)",
                gateManager2Field488Readable ? gateManager2Field488 : 0u,
                gateManager2Field488Readable ? "" : "(unreadable)",
                gateManager3,
                gateManager3Field32Readable ? gateManager3Field32 : 0u,
                gateManager3Field32Readable ? "" : "(unreadable)",
                gateManager4,
                gateStateDwordReadable ? gateStateDword : 0u,
                gateStateDwordReadable ? "" : "(unreadable)");
            logger::info(
                "[DualPad][NativeDispatch] JumpRootGateInline idx=0x{:X} entry=0x{:X} flags=0x{:X}{} bit1A={} maskDiff=0x{:X} link=0x{:X}{} linkField8=0x{:X}{} linkIndex=0x{:X}",
                gateInlineIndex,
                gateInlineEntryAddress,
                gateInlineFlagsReadable ? gateInlineFlags : 0u,
                gateInlineFlagsReadable ? "" : "(unreadable)",
                gateInlineFlagsReadable ? ((gateInlineFlags & 0x4000000u) != 0u) : false,
                gateInlineFlagsReadable && gateManager2Field488Readable ?
                    ((gateInlineFlags ^ gateManager2Field488) & 0x3F00000u) :
                    0u,
                gateInlineLinkReadable ? gateInlineLink : 0u,
                gateInlineLinkReadable ? "" : "(unreadable)",
                gateInlineLinkField8Readable ? gateInlineLinkField8 : 0u,
                gateInlineLinkField8Readable ? "" : "(unreadable)",
                gateInlineLinkField8Readable ? (gateInlineLinkField8 >> 11) : 0u);
            ++gateSamples;
        }

        void LogGameplayRootGateStep(
            std::string_view helper,
            bool result,
            std::uintptr_t rcx,
            std::uintptr_t rdx = 0,
            std::uint32_t stateValue = 0,
            bool hasStateValue = false)
        {
            if (!RuntimeConfig::GetSingleton().LogNativeInjection()) {
                return;
            }

            static std::uint32_t helperSamples = 0;
            if (helperSamples >= 48) {
                return;
            }

            const auto& context = g_jumpRootInvocationContext;
            if (!context.active || !context.head) {
                return;
            }

            logger::info(
                "[DualPad][NativeDispatch] JumpRootGateStep helper={} self=0x{:X} head=0x{:X} rcx=0x{:X} rdx=0x{:X} state=0x{:X}{} result={}",
                helper,
                context.self,
                reinterpret_cast<std::uintptr_t>(context.head),
                rcx,
                rdx,
                hasStateValue ? stateValue : 0u,
                hasStateValue ? "" : "(n/a)",
                result);
            ++helperSamples;
        }

        void LogGameplayRootGateHashMatch(std::uintptr_t manager, std::uintptr_t keyPointer, bool helperResult)
        {
            if (!RuntimeConfig::GetSingleton().LogNativeInjection()) {
                return;
            }

            static std::uint32_t matchSamples = 0;
            if (matchSamples >= 16) {
                return;
            }

            const auto& context = g_jumpRootInvocationContext;
            if (!context.active || !context.head || manager == 0 || keyPointer == 0) {
                return;
            }

            std::uint64_t key = 0;
            if (!TryReadQword(keyPointer, key)) {
                return;
            }

            std::uint64_t bucketsBase = 0;
            std::uint64_t sentinel = 0;
            std::uint32_t bucketCount = 0;
            if (!TryReadQword(manager + 336, bucketsBase) ||
                !TryReadQword(manager + 320, sentinel) ||
                !TryReadDword(manager + 308, bucketCount) ||
                bucketsBase == 0 || bucketCount == 0) {
                return;
            }

            std::uint32_t hash = 0;
            if (!ComputeGateHash(key, hash)) {
                return;
            }

            const auto bucketMask = bucketCount - 1;
            auto nodeAddress = static_cast<std::uintptr_t>(bucketsBase) +
                static_cast<std::uintptr_t>(32) * static_cast<std::uintptr_t>(hash & bucketMask);

            std::uint64_t nextNode = 0;
            if (!TryReadQword(nodeAddress + 24, nextNode)) {
                return;
            }

            std::uintptr_t matchedNode = 0;
            while (nextNode != 0) {
                std::uint64_t nodeKey = 0;
                if (!TryReadQword(nodeAddress, nodeKey)) {
                    break;
                }

                if (nodeKey == key) {
                    matchedNode = nodeAddress;
                    break;
                }

                if (nodeAddress == static_cast<std::uintptr_t>(sentinel)) {
                    break;
                }

                nodeAddress = static_cast<std::uintptr_t>(nextNode);
                if (!TryReadQword(nodeAddress + 24, nextNode)) {
                    break;
                }
            }

            std::uint64_t stateObject = 0;
            std::uint8_t stateFlags = 0;
            std::uint64_t stateVtable = 0;
            bool stateFlagsReadable = false;
            bool stateVtableReadable = false;
            if (matchedNode != 0 && TryReadQword(matchedNode + 8, stateObject) && stateObject != 0) {
                stateFlagsReadable = TryReadByte(static_cast<std::uintptr_t>(stateObject) + 0x1C, stateFlags);
                stateVtableReadable = TryReadQword(static_cast<std::uintptr_t>(stateObject), stateVtable);
            }

            logger::info(
                "[DualPad][NativeDispatch] JumpRootGateHash key=0x{:X} hash=0x{:X} manager=0x{:X} bucketCount=0x{:X} bucket=0x{:X} matchedNode=0x{:X} sentinel=0x{:X} stateObj=0x{:X} flags=0x{:X}{} vtbl=0x{:X}{} helperResult={}",
                key,
                hash,
                manager,
                bucketCount,
                static_cast<std::uintptr_t>(bucketsBase) +
                    static_cast<std::uintptr_t>(32) * static_cast<std::uintptr_t>(hash & bucketMask),
                matchedNode,
                static_cast<std::uintptr_t>(sentinel),
                static_cast<std::uintptr_t>(stateObject),
                stateFlagsReadable ? static_cast<std::uint32_t>(stateFlags) : 0u,
                stateFlagsReadable ? "" : "(unreadable)",
                stateVtableReadable ? static_cast<std::uintptr_t>(stateVtable) : 0u,
                stateVtableReadable ? "" : "(unreadable)",
                helperResult);
            ++matchSamples;
        }

        struct PollControlMapCallHook
        {
            static void Thunk(RE::ControlMap* controlMap, RE::InputEvent* head)
            {
                auto& config = RuntimeConfig::GetSingleton();
                if (!config.UseNativeButtonInjector()) {
                    _original(controlMap, head);
                    return;
                }

                PadEventSnapshotDispatcher::GetSingleton().DrainOnMainThread();

                RE::InputEvent* combinedHead = head;
                std::size_t submittedCount = 0;
                auto& processor = PadEventSnapshotProcessor::GetSingleton();
                const auto mode = config.GetNativeButtonHookMode();

                if (mode == NativeButtonHookMode::DropProbe) {
                    submittedCount = processor.GetPendingInjectedButtonCount();
                    if (submittedCount != 0) {
                        processor.DiscardPendingInjectedButtonEvents();
                    }
                }
                else if (mode == NativeButtonHookMode::AppendProbe) {
                    submittedCount = processor.GetPendingInjectedButtonCount();
                    if (submittedCount != 0) {
                        if (auto* queue = RE::BSInputEventQueue::GetSingleton(); queue) {
                            logger::info(
                                "[DualPad][NativePreControlMap] AppendProbe pending={} head=0x{:X} queueHead=0x{:X} queueTail=0x{:X} buttonCount={}",
                                submittedCount,
                                reinterpret_cast<std::uintptr_t>(combinedHead),
                                reinterpret_cast<std::uintptr_t>(detail::GetSEQueueHead(queue)),
                                reinterpret_cast<std::uintptr_t>(detail::GetSEQueueTail(queue)),
                                queue->buttonEventCount);
                            LogInputHeadSample(combinedHead, queue);
                        }
                        processor.DiscardPendingInjectedButtonEvents();
                    }
                }
                else if (mode == NativeButtonHookMode::Append) {
                    submittedCount = processor.FlushInjectedInputQueue();
                    if (submittedCount != 0 && !combinedHead) {
                        if (auto* queue = RE::BSInputEventQueue::GetSingleton(); queue) {
                            combinedHead = detail::GetSEQueueHead(queue);
                        }
                    }
                    else if (submittedCount != 0 && config.LogNativeInjection()) {
                        if (auto* queue = RE::BSInputEventQueue::GetSingleton(); queue) {
                            logger::info(
                                "[DualPad][NativePreControlMap] Append kept original head=0x{:X} queueHead=0x{:X}",
                                reinterpret_cast<std::uintptr_t>(combinedHead),
                                reinterpret_cast<std::uintptr_t>(detail::GetSEQueueHead(queue)));
                        }
                    }
                }
                else if (mode == NativeButtonHookMode::EngineCache) {
                    submittedCount = processor.AppendInjectedInputEventsUsingEngineCache(combinedHead);
                }
                else if (mode == NativeButtonHookMode::HeadPrepend) {
                    RE::InputEvent* combinedTail = nullptr;
                    submittedCount = processor.PrependInjectedInputQueueEvents(
                        combinedHead,
                        combinedTail);
                }
                else {
                    RE::InputEvent* combinedTail = nullptr;
                    submittedCount = processor.PrependInjectedInputQueueEvents(
                        combinedHead,
                        combinedTail);
                }

                if (config.LogNativeInjection()) {
                    static std::uint32_t loggedCalls = 0;
                    if (loggedCalls < 3 || submittedCount != 0) {
                        logger::info(
                            "[DualPad][NativePreControlMap] CallHook#{} controlMap=0x{:X} head=0x{:X} submitted={} mode={}",
                            loggedCalls < 3 ? loggedCalls + 1 : loggedCalls,
                            reinterpret_cast<std::uintptr_t>(controlMap),
                            reinterpret_cast<std::uintptr_t>(combinedHead),
                            submittedCount,
                            ToModeString(mode));
                        if (loggedCalls < 3) {
                            ++loggedCalls;
                        }
                    }
                }

                if (submittedCount != 0 && config.LogNativeInjection()) {
                    logger::info(
                        "[DualPad][NativePreControlMap] {} {} staged button events at Poll call-site before ControlMap",
                        mode == NativeButtonHookMode::DropProbe ? "Dropped" :
                        mode == NativeButtonHookMode::AppendProbe ? "Probed and dropped" :
                        mode == NativeButtonHookMode::Append ? "Appended" :
                        mode == NativeButtonHookMode::EngineCache ? "Appended through engine cache" :
                        mode == NativeButtonHookMode::HeadPrepend ? "Prepended onto head arg" :
                        "Prepended",
                        submittedCount);
                }

                if (config.LogNativeInjection()) {
                    LogJumpDispatchSample(controlMap, combinedHead);
                }

                _original(controlMap, combinedHead);
            }

            static inline REL::Relocation<ControlMapConsume_t> _original;
        };

        struct DispatchCallHook
        {
            static void Thunk(void* owner, RE::InputEvent** head)
            {
                if (RuntimeConfig::GetSingleton().UseNativeButtonInjector() && head && *head) {
                    LogJumpFamilyDispatchSample("ENTRY", owner, *head);
                }

                _original(owner, head);

                if (RuntimeConfig::GetSingleton().UseNativeButtonInjector() && head && *head) {
                    LogJumpFamilyDispatchSample("EXIT", owner, *head);
                }
            }

            static inline REL::Relocation<InputDispatch_t> _original;
            static inline bool _installed{ false };
        };

        struct JumpValidateHook
        {
            static bool Install()
            {
                if (_installed) {
                    return true;
                }

                const auto gameplayRoot = ReadGlobalObjectPointer(kGameplayRootGlobalObjectPointerRva);
                if (gameplayRoot == 0) {
                    return false;
                }

                std::uint64_t handlerObject = 0;
                if (!TryReadQword(
                        gameplayRoot + kJumpHandlerObjectSlotIndex * sizeof(std::uint64_t),
                        handlerObject) ||
                    handlerObject == 0) {
                    return false;
                }

                std::uint64_t originalVtable = 0;
                if (!TryReadQword(static_cast<std::uintptr_t>(handlerObject), originalVtable) ||
                    originalVtable == 0) {
                    return false;
                }

                for (std::size_t i = 0; i < _shadowVtable.size(); ++i) {
                    std::uint64_t entry = 0;
                    if (!TryReadQword(static_cast<std::uintptr_t>(originalVtable) + i * sizeof(std::uint64_t), entry)) {
                        return false;
                    }
                    _shadowVtable[i] = static_cast<std::uintptr_t>(entry);
                }

                _handlerObject = static_cast<std::uintptr_t>(handlerObject);
                _original = reinterpret_cast<GameplayValidate_t>(_shadowVtable[1]);
                _shadowVtable[1] = reinterpret_cast<std::uintptr_t>(&Thunk);
                *reinterpret_cast<std::uintptr_t*>(_handlerObject) = reinterpret_cast<std::uintptr_t>(_shadowVtable.data());
                _installed = true;

                if (RuntimeConfig::GetSingleton().LogNativeInjection()) {
                    logger::info(
                        "[DualPad][NativeDispatch] Jump validate hook installed=true gameplayRoot=0x{:X} handler=0x{:X} original=0x{:X}",
                        gameplayRoot,
                        _handlerObject,
                        reinterpret_cast<std::uintptr_t>(_original));
                }

                return true;
            }

            static bool __fastcall Thunk(void* self, RE::InputEvent* event)
            {
                const auto result = _original ? _original(self, event) : false;
                const auto* idEvent = event ? event->AsIDEvent() : nullptr;
                if (idEvent && idEvent->idCode == kJumpGamepadId) {
                    static std::uint32_t logged = 0;
                    if (logged < 12 && RuntimeConfig::GetSingleton().LogNativeInjection()) {
                        float value = 0.0f;
                        float held = 0.0f;
                        std::uintptr_t field18 = 0;
                        if (const auto* buttonEvent = event->AsButtonEvent(); buttonEvent) {
                            value = buttonEvent->Value();
                            held = buttonEvent->HeldDuration();
                        }
                        field18 = *reinterpret_cast<const std::uintptr_t*>(
                            reinterpret_cast<const std::byte*>(event) + 0x18);

                        logger::info(
                            "[DualPad][NativeDispatch] JumpValidate self=0x{:X} event=0x{:X} id=0x{:04X} userEvent={} field18=0x{:X} value={:.3f} held={:.3f} result={}",
                            reinterpret_cast<std::uintptr_t>(self),
                            reinterpret_cast<std::uintptr_t>(event),
                            idEvent->idCode,
                            idEvent->userEvent.empty() ? "" : idEvent->userEvent.c_str(),
                            field18,
                            value,
                            held,
                            result);
                        ++logged;
                    }
                }

                return result;
            }

            static inline bool _installed{ false };
            static inline std::uintptr_t _handlerObject{ 0 };
            static inline GameplayValidate_t _original{ nullptr };
            static inline std::array<std::uintptr_t, kShadowVtableEntryCount> _shadowVtable{};
        };

        struct SiblingValidateObserver
        {
            struct HookEntry
            {
                std::string_view label{};
                std::size_t slotIndex{ 0 };
                std::uintptr_t handlerObject{ 0 };
                GameplayValidate_t original{ nullptr };
                std::array<std::uintptr_t, kShadowVtableEntryCount> shadowVtable{};
                std::uint32_t logged{ 0 };
            };

            static bool Install()
            {
                if (_installed) {
                    return true;
                }

                const auto gameplayRoot = ReadGlobalObjectPointer(kGameplayRootGlobalObjectPointerRva);
                if (gameplayRoot == 0) {
                    return false;
                }

                // These slot indices are inferred from the confirmed contiguous gameplay-handler order:
                // Movement, Look, Sprint, ReadyWeapon, AutoMove, ToggleRun, Activate, Jump,
                // Shout, AttackBlock, Run, Sneak, TogglePOV.
                static constexpr std::array kObservedHandlers = {
                    std::pair{ std::size_t{ 46 }, "Movement"sv },
                    std::pair{ std::size_t{ 47 }, "Look"sv },
                    std::pair{ std::size_t{ 48 }, "Sprint"sv },
                    std::pair{ std::size_t{ 49 }, "ReadyWeapon"sv },
                    std::pair{ std::size_t{ 50 }, "AutoMove"sv },
                    std::pair{ std::size_t{ 51 }, "ToggleRun"sv },
                    std::pair{ std::size_t{ 52 }, "Activate"sv },
                    std::pair{ std::size_t{ 54 }, "Shout"sv },
                    std::pair{ std::size_t{ 55 }, "AttackBlock"sv },
                    std::pair{ std::size_t{ 56 }, "Run"sv },
                    std::pair{ std::size_t{ 57 }, "Sneak"sv },
                    std::pair{ std::size_t{ 58 }, "TogglePOV"sv }
                };

                bool installedAny = false;
                for (const auto& [slotIndex, label] : kObservedHandlers) {
                    std::uint64_t handlerObject = 0;
                    if (!TryReadQword(
                            gameplayRoot + slotIndex * sizeof(std::uint64_t),
                            handlerObject) ||
                        handlerObject == 0) {
                        continue;
                    }

                    if (InstallForHandler(static_cast<std::uintptr_t>(handlerObject), slotIndex, label)) {
                        installedAny = true;
                    }
                }

                _installed = installedAny;
                return installedAny;
            }

            static bool __fastcall Thunk(void* self, RE::InputEvent* event)
            {
                const auto it = _entriesByObject.find(reinterpret_cast<std::uintptr_t>(self));
                if (it == _entriesByObject.end()) {
                    return false;
                }

                auto* entry = it->second;
                const auto result = entry->original ? entry->original(self, event) : false;
                const auto* idEvent = event ? event->AsIDEvent() : nullptr;
                if (entry && idEvent && idEvent->idCode == kJumpGamepadId &&
                    entry->logged < 16 && RuntimeConfig::GetSingleton().LogNativeInjection()) {
                    float value = 0.0f;
                    float held = 0.0f;
                    std::uintptr_t field18 = 0;
                    if (const auto* buttonEvent = event->AsButtonEvent(); buttonEvent) {
                        value = buttonEvent->Value();
                        held = buttonEvent->HeldDuration();
                    }
                    field18 = *reinterpret_cast<const std::uintptr_t*>(
                        reinterpret_cast<const std::byte*>(event) + 0x18);

                    logger::info(
                        "[DualPad][NativeDispatch] JumpSiblingValidate handler={} slot={} self=0x{:X} event=0x{:X} userEvent={} field18=0x{:X} value={:.3f} held={:.3f} result={}",
                        entry->label,
                        entry->slotIndex,
                        reinterpret_cast<std::uintptr_t>(self),
                        reinterpret_cast<std::uintptr_t>(event),
                        idEvent->userEvent.empty() ? "" : idEvent->userEvent.c_str(),
                        field18,
                        value,
                        held,
                        result);
                    ++entry->logged;
                }

                return result;
            }

            static bool InstallForHandler(
                std::uintptr_t handlerObject,
                std::size_t slotIndex,
                std::string_view label)
            {
                if (_entriesByObject.contains(handlerObject)) {
                    return true;
                }

                std::uint64_t originalVtable = 0;
                if (!TryReadQword(handlerObject, originalVtable) || originalVtable == 0) {
                    return false;
                }

                auto entry = std::make_unique<HookEntry>();
                entry->label = label;
                entry->slotIndex = slotIndex;
                entry->handlerObject = handlerObject;
                for (std::size_t i = 0; i < entry->shadowVtable.size(); ++i) {
                    std::uint64_t value = 0;
                    if (!TryReadQword(static_cast<std::uintptr_t>(originalVtable) + i * sizeof(std::uint64_t), value)) {
                        return false;
                    }
                    entry->shadowVtable[i] = static_cast<std::uintptr_t>(value);
                }

                entry->original = reinterpret_cast<GameplayValidate_t>(entry->shadowVtable[1]);
                entry->shadowVtable[1] = reinterpret_cast<std::uintptr_t>(&Thunk);
                *reinterpret_cast<std::uintptr_t*>(handlerObject) =
                    reinterpret_cast<std::uintptr_t>(entry->shadowVtable.data());

                if (RuntimeConfig::GetSingleton().LogNativeInjection()) {
                    logger::info(
                        "[DualPad][NativeDispatch] Jump sibling validate hook installed=true handler={} slot={} object=0x{:X} original=0x{:X}",
                        label,
                        slotIndex,
                        handlerObject,
                        reinterpret_cast<std::uintptr_t>(entry->original));
                }

                _entriesByObject.emplace(handlerObject, entry.get());
                _entries.push_back(std::move(entry));
                return true;
            }

            static inline bool _installed{ false };
            static inline std::vector<std::unique_ptr<HookEntry>> _entries{};
            static inline std::unordered_map<std::uintptr_t, HookEntry*> _entriesByObject{};
        };

        struct GameplayRootProcessHook
        {
            static bool Install()
            {
                if (_installed) {
                    return true;
                }

                const auto gameplayRoot = ReadGlobalObjectPointer(kGameplayRootGlobalObjectPointerRva);
                if (gameplayRoot == 0) {
                    return false;
                }

                std::uint64_t originalVtable = 0;
                if (!TryReadQword(gameplayRoot, originalVtable) || originalVtable == 0) {
                    return false;
                }

                for (std::size_t i = 0; i < _shadowVtable.size(); ++i) {
                    std::uint64_t entry = 0;
                    if (!TryReadQword(static_cast<std::uintptr_t>(originalVtable) + i * sizeof(std::uint64_t), entry)) {
                        return false;
                    }
                    _shadowVtable[i] = static_cast<std::uintptr_t>(entry);
                }

                _rootObject = gameplayRoot;
                _original = reinterpret_cast<GameplayRootProcess_t>(_shadowVtable[1]);
                _shadowVtable[1] = reinterpret_cast<std::uintptr_t>(&Thunk);
                *reinterpret_cast<std::uintptr_t*>(_rootObject) = reinterpret_cast<std::uintptr_t>(_shadowVtable.data());
                _installed = true;

                if (RuntimeConfig::GetSingleton().LogNativeInjection()) {
                    logger::info(
                        "[DualPad][NativeDispatch] Gameplay root process hook installed=true root=0x{:X} original=0x{:X}",
                        _rootObject,
                        reinterpret_cast<std::uintptr_t>(_original));
                }

                return true;
            }

            static RE::BSEventNotifyControl __fastcall Thunk(
                void* self,
                RE::InputEvent* const* head,
                RE::BSTEventSource<RE::InputEvent*>* source)
            {
                const bool hasJump = head && *head && HeadContainsJumpEvent(*head);
                if (hasJump) {
                    LogGameplayRootProcessSample(
                        "ENTRY",
                        self,
                        head,
                        source,
                        RE::BSEventNotifyControl{},
                        false);
                }

                const ScopedJumpRootInvocationContext contextScope(self, head, hasJump);
                const auto result = _original ?
                    _original(self, head, source) :
                    RE::BSEventNotifyControl::kContinue;

                if (hasJump) {
                    LogGameplayRootProcessSample(
                        "EXIT",
                        self,
                        head,
                        source,
                        result,
                        true);
                }

                return result;
            }

            static inline bool _installed{ false };
            static inline std::uintptr_t _rootObject{ 0 };
            static inline GameplayRootProcess_t _original{ nullptr };
            static inline std::array<std::uintptr_t, kShadowVtableEntryCount> _shadowVtable{};
        };

        struct GameplayRootAllowGateHook
        {
            static bool Install()
            {
                if (_installed) {
                    return true;
                }

                const auto callAddress =
                    REL::Module::get().base() + kGameplayRootProcessRva + kGameplayRootAllowGateCallOffset;
                if (!REL::verify_code(callAddress, kExpectedGameplayRootAllowGateCallInstruction)) {
                    logger::warn(
                        "[DualPad][NativeDispatch] Gameplay root gate call-site check failed at 0x{:X}",
                        callAddress);
                    return false;
                }

                _original = SKSE::GetTrampoline().write_call<5>(
                    callAddress,
                    GameplayRootAllowGateHook::Thunk);
                _installed = _original.address() != 0;

                if (_installed && RuntimeConfig::GetSingleton().LogNativeInjection()) {
                    logger::info(
                        "[DualPad][NativeDispatch] Gameplay root gate hook installed=true callSite=0x{:X}",
                        callAddress);
                }

                return _installed;
            }

            static bool __fastcall Thunk(void* self)
            {
                const auto result = _original.address() != 0 ? _original(self) : false;
                LogGameplayRootGateSample(self, result);
                return result;
            }

            static inline REL::Relocation<GameplayRootAllowGate_t> _original;
            static inline bool _installed{ false };
        };

        struct GameplayRootGateHelperHooks
        {
            struct IsBlockedCallHook
            {
                static bool __fastcall Thunk(void* rcx, void* rdx)
                {
                    const auto result = _original.address() != 0 ? _original(rcx, rdx) : false;
                    LogGameplayRootGateStep(
                        "92E150",
                        result,
                        reinterpret_cast<std::uintptr_t>(rcx),
                        reinterpret_cast<std::uintptr_t>(rdx));
                    LogGameplayRootGateHashMatch(
                        reinterpret_cast<std::uintptr_t>(rcx),
                        reinterpret_cast<std::uintptr_t>(rdx),
                        result);
                    return result;
                }

                static inline REL::Relocation<GameplayQueryBool2_t> _original;
            };

            struct InputBusyCallHook
            {
                static bool __fastcall Thunk(void* rcx)
                {
                    const auto result = _original.address() != 0 ? _original(rcx) : false;
                    LogGameplayRootGateStep("11F700", result, reinterpret_cast<std::uintptr_t>(rcx));
                    return result;
                }

                static inline REL::Relocation<GameplayQueryBool1_t> _original;
            };

            struct InputAllowedCallHook
            {
                static bool __fastcall Thunk(void* rcx)
                {
                    const auto result = _original.address() != 0 ? _original(rcx) : false;
                    LogGameplayRootGateStep("0757C0", result, reinterpret_cast<std::uintptr_t>(rcx));
                    return result;
                }

                static inline REL::Relocation<GameplayQueryBool1_t> _original;
            };

            struct InputSuppressedCallHook
            {
                static bool __fastcall Thunk(void* rcx)
                {
                    const auto result = _original.address() != 0 ? _original(rcx) : false;
                    LogGameplayRootGateStep("11F690", result, reinterpret_cast<std::uintptr_t>(rcx));
                    return result;
                }

                static inline REL::Relocation<GameplayQueryBool1_t> _original;
            };

            struct StateMatchCallHook
            {
                static bool __fastcall Thunk(void* rcx, std::uint32_t* rdx)
                {
                    const auto result = _original.address() != 0 ? _original(rcx, rdx) : false;
                    LogGameplayRootGateStep(
                        "4ED8E0",
                        result,
                        reinterpret_cast<std::uintptr_t>(rcx),
                        reinterpret_cast<std::uintptr_t>(rdx),
                        rdx ? *rdx : 0u,
                        rdx != nullptr);
                    return result;
                }

                static inline REL::Relocation<GameplayQueryBool2State_t> _original;
            };

            struct UiBlockedCallHook
            {
                static bool __fastcall Thunk()
                {
                    const auto result = _original.address() != 0 ? _original() : false;
                    LogGameplayRootGateStep("0449C0", result, 0);
                    return result;
                }

                static inline REL::Relocation<GameplayQueryBool0_t> _original;
            };

            static bool Install()
            {
                if (_installed) {
                    return true;
                }

                bool installedAny = false;
                installedAny |= InstallCallsite(
                    kGameplayRootAllowGateCallIsBlockedOffset,
                    kExpectedGateCallIsBlockedInstruction,
                    IsBlockedCallHook::_original,
                    IsBlockedCallHook::Thunk,
                    "helper 92E150");
                installedAny |= InstallCallsite(
                    kGameplayRootAllowGateCallInputBusyOffset,
                    kExpectedGateCallInputBusyInstruction,
                    InputBusyCallHook::_original,
                    InputBusyCallHook::Thunk,
                    "helper 11F700");
                installedAny |= InstallCallsite(
                    kGameplayRootAllowGateCallInputAllowedOffset,
                    kExpectedGateCallInputAllowedInstruction,
                    InputAllowedCallHook::_original,
                    InputAllowedCallHook::Thunk,
                    "helper 0757C0");
                installedAny |= InstallCallsite(
                    kGameplayRootAllowGateCallInputSuppressedOffset,
                    kExpectedGateCallInputSuppressedInstruction,
                    InputSuppressedCallHook::_original,
                    InputSuppressedCallHook::Thunk,
                    "helper 11F690");
                installedAny |= InstallCallsite(
                    kGameplayRootAllowGateCallStateMatchOffset,
                    kExpectedGateCallStateMatchInstruction,
                    StateMatchCallHook::_original,
                    StateMatchCallHook::Thunk,
                    "helper 4ED8E0");
                installedAny |= InstallCallsite(
                    kGameplayRootAllowGateCallUiBlockedOffset,
                    kExpectedGateCallUiBlockedInstruction,
                    UiBlockedCallHook::_original,
                    UiBlockedCallHook::Thunk,
                    "helper 0449C0");

                _installed = installedAny;
                return installedAny;
            }

        private:
            template <class TOriginal, class TThunk, std::size_t N>
            static bool InstallCallsite(
                std::ptrdiff_t offset,
                const std::array<std::uint8_t, N>& expected,
                REL::Relocation<TOriginal>& original,
                TThunk thunk,
                std::string_view label)
            {
                const auto callAddress = REL::Module::get().base() + kGameplayRootAllowGateRva + offset;
                if (!REL::verify_code(callAddress, expected)) {
                    logger::warn(
                        "[DualPad][NativeDispatch] Gameplay root gate {} call-site check failed at 0x{:X}",
                        label,
                        callAddress);
                    return false;
                }

                original = SKSE::GetTrampoline().write_call<5>(callAddress, thunk);
                const bool installed = original.address() != 0;
                if (installed && RuntimeConfig::GetSingleton().LogNativeInjection()) {
                    logger::info(
                        "[DualPad][NativeDispatch] Gameplay root gate {} hook installed=true callSite=0x{:X}",
                        label,
                        callAddress);
                }
                return installed;
            }

            static inline bool _installed{ false };
        };
    }

    NativeInputPreControlMapHook& NativeInputPreControlMapHook::GetSingleton()
    {
        static NativeInputPreControlMapHook instance;
        return instance;
    }

    void NativeInputPreControlMapHook::Install()
    {
        if (_installed) {
            return;
        }

        if (REL::Module::get().version() != kSupportedRuntime) {
            logger::error(
                "[DualPad][NativePreControlMap] Unsupported runtime {}; hook is only enabled on Skyrim SE 1.5.97",
                REL::Module::get().version().string());
            return;
        }

        REL::Relocation<std::uintptr_t> pollTarget{ RELOCATION_ID(67315, 68617) };
        const auto callAddress = pollTarget.address() + kPollCallControlMapOffset;
        if (!REL::verify_code(callAddress, kExpectedCallInstruction)) {
            logger::error(
                "[DualPad][NativePreControlMap] Poll call instruction mismatch at 0x{:X}; refusing to patch",
                callAddress);
            return;
        }

        PollControlMapCallHook::_original = SKSE::GetTrampoline().write_call<5>(
            callAddress,
            PollControlMapCallHook::Thunk);
        _installed = PollControlMapCallHook::_original.address() != 0;

        const auto dispatchCallAddress = pollTarget.address() + kDispatchCallOffset;
        if (!DispatchCallHook::_installed) {
            if (REL::verify_code(dispatchCallAddress, kExpectedDispatchCallInstruction)) {
                DispatchCallHook::_original = SKSE::GetTrampoline().write_call<5>(
                    dispatchCallAddress,
                    DispatchCallHook::Thunk);
                DispatchCallHook::_installed = DispatchCallHook::_original.address() != 0;

                if (DispatchCallHook::_installed && RuntimeConfig::GetSingleton().LogNativeInjection()) {
                    logger::info(
                        "[DualPad][NativeDispatch] Family dispatch hook installed=true callSite=0x{:X}",
                        dispatchCallAddress);
                }
            } else {
                logger::warn(
                    "[DualPad][NativeDispatch] Family dispatch call-site check failed at 0x{:X}",
                    dispatchCallAddress);
            }
        }

        GameplayRootProcessHook::Install();
        GameplayRootAllowGateHook::Install();
        GameplayRootGateHelperHooks::Install();
        JumpValidateHook::Install();
        SiblingValidateObserver::Install();

        if (_installed && RuntimeConfig::GetSingleton().LogNativeInjection()) {
            if (auto* queue = RE::BSInputEventQueue::GetSingleton(); queue) {
                logger::info(
                    "[DualPad][NativePreControlMap] Using independent input queue singleton=0x{:X} headOff=0x380 tailOff=0x388",
                    reinterpret_cast<std::uintptr_t>(queue));
            }
        }

        logger::info(
            "[DualPad][NativePreControlMap] Poll call patch installed={} callSite=0x{:X} pollRva=0x{:X}+0x{:X} mode={}",
            _installed,
            callAddress,
            pollTarget.address() - REL::Module::get().base(),
            kPollCallControlMapOffset,
            ToModeString(RuntimeConfig::GetSingleton().GetNativeButtonHookMode()));
    }

    bool NativeInputPreControlMapHook::IsInstalled() const
    {
        return _installed;
    }

    bool NativeInputPreControlMapHook::CanInject() const
    {
        const auto& config = RuntimeConfig::GetSingleton();
        return _installed &&
            config.UseNativeButtonInjector() &&
            config.GetNativeButtonHookMode() != NativeButtonHookMode::DropProbe;
    }

    bool NativeInputPreControlMapHook::IsGameplayInputGateOpen() const
    {
        if (REL::Module::get().version() != kSupportedRuntime) {
            return true;
        }

        const auto gameplayRoot = ReadGlobalObjectPointer(kGameplayRootGlobalObjectPointerRva);
        if (gameplayRoot == 0) {
            return true;
        }

        static const REL::Relocation<GameplayRootAllowGate_t> allowGate{
            REL::Module::get().base() + kGameplayRootAllowGateRva
        };
        if (allowGate.address() == 0) {
            return true;
        }

        return allowGate(reinterpret_cast<void*>(gameplayRoot));
    }
}
