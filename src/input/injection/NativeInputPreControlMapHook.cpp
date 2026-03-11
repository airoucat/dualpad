#include "pch.h"
#include "input/injection/NativeInputPreControlMapHook.h"

#include <SKSE/Version.h>

#include "input/RuntimeConfig.h"
#include "input/injection/PadEventSnapshotDispatcher.h"
#include "input/injection/PadEventSnapshotProcessor.h"
#include "input/injection/SeInputEventQueueAccess.h"

#include <array>
#include <string_view>

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        using ControlMapConsume_t = void(RE::ControlMap*, RE::InputEvent*);
        using NativeButtonHookMode = dualpad::input::NativeButtonHookMode;

        constexpr auto kSupportedRuntime = SKSE::RUNTIME_SSE_1_5_97;
        constexpr std::ptrdiff_t kPollCallControlMapOffset = 0x53;
        constexpr std::array<std::uint8_t, 5> kExpectedCallInstruction = {
            0xE8, 0xF8, 0xC4, 0xFF, 0xFF
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
            case NativeButtonHookMode::DropProbe:
            default:
                return "drop";
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
                        mode == NativeButtonHookMode::HeadPrepend ? "Prepended onto head arg" :
                        "Prepended",
                        submittedCount);
                }

                _original(controlMap, combinedHead);
            }

            static inline REL::Relocation<ControlMapConsume_t> _original;
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
}
