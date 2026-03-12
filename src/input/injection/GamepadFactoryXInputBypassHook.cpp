#include "pch.h"
#include "input/injection/GamepadFactoryXInputBypassHook.h"

#include <Windows.h>
#include <SKSE/Version.h>
#include <Xinput.h>

#include <array>
#include <cstdint>
#include <cstring>

#include "input/RuntimeConfig.h"

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        constexpr auto kSupportedRuntime = SKSE::RUNTIME_SSE_1_5_97;
        constexpr std::uintptr_t kExpectedFactoryRva = 0xC199F0;
        constexpr std::ptrdiff_t kExpectedXInputWindowOffset = 0x21;
        constexpr std::ptrdiff_t kExpectedXInputCallOffset = 0x2D;
        constexpr std::array<std::uint8_t, 24> kExpectedXInputWindow = {
            0x4C, 0x8D, 0x44, 0x24, 0x28, 0xBA, 0x01, 0x00,
            0x00, 0x00, 0x8B, 0xCB, 0xE8, 0xD8, 0x10, 0x00,
            0x00, 0x85, 0xC0, 0x0F, 0x84, 0xAD, 0x00, 0x00
        };

        using XInputGetCapabilities_t =
            std::uint32_t(WINAPI*)(std::uint32_t userIndex, std::uint32_t flags, XINPUT_CAPABILITIES* capabilities);

        struct FactoryXInputCallHook
        {
            static std::uint32_t WINAPI Thunk(
                std::uint32_t userIndex,
                std::uint32_t flags,
                XINPUT_CAPABILITIES* capabilities)
            {
                if (capabilities) {
                    std::memset(capabilities, 0, sizeof(XINPUT_CAPABILITIES));
                }

                static std::uint32_t hitCount = 0;
                const auto hitIndex = ++hitCount;
                const bool verbose = RuntimeConfig::GetSingleton().LogNativeInjection();
                const bool shouldLog = hitIndex <= 8 || verbose;
                if (shouldLog) {
                    logger::info(
                        "[DualPad][FactoryXInputBypass] hit={} userIndex={} flags=0x{:X} capabilities=0x{:X} forcedResult=0x{:08X}",
                        hitIndex,
                        userIndex,
                        flags,
                        reinterpret_cast<std::uintptr_t>(capabilities),
                        ERROR_DEVICE_NOT_CONNECTED);
                }

                // This hook is installed only on sub_140C199F0's XInputGetCapabilities
                // call-site so we can test whether the factory falls back from the
                // Win32/XInput family into the Orbis/provider branch.
                return ERROR_DEVICE_NOT_CONNECTED;
            }

            static inline std::uintptr_t _originalTarget{ 0 };
        };
    }

    GamepadFactoryXInputBypassHook& GamepadFactoryXInputBypassHook::GetSingleton()
    {
        static GamepadFactoryXInputBypassHook instance;
        return instance;
    }

    void GamepadFactoryXInputBypassHook::Install()
    {
        if (_installed || _attemptedInstall) {
            return;
        }
        _attemptedInstall = true;

        const auto& config = RuntimeConfig::GetSingleton();
        if (!config.ForceFactoryXInputCapabilitiesFail()) {
            return;
        }

        if (REL::Module::get().version() != kSupportedRuntime) {
            if (!_loggedUnsupportedRuntime) {
                logger::error(
                    "[DualPad][FactoryXInputBypass] Unsupported runtime {}; experiment only supports Skyrim SE 1.5.97",
                    REL::Module::get().version().string());
                _loggedUnsupportedRuntime = true;
            }
            return;
        }

        const auto factoryAddress = REL::Module::get().base() + kExpectedFactoryRva;
        const auto windowAddress = factoryAddress + kExpectedXInputWindowOffset;
        if (!REL::verify_code(windowAddress, kExpectedXInputWindow)) {
            logger::error(
                "[DualPad][FactoryXInputBypass] Factory XInput call-site verification failed at factory=0x{:X}; experiment not installed",
                factoryAddress);
            return;
        }

        const auto callAddress = factoryAddress + kExpectedXInputCallOffset;
        FactoryXInputCallHook::_originalTarget = SKSE::GetTrampoline().write_call<5>(
            callAddress,
            FactoryXInputCallHook::Thunk);
        _installed = FactoryXInputCallHook::_originalTarget != 0;

        if (!_installed) {
            logger::error(
                "[DualPad][FactoryXInputBypass] Failed to patch factory XInputGetCapabilities call-site at 0x{:X}",
                callAddress);
            return;
        }

        logger::warn(
            "[DualPad][FactoryXInputBypass] Installed factory-only XInputGetCapabilities bypass factory=0x{:X} callSite=0x{:X} originalTarget=0x{:X}",
            factoryAddress,
            callAddress,
            FactoryXInputCallHook::_originalTarget);
    }

    bool GamepadFactoryXInputBypassHook::IsInstalled() const
    {
        return _installed;
    }
}
