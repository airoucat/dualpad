#include "pch.h"
#include "input/injection/UpstreamGamepadHook.h"

#include <Windows.h>
#include <SKSE/Version.h>

#include <array>

#include "input/HidReader.h"
#include "input/InputModalityTracker.h"
#include "input/XInputStateBridge.h"
#include "input/backend/NativeButtonCommitBackend.h"
#include "input/injection/PadEventSnapshotDispatcher.h"

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        constexpr auto kSupportedRuntime = SKSE::RUNTIME_SSE_1_5_97;
        constexpr std::uintptr_t kExpectedPollRva = 0xC1AB40;
        constexpr std::ptrdiff_t kExpectedPollXInputCallOffset = 0x5D;
        constexpr std::ptrdiff_t kExpectedPollXInputWindowOffset = 0x3E;
        constexpr std::size_t kUpstreamDrainBudget = 64;
        constexpr std::array<std::uint8_t, 38> kExpectedPollXInputWindow = {
            0x8B, 0x89, 0xC8, 0x00, 0x00, 0x00, 0x83, 0xF9,
            0xFF, 0x0F, 0x84, 0x1F, 0x02, 0x00, 0x00, 0x80,
            0x3D, 0x0C, 0xB2, 0x1E, 0x01, 0x00, 0x0F, 0x84,
            0x12, 0x02, 0x00, 0x00, 0x48, 0x8B, 0xD7, 0xE8,
            0x2C, 0x17, 0x00, 0x00, 0x85, 0xC0
        };

        struct PollXInputCallHook
        {
            using OriginalXInputGetState_t = std::uint32_t(WINAPI*)(std::uint32_t, void*);

            static std::uint32_t WINAPI Thunk(std::uint32_t userIndex, void* currentState)
            {
                if (!currentState || userIndex != 0) {
                    return CallOriginal(userIndex, currentState);
                }

                if (!IsHidReaderRunning()) {
                    StartHidReader();
                    logger::info("[DualPad][UpstreamGamepad] Deferred HID reader start released via first poll activity");
                }

                UpstreamGamepadHook::GetSingleton().NotePollCallActivity();
                PadEventSnapshotDispatcher::GetSingleton().DrainOnMainThread(kUpstreamDrainBudget);
                (void)backend::NativeButtonCommitBackend::GetSingleton().CommitPollState();
                const auto result = FillSyntheticXInputState(currentState);

                struct XInputGamepadView
                {
                    std::uint16_t buttons;
                    std::uint8_t leftTrigger;
                    std::uint8_t rightTrigger;
                    std::int16_t thumbLX;
                    std::int16_t thumbLY;
                    std::int16_t thumbRX;
                    std::int16_t thumbRY;
                };

                struct XInputStateView
                {
                    std::uint32_t packetNumber;
                    XInputGamepadView gamepad;
                };

                const auto* state = reinterpret_cast<const XInputStateView*>(currentState);
                const bool active =
                    state->gamepad.buttons != 0 ||
                    state->gamepad.leftTrigger != 0 ||
                    state->gamepad.rightTrigger != 0 ||
                    state->gamepad.thumbLX != 0 ||
                    state->gamepad.thumbLY != 0 ||
                    state->gamepad.thumbRX != 0 ||
                    state->gamepad.thumbRY != 0;

                static std::uint32_t activeLogBudget = 64;
                static std::uint32_t idleLogBudget = 16;
                if ((active && activeLogBudget > 0) || (!active && idleLogBudget > 0)) {
                    if (active) {
                        --activeLogBudget;
                    } else {
                        --idleLogBudget;
                    }

                    logger::info(
                        "[DualPad][UpstreamGamepad] Poll thunk result={} packet={} buttons=0x{:04X} lx={} ly={} rx={} ry={} lt={} rt={} active={}",
                        result,
                        state->packetNumber,
                        state->gamepad.buttons,
                        state->gamepad.thumbLX,
                        state->gamepad.thumbLY,
                        state->gamepad.thumbRX,
                        state->gamepad.thumbRY,
                        state->gamepad.leftTrigger,
                        state->gamepad.rightTrigger,
                        active);
                }

                return result;
            }

            static inline std::uintptr_t _originalTarget{ 0 };

            static std::uint32_t CallOriginal(std::uint32_t userIndex, void* currentState)
            {
                const auto original = reinterpret_cast<OriginalXInputGetState_t>(_originalTarget);
                if (!original) {
                    return ERROR_DEVICE_NOT_CONNECTED;
                }

                return original(userIndex, currentState);
            }
        };
    }

    UpstreamGamepadHook& UpstreamGamepadHook::GetSingleton()
    {
        static UpstreamGamepadHook instance;
        return instance;
    }

    void UpstreamGamepadHook::Install()
    {
        if (_installed || _attemptedInstall) {
            return;
        }
        _attemptedInstall = true;

        const auto& config = RuntimeConfig::GetSingleton();
        if (!config.UseUpstreamGamepadHook()) {
            return;
        }

        if (config.GetUpstreamGamepadHookMode() != UpstreamGamepadHookMode::PollXInputCall) {
            logger::warn(
                "[DualPad][UpstreamGamepad] Unsupported upstream hook mode '{}'; only poll-xinput-call is retained as the official route",
                config.GetUpstreamGamepadHookMode() == UpstreamGamepadHookMode::Disabled ? "disabled" : "unknown");
            return;
        }

        if (REL::Module::get().version() != kSupportedRuntime) {
            if (!_loggedUnsupportedRuntime) {
                logger::error(
                    "[DualPad][UpstreamGamepad] Unsupported runtime {}; Route B is only enabled on Skyrim SE 1.5.97",
                    REL::Module::get().version().string());
                _loggedUnsupportedRuntime = true;
            }
            return;
        }

        const auto pollAddress = REL::Module::get().base() + kExpectedPollRva;
        const auto windowAddress = pollAddress + kExpectedPollXInputWindowOffset;
        if (!REL::verify_code(windowAddress, kExpectedPollXInputWindow)) {
            logger::error(
                "[DualPad][UpstreamGamepad] Poll XInput call-site verification failed at poll=0x{:X}; upstream poll hook remains disabled",
                pollAddress);
            return;
        }

        const auto callAddress = pollAddress + kExpectedPollXInputCallOffset;
        PollXInputCallHook::_originalTarget = SKSE::GetTrampoline().write_call<5>(
            callAddress,
            PollXInputCallHook::Thunk);
        _installed = PollXInputCallHook::_originalTarget != 0;

        if (!_installed) {
            logger::error(
                "[DualPad][UpstreamGamepad] Failed to patch Poll-internal XInput call at 0x{:X}; upstream poll hook remains disabled",
                callAddress);
            return;
        }

        logger::info(
            "[DualPad][UpstreamGamepad] Installed official Poll XInput call-site hook poll=0x{:X} callSite=0x{:X} originalTarget=0x{:X}",
            pollAddress,
            callAddress,
            PollXInputCallHook::_originalTarget);
    }

    bool UpstreamGamepadHook::IsInstalled() const
    {
        return _installed;
    }

    bool UpstreamGamepadHook::IsRouteActive() const
    {
        const auto& config = RuntimeConfig::GetSingleton();
        return _installed &&
            config.UseUpstreamGamepadHook() &&
            config.GetUpstreamGamepadHookMode() == UpstreamGamepadHookMode::PollXInputCall;
    }

    void UpstreamGamepadHook::NotePollCallActivity()
    {
        _lastPollCallTickMs.store(GetTickCount64(), std::memory_order_relaxed);
    }

    bool UpstreamGamepadHook::HasRecentPollCallActivity(std::uint64_t maxAgeMs) const
    {
        const auto lastTick = _lastPollCallTickMs.load(std::memory_order_relaxed);
        if (lastTick == 0) {
            return false;
        }

        const auto now = GetTickCount64();
        return now >= lastTick && (now - lastTick) <= maxAgeMs;
    }
}
