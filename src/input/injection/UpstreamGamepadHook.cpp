#include "pch.h"
#include "input/injection/UpstreamGamepadHook.h"

#include <Windows.h>
#include <SKSE/Version.h>

#include <array>
#include <cstring>
#include <intrin.h>
#include <string>
#include <vector>

#include "input/XInputStateBridge.h"
#include "input/injection/HookPatchTransaction.h"
#include "input/injection/NativeKbmSemanticPolicy.h"
#include "input/injection/PollDiagnostics.h"
#include "input/injection/PollMaterializationReceipt.h"
#include "input/injection/RouteHealthContract.h"
#include "input_v2/gameplay/PollOutputFrame.h"

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        constexpr auto kSupportedRuntime = SKSE::RUNTIME_SSE_1_5_97;
        constexpr std::uintptr_t kExpectedPollRva = 0xC1AB40;
        constexpr std::ptrdiff_t kExpectedPollXInputCallOffset = 0x5D;
        constexpr std::ptrdiff_t kExpectedPollXInputWindowOffset = 0x3E;
        constexpr std::uint64_t kPollDiagnosticCapacity = 256;
        PollDiagnosticLimiter g_pollDiagnosticLimiter{ kPollDiagnosticCapacity };
        I0AvailabilitySampler g_i0AvailabilitySampler{ 5'000 };
        constexpr std::array<std::uint8_t, 38> kExpectedPollXInputWindow = {
            0x8B, 0x89, 0xC8, 0x00, 0x00, 0x00, 0x83, 0xF9,
            0xFF, 0x0F, 0x84, 0x1F, 0x02, 0x00, 0x00, 0x80,
            0x3D, 0x0C, 0xB2, 0x1E, 0x01, 0x00, 0x0F, 0x84,
            0x12, 0x02, 0x00, 0x00, 0x48, 0x8B, 0xD7, 0xE8,
            0x2C, 0x17, 0x00, 0x00, 0x85, 0xC0
        };

        patching::Bytes ReadPatchBytes(std::uintptr_t address, std::size_t size)
        {
            patching::Bytes bytes(size);
            std::memcpy(bytes.data(), reinterpret_cast<const void*>(address), size);
            return bytes;
        }

        bool CompareWritePatchBytes(
            std::uintptr_t address,
            const patching::Bytes& expected,
            const patching::Bytes& desired)
        {
            if (expected.size() != desired.size() || expected.empty()) {
                return false;
            }
            return REL::safe_write(
                address,
                desired.data(),
                desired.size(),
                expected.data(),
                expected.size());
        }

        std::uintptr_t AllocateAbsoluteJumpStub(std::uintptr_t destination)
        {
            const auto bytes = patching::MakeAbsoluteJump(destination);
            if (bytes.empty()) {
                return 0;
            }
            auto* memory = SKSE::GetTrampoline().allocate(bytes.size());
            if (!memory) {
                return 0;
            }
            const auto address = reinterpret_cast<std::uintptr_t>(memory);
            REL::safe_write(address, bytes.data(), bytes.size());
            return ReadPatchBytes(address, bytes.size()) == bytes ? address : 0;
        }

        struct PollXInputCallHook
        {
            using OriginalXInputGetState_t = std::uint32_t(WINAPI*)(std::uint32_t, void*);

            static std::uint32_t WINAPI Thunk(std::uint32_t userIndex, void* currentState)
            {
                if (!_routeEnabled.load(std::memory_order_acquire) || !currentState || userIndex != 0) {
                    return CallOriginal(userIndex, currentState);
                }

                const bool pollDiagnosticsEnabled = RuntimeConfig::GetSingleton().LogPollDiagnostics();
                const auto diagnostic = g_pollDiagnosticLimiter.Begin(pollDiagnosticsEnabled);
                const auto threadId = GetCurrentThreadId();
                const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
                if (diagnostic.record) {
                    logger::info(
                        "[DualPad][PollDiagnostic] event=enter sequence={} thread={} inFlight={} caller=0x{:X} userIndex={}",
                        diagnostic.sequence,
                        threadId,
                        diagnostic.inFlight,
                        caller,
                        userIndex);
                }

                auto& upstreamHook = UpstreamGamepadHook::GetSingleton();
                upstreamHook.NotePollCallActivity();
                const auto outputFrame = input_v2::gameplay::PollOutputPublication::GetSingleton().AcquireForPoll();
                if (auto* controlMap = RE::ControlMap::GetSingleton(); controlMap) {
                    auto& ignoreKeyboardMouse =
                        controlMap->GetRuntimeData().ignoreKeyboardMouse;
                    if (ApplyNativeKbmSemanticPolicy(
                            outputFrame->remapMode,
                            ignoreKeyboardMouse)) {
                        logger::info(
                            "[DualPad][NativeKbmSemantics] ignoreKeyboardMouse={} remapMode={} phase=before_controlmap_mapping",
                            ignoreKeyboardMouse,
                            outputFrame->remapMode);
                    }
                }
                const auto result = FillSyntheticXInputState(currentState, *outputFrame);
                if (result == ERROR_SUCCESS) {
                    (void)PollMaterializationReceiptStore::GetSingleton()
                        .PublishAfterSuccessfulSerialize(*outputFrame, threadId);
                }

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
                const auto currentStateClass = BuildI0AvailabilityStateClass(
                    state->gamepad.buttons,
                    state->gamepad.thumbLX,
                    state->gamepad.thumbLY,
                    state->gamepad.thumbRX,
                    state->gamepad.thumbRY,
                    state->gamepad.leftTrigger,
                    state->gamepad.rightTrigger);
                const auto availabilityFingerprint = BuildI0AvailabilityFingerprint(
                    I0AvailabilityFingerprintInput{
                        .xinputResult = result,
                        .currentStateClass = currentStateClass,
                        .gamepadSessionId = outputFrame->gamepadSessionId,
                        .contextRevision = static_cast<std::uint32_t>(
                            outputFrame->contextRevision),
                        .menuStackRevision = outputFrame->menuStackRevision,
                        .context = static_cast<std::uint16_t>(outputFrame->context),
                        .routeHealth = static_cast<std::uint8_t>(outputFrame->routeHealth),
                        .remapMode = outputFrame->remapMode,
                        .connected = outputFrame->connected,
                        .delegateReady = outputFrame->delegateReady
                    });
                const auto availabilitySample = g_i0AvailabilitySampler.Observe(
                    GetTickCount64(),
                    availabilityFingerprint);
                if (availabilitySample.record) {
                    const bool neutral = state->gamepad.buttons == 0 &&
                        state->gamepad.thumbLX == 0 &&
                        state->gamepad.thumbLY == 0 &&
                        state->gamepad.thumbRX == 0 &&
                        state->gamepad.thumbRY == 0 &&
                        state->gamepad.leftTrigger == 0 &&
                        state->gamepad.rightTrigger == 0;
                    logger::info(
                        "[DualPad][I0Availability] pollCount={} sampleReason={} thread={} caller=0x{:X} nativePollReached=true compatPatchGroup=disabled_i0_no_go xinputResult={} outputGeneration={} runtimeGeneration={} routeHealth={} context={} contextRevision={} menuStackRevision={} remapMode={} connected={} delegateReady={} gamepadSession={} packet={} neutral={} buttons=0x{:04X} lx={} ly={} rx={} ry={} lt={} rt={}",
                        availabilitySample.pollCount,
                        ToString(availabilitySample.reason),
                        threadId,
                        caller,
                        result,
                        outputFrame->publicationGeneration,
                        outputFrame->runtimeGeneration,
                        input_v2::gameplay::ToString(outputFrame->routeHealth),
                        ToString(outputFrame->context),
                        outputFrame->contextRevision,
                        outputFrame->menuStackRevision,
                        outputFrame->remapMode,
                        outputFrame->connected,
                        outputFrame->delegateReady,
                        outputFrame->gamepadSessionId,
                        state->packetNumber,
                        neutral,
                        state->gamepad.buttons,
                        state->gamepad.thumbLX,
                        state->gamepad.thumbLY,
                        state->gamepad.thumbRX,
                        state->gamepad.thumbRY,
                        state->gamepad.leftTrigger,
                        state->gamepad.rightTrigger);
                }
                const auto remainingInFlight = g_pollDiagnosticLimiter.End(pollDiagnosticsEnabled);
                if (diagnostic.record) {
                    logger::info(
                        "[DualPad][PollDiagnostic] event=exit sequence={} thread={} inFlight={} result={} outputGeneration={} runtimeGeneration={} routeHealth={} pulseToken={} pulseDownGeneration={} pulseUpGeneration={} contextRevision={} contextEpoch={} presentationEpoch={} menuStackRevision={} packet={} buttons=0x{:04X} lx={} ly={} rx={} ry={} lt={} rt={} dropped={}",
                        diagnostic.sequence,
                        threadId,
                        remainingInFlight,
                        result,
                        outputFrame->publicationGeneration,
                        outputFrame->runtimeGeneration,
                        input_v2::gameplay::ToString(outputFrame->routeHealth),
                        outputFrame->pulseToken,
                        outputFrame->pulseDownGeneration,
                        outputFrame->pulseUpGeneration,
                        outputFrame->contextRevision,
                        outputFrame->contextEpoch,
                        outputFrame->presentationEpoch,
                        outputFrame->menuStackRevision,
                        state->packetNumber,
                        state->gamepad.buttons,
                        state->gamepad.thumbLX,
                        state->gamepad.thumbLY,
                        state->gamepad.thumbRX,
                        state->gamepad.thumbRY,
                        state->gamepad.leftTrigger,
                        state->gamepad.rightTrigger,
                        g_pollDiagnosticLimiter.Dropped());
                }

                return result;
            }

            static inline std::uintptr_t _originalTarget{ 0 };
            static inline std::atomic_bool _routeEnabled{ false };

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
        if (_installed.load(std::memory_order_acquire)) {
            SetInstallStatus(UpstreamGamepadHookInstallStatus::AlreadyInstalled, "already_installed");
            return;
        }
        if (_attemptedInstall.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        PollXInputCallHook::_routeEnabled.store(false, std::memory_order_release);

        const auto& config = RuntimeConfig::GetSingleton();
        if (!config.UseUpstreamGamepadHook()) {
            SetInstallStatus(UpstreamGamepadHookInstallStatus::DisabledByConfig, "disabled_by_config");
            return;
        }

        if (config.GetUpstreamGamepadHookMode() != UpstreamGamepadHookMode::PollXInputCall) {
            if (config.GetUpstreamGamepadHookMode() == UpstreamGamepadHookMode::Disabled) {
                SetInstallStatus(UpstreamGamepadHookInstallStatus::DisabledByConfig, "disabled_by_config");
                return;
            }
            SetInstallStatus(UpstreamGamepadHookInstallStatus::UnsupportedMode, "unsupported_mode");
            logger::warn(
                "[DualPad][UpstreamGamepad] Unsupported upstream hook mode '{}'; only poll-xinput-call is retained as the official route",
                "unknown");
            return;
        }

        if (REL::Module::get().version() != kSupportedRuntime) {
            SetInstallStatus(
                UpstreamGamepadHookInstallStatus::UnsupportedRuntime,
                std::string("unsupported_runtime_") + REL::Module::get().version().string());
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
            SetInstallStatus(
                UpstreamGamepadHookInstallStatus::SignatureMismatch,
                "poll_xinput_call_signature_mismatch");
            logger::error(
                "[DualPad][UpstreamGamepad] Poll XInput call-site verification failed at poll=0x{:X}; upstream poll hook remains disabled",
                pollAddress);
            return;
        }

        const auto callAddress = pollAddress + kExpectedPollXInputCallOffset;
        const auto originalCall = ReadPatchBytes(callAddress, 5);
        const auto originalTarget = patching::DecodeRelativeTarget(
            callAddress,
            originalCall,
            patching::RelativePatchOpcode::Call);
        const auto thunkStub = AllocateAbsoluteJumpStub(
            reinterpret_cast<std::uintptr_t>(PollXInputCallHook::Thunk));
        const auto replacementCall = patching::MakeRelativePatch(
            callAddress,
            thunkStub,
            patching::RelativePatchOpcode::Call,
            originalCall.size());
        if (!originalTarget || thunkStub == 0 || replacementCall.empty()) {
            SetInstallStatus(UpstreamGamepadHookInstallStatus::PatchFailed, "patch_preparation_failed");
            logger::error(
                "[DualPad][UpstreamGamepad] Failed to prepare transactional Poll hook at 0x{:X}; upstream route remains disabled",
                callAddress);
            return;
        }

        PollXInputCallHook::_originalTarget = *originalTarget;
        std::vector<patching::PatchSite> sites;
        sites.push_back(patching::PatchSite{
            .name = "poll_xinput_call",
            .original = originalCall,
            .replacement = replacementCall,
            .read = [callAddress]() { return ReadPatchBytes(callAddress, 5); },
            .compareWrite = [callAddress](const auto& expected, const auto& desired) {
                return CompareWritePatchBytes(callAddress, expected, desired);
            }
        });
        const auto transaction = patching::ExecutePatchTransaction(sites);
        switch (transaction.outcome) {
        case patching::PatchTransactionOutcome::Installed:
            break;
        case patching::PatchTransactionOutcome::RolledBack:
            SetInstallStatus(UpstreamGamepadHookInstallStatus::PatchRolledBack, "patch_failed_rolled_back");
            return;
        case patching::PatchTransactionOutcome::UnsafePartial:
            SetInstallStatus(UpstreamGamepadHookInstallStatus::UnsafePartial, "unsafe_partial_patch");
            logger::critical(
                "[DualPad][UpstreamGamepad] Unsafe partial Poll hook at site={}; thunk remains original passthrough",
                transaction.failedSite);
            return;
        case patching::PatchTransactionOutcome::FailedNoWrite:
        default:
            SetInstallStatus(UpstreamGamepadHookInstallStatus::PatchFailed, "patch_failed_no_write");
            return;
        }

        SetInstallStatus(UpstreamGamepadHookInstallStatus::Installed, "installed");
        PollXInputCallHook::_routeEnabled.store(true, std::memory_order_release);
        logger::info(
            "[DualPad][UpstreamGamepad] Installed official Poll XInput call-site hook poll=0x{:X} callSite=0x{:X} originalTarget=0x{:X}",
            pollAddress,
            callAddress,
            PollXInputCallHook::_originalTarget);
    }

    bool UpstreamGamepadHook::IsInstalled() const
    {
        return _installed.load(std::memory_order_acquire);
    }

    bool UpstreamGamepadHook::IsRouteActive() const
    {
        const auto& config = RuntimeConfig::GetSingleton();
        return _installed.load(std::memory_order_acquire) &&
            config.UseUpstreamGamepadHook() &&
            config.GetUpstreamGamepadHookMode() == UpstreamGamepadHookMode::PollXInputCall;
    }

    UpstreamGamepadHookInstallStatus UpstreamGamepadHook::GetInstallStatus() const
    {
        return _installStatus.load(std::memory_order_acquire);
    }

    std::string UpstreamGamepadHook::GetInstallDebugReason() const
    {
        std::scoped_lock lock(_installReasonMutex);
        return _installDebugReason;
    }

    bool UpstreamGamepadHook::HasInstallFailed() const
    {
        return HasUpstreamGamepadHookInstallFailed(GetInstallStatus());
    }

    bool UpstreamGamepadHook::WasInstallAttempted() const
    {
        return WasUpstreamGamepadHookInstallAttempted(GetInstallStatus());
    }

    UpstreamRouteInstallSnapshot UpstreamGamepadHook::GetInstallSnapshot(bool configured) const
    {
        std::scoped_lock lock(_installReasonMutex);
        const auto status = _installStatus.load(std::memory_order_acquire);
        return UpstreamRouteInstallSnapshot{
            .configured = configured,
            .installAttempted = WasUpstreamGamepadHookInstallAttempted(status),
            .installed = status == UpstreamGamepadHookInstallStatus::Installed ||
                status == UpstreamGamepadHookInstallStatus::AlreadyInstalled,
            .failed = HasUpstreamGamepadHookInstallFailed(status),
            .status = status,
            .operationalState = ResolveUpstreamHookOperationalState(status),
            .disposition = ResolveUpstreamHookFailureDisposition(status),
            .debugReason = _installDebugReason
        };
    }

    void UpstreamGamepadHook::NotePollCallActivity()
    {
        _lastPollCallTickMs.store(GetTickCount64(), std::memory_order_relaxed);
    }

    std::optional<std::uint64_t> UpstreamGamepadHook::GetLastPollCallAgeMs() const
    {
        const auto lastTick = _lastPollCallTickMs.load(std::memory_order_relaxed);
        if (lastTick == 0) {
            return std::nullopt;
        }

        const auto now = GetTickCount64();
        if (now < lastTick) {
            return std::nullopt;
        }

        return now - lastTick;
    }

    bool UpstreamGamepadHook::HasRecentPollCallActivity(std::uint64_t maxAgeMs) const
    {
        const auto lastPollAgeMs = GetLastPollCallAgeMs();
        return lastPollAgeMs && *lastPollAgeMs <= maxAgeMs;
    }

    void UpstreamGamepadHook::SetInstallStatus(
        UpstreamGamepadHookInstallStatus status,
        std::string_view debugReason)
    {
        {
            std::scoped_lock lock(_installReasonMutex);
            _installDebugReason = debugReason.empty() ? ToString(status) : std::string(debugReason);
            _installed.store(
                status == UpstreamGamepadHookInstallStatus::Installed ||
                    status == UpstreamGamepadHookInstallStatus::AlreadyInstalled,
                std::memory_order_release);
            _installStatus.store(status, std::memory_order_release);
        }
    }

    UpstreamRouteInstallSnapshot GetUpstreamRouteInstallSnapshot()
    {
        const auto& config = RuntimeConfig::GetSingleton();
        const auto& hook = UpstreamGamepadHook::GetSingleton();
        const bool configured = config.UseUpstreamGamepadHook();
        return hook.GetInstallSnapshot(configured);
    }
}
