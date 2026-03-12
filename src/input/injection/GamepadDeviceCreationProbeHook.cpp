#include "pch.h"
#include "input/injection/GamepadDeviceCreationProbeHook.h"

#include <SKSE/Version.h>

#include <array>
#include <cstdint>

#include "input/RuntimeConfig.h"

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        constexpr auto kSupportedRuntime = SKSE::RUNTIME_SSE_1_5_97;

        constexpr std::uintptr_t kFactoryRva = 0xC199F0;
        constexpr std::uintptr_t kFactoryViaJumpRva = 0xC1989F;
        constexpr std::uintptr_t kFactoryViaCallRva = 0xC198D1;
        constexpr std::uintptr_t kWin32GamepadVtableRva = 0x175E920;
        constexpr std::uintptr_t kOrbisGamepadVtableRva = 0x175E9A0;

        constexpr std::array<std::uint8_t, 8> kExpectedFactoryViaJumpWindow = {
            0xE9, 0x4C, 0x01, 0x00, 0x00, 0x48, 0x83, 0xC4
        };

        constexpr std::array<std::uint8_t, 8> kExpectedFactoryViaCallWindow = {
            0xE8, 0x1A, 0x01, 0x00, 0x00, 0x48, 0x8B, 0x4B
        };

        using GamepadFactory_t = void**(__fastcall*)(std::uint64_t* owner);

        enum class GamepadDeviceFamily
        {
            None = 0,
            Win32,
            Orbis,
            Unknown
        };

        const char* ToString(GamepadDeviceFamily family)
        {
            switch (family) {
            case GamepadDeviceFamily::Win32:
                return "BSWin32GamepadDevice";
            case GamepadDeviceFamily::Orbis:
                return "BSPCOrbisGamepadDevice";
            case GamepadDeviceFamily::Unknown:
                return "Unknown";
            case GamepadDeviceFamily::None:
            default:
                return "None";
            }
        }

        GamepadDeviceFamily ClassifyDevice(const void* device)
        {
            if (!device) {
                return GamepadDeviceFamily::None;
            }

            const auto* moduleBase = reinterpret_cast<const std::uint8_t*>(REL::Module::get().base());
            const auto* const* vtbl = *reinterpret_cast<const void* const* const*>(device);
            if (!vtbl) {
                return GamepadDeviceFamily::Unknown;
            }

            const auto win32Vtable = moduleBase + kWin32GamepadVtableRva;
            const auto orbisVtable = moduleBase + kOrbisGamepadVtableRva;
            if (vtbl == reinterpret_cast<const void* const*>(win32Vtable)) {
                return GamepadDeviceFamily::Win32;
            }
            if (vtbl == reinterpret_cast<const void* const*>(orbisVtable)) {
                return GamepadDeviceFamily::Orbis;
            }

            return GamepadDeviceFamily::Unknown;
        }

        void LogFactoryResult(std::string_view route, std::uint64_t* owner, void** result)
        {
            static std::uint32_t hitCount = 0;
            const auto hitIndex = ++hitCount;
            const auto* device = owner ? reinterpret_cast<void*>(owner[1]) : nullptr;
            const auto family = ClassifyDevice(device);
            const auto userIndex = device ? *reinterpret_cast<const std::int32_t*>(reinterpret_cast<const std::uint8_t*>(device) + 0xC8) : -1;
            const auto enabled = device ? *(reinterpret_cast<const std::uint8_t*>(device) + 0xCC) : 0;
            const auto vtable = device ? *reinterpret_cast<const std::uintptr_t*>(device) : 0;

            logger::info(
                "[DualPad][GamepadFactoryProbe] hit={} route={} owner=0x{:X} result=0x{:X} device=0x{:X} family={} vtable=0x{:X} userIndex={} enabled={}",
                hitIndex,
                route,
                reinterpret_cast<std::uintptr_t>(owner),
                reinterpret_cast<std::uintptr_t>(result),
                reinterpret_cast<std::uintptr_t>(device),
                ToString(family),
                vtable,
                userIndex,
                enabled);
        }

        struct FactoryJumpHook
        {
            static void** __fastcall Thunk(std::uint64_t* owner)
            {
                const auto original = reinterpret_cast<GamepadFactory_t>(_originalTarget);
                const auto result = original ? original(owner) : nullptr;
                LogFactoryResult("init-jump", owner, result);
                return result;
            }

            static inline std::uintptr_t _originalTarget{ 0 };
        };

        struct FactoryCallHook
        {
            static void** __fastcall Thunk(std::uint64_t* owner)
            {
                const auto original = reinterpret_cast<GamepadFactory_t>(_originalTarget);
                const auto result = original ? original(owner) : nullptr;
                LogFactoryResult("lazy-call", owner, result);
                return result;
            }

            static inline std::uintptr_t _originalTarget{ 0 };
        };
    }

    GamepadDeviceCreationProbeHook& GamepadDeviceCreationProbeHook::GetSingleton()
    {
        static GamepadDeviceCreationProbeHook instance;
        return instance;
    }

    void GamepadDeviceCreationProbeHook::Install()
    {
        if (_installed || _attemptedInstall) {
            return;
        }
        _attemptedInstall = true;

        const auto& config = RuntimeConfig::GetSingleton();
        if (!config.UseGamepadDeviceCreationProbeHook()) {
            return;
        }

        if (REL::Module::get().version() != kSupportedRuntime) {
            if (!_loggedUnsupportedRuntime) {
                logger::error(
                    "[DualPad][GamepadFactoryProbe] Unsupported runtime {}; probe only supports Skyrim SE 1.5.97",
                    REL::Module::get().version().string());
                _loggedUnsupportedRuntime = true;
            }
            return;
        }

        const auto base = REL::Module::get().base();
        const auto jumpAddress = base + kFactoryViaJumpRva;
        const auto callAddress = base + kFactoryViaCallRva;

        if (!REL::verify_code(jumpAddress, kExpectedFactoryViaJumpWindow)) {
            logger::error(
                "[DualPad][GamepadFactoryProbe] Factory jump-site verification failed at 0x{:X}; probe not installed",
                jumpAddress);
            return;
        }

        if (!REL::verify_code(callAddress, kExpectedFactoryViaCallWindow)) {
            logger::error(
                "[DualPad][GamepadFactoryProbe] Factory call-site verification failed at 0x{:X}; probe not installed",
                callAddress);
            return;
        }

        FactoryJumpHook::_originalTarget = SKSE::GetTrampoline().write_branch<5>(
            jumpAddress,
            FactoryJumpHook::Thunk);
        FactoryCallHook::_originalTarget = SKSE::GetTrampoline().write_call<5>(
            callAddress,
            FactoryCallHook::Thunk);

        _installed =
            FactoryJumpHook::_originalTarget == (base + kFactoryRva) &&
            FactoryCallHook::_originalTarget == (base + kFactoryRva);

        if (!_installed) {
            logger::error(
                "[DualPad][GamepadFactoryProbe] Failed to install factory probe jumpOriginal=0x{:X} callOriginal=0x{:X} expectedFactory=0x{:X}",
                FactoryJumpHook::_originalTarget,
                FactoryCallHook::_originalTarget,
                base + kFactoryRva);
            return;
        }

        logger::info(
            "[DualPad][GamepadFactoryProbe] Installed observe-only factory probe jumpSite=0x{:X} callSite=0x{:X} factory=0x{:X}",
            jumpAddress,
            callAddress,
            base + kFactoryRva);
    }

    bool GamepadDeviceCreationProbeHook::IsInstalled() const
    {
        return _installed;
    }
}
