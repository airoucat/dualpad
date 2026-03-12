#include "pch.h"
#include "input/injection/OrbisGamepadProbeHook.h"

#include <Windows.h>
#include <SKSE/Version.h>

#include <array>
#include <cstring>
#include <string>

#include "input/RuntimeConfig.h"

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        constexpr auto kSupportedRuntime = SKSE::RUNTIME_SSE_1_5_97;
        constexpr std::uintptr_t kExpectedOrbisPollRva = 0xC1BD20;
        constexpr std::ptrdiff_t kExpectedOrbisProviderWindowOffset = 0xA2;
        constexpr std::ptrdiff_t kExpectedOrbisProviderCallOffset = 0xB3;
        constexpr std::array<std::uint8_t, 32> kExpectedOrbisProviderWindow = {
            0x8B, 0x8F, 0xC8, 0x00, 0x00, 0x00, 0x85, 0xC9,
            0x0F, 0x8E, 0x62, 0x02, 0x00, 0x00, 0x48, 0x8B,
            0xD3, 0xE8, 0x28, 0xF8, 0xFF, 0xFF, 0x85, 0xC0,
            0x0F, 0x85, 0x52, 0x02, 0x00, 0x00, 0x0F, 0xB6
        };

        constexpr std::size_t kLoggedPrefixBytes = 16;

        using ProviderRead_t = std::uint32_t(__fastcall*)(std::uint32_t providerIndex, void* currentState);

        std::uint32_t ReadU32(const std::uint8_t* bytes, std::size_t offset)
        {
            std::uint32_t value = 0;
            std::memcpy(&value, bytes + offset, sizeof(value));
            return value;
        }

        std::string FormatHexPrefix(const std::uint8_t* bytes, std::size_t count)
        {
            static constexpr char kHex[] = "0123456789ABCDEF";

            std::string result;
            result.reserve(count == 0 ? 0 : (count * 3) - 1);
            for (std::size_t i = 0; i < count; ++i) {
                if (i != 0) {
                    result.push_back(' ');
                }

                const auto value = bytes[i];
                result.push_back(kHex[(value >> 4) & 0xF]);
                result.push_back(kHex[value & 0xF]);
            }

            return result;
        }

        struct OrbisProviderCallHook
        {
            static std::uint32_t __fastcall Thunk(std::uint32_t providerIndex, void* currentState)
            {
                const auto original = reinterpret_cast<ProviderRead_t>(_originalTarget);
                if (!original) {
                    return 0x8000FFFFu;
                }

                const auto result = original(providerIndex, currentState);
                OrbisGamepadProbeHook::GetSingleton().NoteProviderCallActivity();

                if (!currentState) {
                    static std::uint32_t nullLogBudget = 4;
                    if (nullLogBudget > 0) {
                        --nullLogBudget;
                        logger::warn(
                            "[DualPad][OrbisProbe] provider={} returned status=0x{:08X} with null state pointer",
                            providerIndex,
                            result);
                    }
                    return result;
                }

                static std::uint32_t hitCount = 0;
                const auto hitIndex = ++hitCount;
                const bool verbose = RuntimeConfig::GetSingleton().LogNativeInjection();
                const bool shouldLog =
                    hitIndex <= 16 ||
                    (verbose && (hitIndex % 240u) == 0) ||
                    (result != 0 && hitIndex <= 32);

                if (!shouldLog) {
                    return result;
                }

                const auto* bytes = static_cast<const std::uint8_t*>(currentState);
                const auto buttons = ReadU32(bytes, 0x00);
                const auto prefix = FormatHexPrefix(bytes, kLoggedPrefixBytes);

                // These field names are inferred from the SE 1.5.97 Orbis poll path.
                logger::info(
                    "[DualPad][OrbisProbe] hit={} provider={} status=0x{:08X} state=0x{:X} buttons=0x{:08X} lx={} ly={} rx={} ry={} lt={} rt={} touchCount={} active={} prefix={}",
                    hitIndex,
                    providerIndex,
                    result,
                    reinterpret_cast<std::uintptr_t>(currentState),
                    buttons,
                    bytes[0x04],
                    bytes[0x05],
                    bytes[0x06],
                    bytes[0x07],
                    bytes[0x08],
                    bytes[0x09],
                    bytes[0x34],
                    bytes[0x4C],
                    prefix);

                return result;
            }

            static inline std::uintptr_t _originalTarget{ 0 };
        };
    }

    OrbisGamepadProbeHook& OrbisGamepadProbeHook::GetSingleton()
    {
        static OrbisGamepadProbeHook instance;
        return instance;
    }

    void OrbisGamepadProbeHook::Install()
    {
        if (_installed || _attemptedInstall) {
            return;
        }
        _attemptedInstall = true;

        const auto& config = RuntimeConfig::GetSingleton();
        if (!config.UseOrbisGamepadProbeHook()) {
            return;
        }

        if (REL::Module::get().version() != kSupportedRuntime) {
            if (!_loggedUnsupportedRuntime) {
                logger::error(
                    "[DualPad][OrbisProbe] Unsupported runtime {}; probe only supports Skyrim SE 1.5.97",
                    REL::Module::get().version().string());
                _loggedUnsupportedRuntime = true;
            }
            return;
        }

        const auto pollAddress = REL::Module::get().base() + kExpectedOrbisPollRva;
        const auto windowAddress = pollAddress + kExpectedOrbisProviderWindowOffset;
        if (!REL::verify_code(windowAddress, kExpectedOrbisProviderWindow)) {
            logger::error(
                "[DualPad][OrbisProbe] Orbis provider call-site verification failed at poll=0x{:X}; probe not installed",
                pollAddress);
            return;
        }

        const auto callAddress = pollAddress + kExpectedOrbisProviderCallOffset;
        OrbisProviderCallHook::_originalTarget = SKSE::GetTrampoline().write_call<5>(
            callAddress,
            OrbisProviderCallHook::Thunk);
        _installed = OrbisProviderCallHook::_originalTarget != 0;

        if (!_installed) {
            logger::error(
                "[DualPad][OrbisProbe] Failed to patch Orbis provider call at 0x{:X}",
                callAddress);
            return;
        }

        logger::info(
            "[DualPad][OrbisProbe] Installed observe-only Orbis provider probe poll=0x{:X} callSite=0x{:X} originalTarget=0x{:X}",
            pollAddress,
            callAddress,
            OrbisProviderCallHook::_originalTarget);
    }

    bool OrbisGamepadProbeHook::IsInstalled() const
    {
        return _installed;
    }

    void OrbisGamepadProbeHook::NoteProviderCallActivity()
    {
        _lastProviderCallTickMs.store(GetTickCount64(), std::memory_order_relaxed);
    }

    bool OrbisGamepadProbeHook::HasRecentProviderCallActivity(std::uint64_t maxAgeMs) const
    {
        const auto lastTick = _lastProviderCallTickMs.load(std::memory_order_relaxed);
        if (lastTick == 0) {
            return false;
        }

        const auto now = GetTickCount64();
        return now >= lastTick && (now - lastTick) <= maxAgeMs;
    }
}
