#include "pch.h"
#include "input/injection/JumpCoexistencePreprocessHook.h"

#include "input/RuntimeConfig.h"

#include <SKSE/Version.h>
#include <Windows.h>

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        using ControlMapPreprocess_t = void(RE::ControlMap*, RE::InputEvent*);

        constexpr auto kSupportedRuntime = SKSE::RUNTIME_SSE_1_5_97;
        constexpr std::ptrdiff_t kPreprocessCallOffset = 0x53;
        constexpr std::ptrdiff_t kQueueEventField08Offset = 0x08;
        constexpr std::ptrdiff_t kQueueEventDescriptorOffset = 0x18;
        constexpr std::ptrdiff_t kQueueEventCodeOffset = 0x20;
        constexpr std::array<std::uint8_t, 5> kExpectedPreprocessCallInstruction = {
            0xE8, 0xF8, 0xC4, 0xFF, 0xFF
        };

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

        bool TryReadQword(std::uintptr_t address, std::uint64_t& value)
        {
            MEMORY_BASIC_INFORMATION info{};
            if (::VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) == 0) {
                return false;
            }
            if (info.State != MEM_COMMIT || !IsReadableProtection(info.Protect)) {
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

            value = *reinterpret_cast<const std::uint32_t*>(address);
            return true;
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

            return preview.empty() ? "empty" : preview;
        }

        RE::ControlMap::InputContextID ResolveCurrentContext(RE::ControlMap& controlMap)
        {
            const auto& runtime = controlMap.GetRuntimeData();
            if (!runtime.contextPriorityStack.empty()) {
                return runtime.contextPriorityStack.back();
            }

            return RE::ControlMap::InputContextID::kGameplay;
        }

        bool NodeMapsToJump(RE::ControlMap& controlMap, std::uintptr_t nodeAddress)
        {
            std::uint32_t code = 0;
            if (!TryReadDword(nodeAddress + kQueueEventCodeOffset, code)) {
                return false;
            }

            if (code == 0x39) {
                return true;
            }

            std::uint64_t descriptor = 0;
            if (TryReadQword(nodeAddress + kQueueEventDescriptorOffset, descriptor) &&
                descriptor != 0 &&
                DescribeStringPointer(static_cast<std::uintptr_t>(descriptor)) == "Jump") {
                return true;
            }

            std::uint32_t deviceOrId = 0;
            (void)TryReadDword(nodeAddress + kQueueEventField08Offset, deviceOrId);
            const auto context = ResolveCurrentContext(controlMap);
            const auto mapped = controlMap.GetUserEventName(
                static_cast<std::uint16_t>(code),
                RE::INPUT_DEVICE::kGamepad,
                context);
            return mapped == "Jump";
        }

        bool HeadContainsJumpCandidate(RE::ControlMap& controlMap, RE::InputEvent* head)
        {
            auto* current = head;
            for (std::size_t index = 0; current != nullptr && index < 6; ++index, current = current->next) {
                if (NodeMapsToJump(controlMap, reinterpret_cast<std::uintptr_t>(current))) {
                    return true;
                }
            }

            return false;
        }

        struct PreprocessCallHook
        {
            static inline REL::Relocation<ControlMapPreprocess_t> _original{};

            static void Thunk(RE::ControlMap* controlMap, RE::InputEvent* head)
            {
                std::optional<std::uint8_t> savedGate121{};
                const bool patchJumpCoexistence =
                    controlMap != nullptr &&
                    head != nullptr &&
                    HeadContainsJumpCandidate(*controlMap, head);

                if (patchJumpCoexistence) {
                    auto* gateObj = reinterpret_cast<std::uint8_t*>(controlMap);
                    auto& gate121 = gateObj[0x121];
                    if (gate121 != 0) {
                        savedGate121 = gate121;
                        gate121 = 0;

                        if (RuntimeConfig::GetSingleton().LogNativeInjection()) {
                            logger::info(
                                "[DualPad][JumpCoexistence] Scoped preprocess gate override gateObj=0x{:X} +0x121 {}->0",
                                reinterpret_cast<std::uintptr_t>(gateObj),
                                *savedGate121);
                        }
                    }
                }

                _original(controlMap, head);

                if (savedGate121) {
                    auto* gateObj = reinterpret_cast<std::uint8_t*>(controlMap);
                    gateObj[0x121] = *savedGate121;

                    if (RuntimeConfig::GetSingleton().LogNativeInjection()) {
                        logger::info(
                            "[DualPad][JumpCoexistence] Scoped preprocess gate restore gateObj=0x{:X} +0x121 restored={}",
                            reinterpret_cast<std::uintptr_t>(gateObj),
                            *savedGate121);
                    }
                }
            }
        };
    }

    JumpCoexistencePreprocessHook& JumpCoexistencePreprocessHook::GetSingleton()
    {
        static JumpCoexistencePreprocessHook instance;
        return instance;
    }

    void JumpCoexistencePreprocessHook::Install()
    {
        if (_installed || _attemptedInstall) {
            return;
        }
        _attemptedInstall = true;

        if (RuntimeConfig::GetSingleton().UseUpstreamKeyboardHook()) {
            return;
        }

        if (REL::Module::get().version() != kSupportedRuntime) {
            return;
        }

        REL::Relocation<std::uintptr_t> inputLoopTarget{ RELOCATION_ID(67315, 68617) };
        const auto preprocessCallAddress = inputLoopTarget.address() + kPreprocessCallOffset;
        if (!REL::verify_code(preprocessCallAddress, kExpectedPreprocessCallInstruction)) {
            logger::warn(
                "[DualPad][JumpCoexistence] sub_140C11600 coexistence gate call-site check failed at 0x{:X}",
                preprocessCallAddress);
            return;
        }

        PreprocessCallHook::_original = SKSE::GetTrampoline().write_call<5>(
            preprocessCallAddress,
            PreprocessCallHook::Thunk);
        _installed = PreprocessCallHook::_original.address() != 0;

        if (_installed) {
            logger::info(
                "[DualPad][JumpCoexistence] Installed shared sub_140C11600 coexistence gate hook callSite=0x{:X}",
                preprocessCallAddress);
        } else {
            logger::warn(
                "[DualPad][JumpCoexistence] Failed to install shared sub_140C11600 coexistence gate hook callSite=0x{:X}",
                preprocessCallAddress);
        }
    }

    bool JumpCoexistencePreprocessHook::IsInstalled() const
    {
        return _installed;
    }
}
