#include "pch.h"

#include "input_v2/presentation/SkyrimCompatibilitySurface.h"

#include <REL/Pattern.h>
#include <SKSE/Version.h>

namespace logger = SKSE::log;

namespace dualpad::input_v2::presentation
{
    namespace
    {
        constexpr REL::ID kIsUsingGamepadId{ 67320 };
        constexpr std::ptrdiff_t kIsUsingGamepadCallOffset = 0xD;
        constexpr REL::ID kGamepadControlsCursorId{ 67321 };
        constexpr std::ptrdiff_t kGamepadControlsCursorCallOffset = 0xD;
        constexpr REL::ID kGamepadHandlerVtblId{ 560029 };
        constexpr std::size_t kGamepadIsEnabledVfuncIndex = 0x8;
        constexpr std::ptrdiff_t kGamepadDelegateOffset = 0x08;
        constexpr std::ptrdiff_t kMenuControlsRemapModeOffset = 0x82;
        constexpr auto kSupportedRuntime = SKSE::RUNTIME_SSE_1_5_97;
        constexpr auto kExpectedCallPatchWindow = REL::make_pattern<"E8 ?? ?? ?? ?? ??">();

        bool IsInstalledStatus(HookInstallStatus status)
        {
            return status == HookInstallStatus::Success ||
                status == HookInstallStatus::AlreadyInstalled;
        }

        bool IsFailClosedStatus(HookInstallStatus status)
        {
            return status == HookInstallStatus::UnsupportedRuntime ||
                status == HookInstallStatus::SignatureMismatch ||
                status == HookInstallStatus::Failed ||
                status == HookInstallStatus::PartialInstall;
        }

        bool HasVfuncSlot(std::uintptr_t vtableBase, std::size_t index)
        {
            if (vtableBase == 0) {
                return false;
            }
            const auto* slot = reinterpret_cast<const std::uintptr_t*>(
                vtableBase + (sizeof(std::uintptr_t) * index));
            return slot && *slot != 0;
        }

        HookInstallResult VerifyHookSites(
            std::uintptr_t usingGamepadCallAddress,
            std::uintptr_t cursorCallAddress,
            std::uintptr_t gamepadHandlerVtblAddress)
        {
            if (!REL::verify_code(usingGamepadCallAddress, kExpectedCallPatchWindow)) {
                return detail::MakeHookInstallResult(
                    HookInstallStatus::SignatureMismatch,
                    "is_using_gamepad_call_signature_mismatch");
            }
            if (!REL::verify_code(cursorCallAddress, kExpectedCallPatchWindow)) {
                return detail::MakeHookInstallResult(
                    HookInstallStatus::SignatureMismatch,
                    "gamepad_cursor_call_signature_mismatch");
            }
            if (!HasVfuncSlot(gamepadHandlerVtblAddress, kGamepadIsEnabledVfuncIndex)) {
                return detail::MakeHookInstallResult(
                    HookInstallStatus::SignatureMismatch,
                    "gamepad_handler_vfunc_signature_mismatch");
            }
            return detail::MakeHookInstallResult(HookInstallStatus::Success, "hook_sites_verified");
        }
    }

    namespace detail
    {
        HookInstallResult MakeHookInstallResult(
            HookInstallStatus status,
            std::string_view debugReason)
        {
            return HookInstallResult{
                .status = status,
                .installed = IsInstalledStatus(status),
                .failClosed = IsFailClosedStatus(status),
                .debugReason = std::string(debugReason)
            };
        }

        HookInstallResult EvaluateHookInstallGate(
            bool runtimeSupported,
            bool signaturesMatch,
            std::string_view debugReason)
        {
            if (!runtimeSupported) {
                return MakeHookInstallResult(
                    HookInstallStatus::UnsupportedRuntime,
                    debugReason.empty() ? "unsupported_runtime" : debugReason);
            }
            if (!signaturesMatch) {
                return MakeHookInstallResult(
                    HookInstallStatus::SignatureMismatch,
                    debugReason.empty() ? "signature_mismatch" : debugReason);
            }
            return MakeHookInstallResult(
                HookInstallStatus::Success,
                debugReason.empty() ? "install_gate_passed" : debugReason);
        }

        HookInstallResult EvaluateHookPatchFailure(
            HookInstallProgress progress,
            std::string_view debugReason)
        {
            return MakeHookInstallResult(
                progress == HookInstallProgress::PatchStarted ?
                    HookInstallStatus::PartialInstall :
                    HookInstallStatus::Failed,
                debugReason);
        }
    }

    bool IsHookInstallFailure(const HookInstallResult& result)
    {
        return result.failClosed;
    }

    const char* ToString(HookInstallStatus status)
    {
        switch (status) {
        case HookInstallStatus::NotAttempted:
            return "not_attempted";
        case HookInstallStatus::Success:
            return "success";
        case HookInstallStatus::UnsupportedRuntime:
            return "unsupported_runtime";
        case HookInstallStatus::SignatureMismatch:
            return "signature_mismatch";
        case HookInstallStatus::AlreadyInstalled:
            return "already_installed";
        case HookInstallStatus::Failed:
            return "failed";
        case HookInstallStatus::PartialInstall:
            return "partial_install";
        default:
            return "unknown";
        }
    }

    std::string ToDebugString(const HookInstallResult& result)
    {
        return std::string("skyrim_compat_hook_status=") +
            ToString(result.status) +
            " installed=" +
            (result.installed ? "true" : "false") +
            " fail_closed=" +
            (result.failClosed ? "true" : "false") +
            " reason=" +
            result.debugReason;
    }

    SkyrimCompatibilitySurface& SkyrimCompatibilitySurface::GetSingleton()
    {
        static SkyrimCompatibilitySurface surface;
        return surface;
    }

    HookInstallResult SkyrimCompatibilitySurface::Install()
    {
        if (!TryBeginInstall()) {
            const auto state = GetInstallState();
            if (state == detail::InstallState::Installed) {
                auto result = detail::MakeHookInstallResult(
                    HookInstallStatus::AlreadyInstalled,
                    "install_already_completed");
                {
                    std::scoped_lock lock(_mutex);
                    _installResult = result;
                }
                logger::info("[DualPad][SkyrimCompat] Hook install already completed");
                return result;
            }
            const auto result = GetInstallResult();
            if (state == detail::InstallState::Failed) {
                logger::error(
                    "[DualPad][SkyrimCompat] Previous hook install failed; refusing silent retry: {}",
                    ToDebugString(result));
            }
            return result;
        }

        if (REL::Module::get().version() != kSupportedRuntime) {
            auto result = detail::EvaluateHookInstallGate(
                false,
                true,
                std::string("unsupported_runtime_") + REL::Module::get().version().string());
            result = MarkInstallFailed(result);
            logger::error(
                "[DualPad][SkyrimCompat] Unsupported runtime {}; input_v2 public surface hooks disabled",
                REL::Module::get().version().string());
            return result;
        }

        detail::HookInstallProgress progress = detail::HookInstallProgress::NotStarted;
        try {
            REL::Relocation<std::uintptr_t> usingGamepadHook{ kIsUsingGamepadId, kIsUsingGamepadCallOffset };
            REL::Relocation<std::uintptr_t> cursorHook{ kGamepadControlsCursorId, kGamepadControlsCursorCallOffset };
            REL::Relocation<std::uintptr_t> gamepadHandlerVtbl{ kGamepadHandlerVtblId };

            auto gate = VerifyHookSites(
                usingGamepadHook.address(),
                cursorHook.address(),
                gamepadHandlerVtbl.address());
            if (IsHookInstallFailure(gate)) {
                gate = MarkInstallFailed(gate);
                logger::error(
                    "[DualPad][SkyrimCompat] Hook signature gate failed: {}",
                    ToDebugString(gate));
                return gate;
            }

            progress = detail::HookInstallProgress::PatchStarted;
            const auto originalUsingGamepad = SKSE::GetTrampoline().write_call<6>(
                usingGamepadHook.address(),
                StaticIsUsingGamepadHook);
            if (originalUsingGamepad == 0) {
                auto result = detail::EvaluateHookPatchFailure(
                    progress,
                    "is_using_gamepad_patch_failed");
                result = MarkInstallFailed(result);
                logger::error("[DualPad][SkyrimCompat] Hook patch failed: {}", ToDebugString(result));
                return result;
            }

            const auto originalCursor = SKSE::GetTrampoline().write_call<6>(
                cursorHook.address(),
                StaticIsGamepadCursorHook);
            if (originalCursor == 0) {
                auto result = detail::EvaluateHookPatchFailure(
                    progress,
                    "gamepad_cursor_patch_failed");
                result = MarkInstallFailed(result);
                logger::error("[DualPad][SkyrimCompat] Hook patch partially failed: {}", ToDebugString(result));
                return result;
            }

            const auto patchSite = detail::MakeVfuncPatchSite(
                gamepadHandlerVtbl.address(),
                kGamepadIsEnabledVfuncIndex);
            REL::Relocation<std::uintptr_t> gamepadHandlerHook{ patchSite.relocationBase };
            const auto originalEnabledHook = gamepadHandlerHook.write_vfunc(
                patchSite.index,
                StaticIsGamepadDeviceEnabledHook);
            if (originalEnabledHook == 0) {
                auto result = detail::EvaluateHookPatchFailure(
                    progress,
                    "gamepad_enabled_vfunc_patch_failed");
                result = MarkInstallFailed(result);
                logger::error("[DualPad][SkyrimCompat] Hook patch partially failed: {}", ToDebugString(result));
                return result;
            }

            auto result = MarkInstallSucceeded();
            logger::info(
                "[DualPad][SkyrimCompat] Installed input_v2 public surface hooks: {}",
                ToDebugString(result));
            return result;
        } catch (...) {
            auto result = detail::EvaluateHookPatchFailure(
                progress,
                progress == detail::HookInstallProgress::PatchStarted ?
                    "exception_after_patch_started" :
                    "exception_before_patch_started");
            result = MarkInstallFailed(result);
            logger::error("[DualPad][SkyrimCompat] Hook install failed: {}", ToDebugString(result));
            return result;
        }
    }

    void SkyrimCompatibilitySurface::Commit(const PublishedPresentationState& state)
    {
        std::scoped_lock lock(_mutex);
        _committed = state;
    }

    void SkyrimCompatibilitySurface::EnableRollback(const LegacyCompatibilitySurface& legacy)
    {
        (void)legacy;
    }

    void SkyrimCompatibilitySurface::DisableRollback()
    {
    }

    bool SkyrimCompatibilitySurface::IsUsingGamepadHook() const
    {
        return GetCommittedState().owner == PresentationOwner::Gamepad;
    }

    bool SkyrimCompatibilitySurface::GamepadControlsCursorHook() const
    {
        return GetCommittedState().cursorOwner == CursorOwner::Gamepad;
    }

    bool SkyrimCompatibilitySurface::IsGamepadDeviceEnabledHook(bool remapMode) const
    {
        if (!remapMode) {
            return true;
        }
        return GetCommittedState().owner == PresentationOwner::Gamepad;
    }

    bool SkyrimCompatibilitySurface::ShouldRefreshMenus()
    {
        std::scoped_lock lock(_mutex);
        const bool presentationDirty =
            HasDirtyFlag(_committed.dirty, PresentationDirtyFlags::Family) ||
            HasDirtyFlag(_committed.dirty, PresentationDirtyFlags::Owner) ||
            HasDirtyFlag(_committed.dirty, PresentationDirtyFlags::Cursor) ||
            HasDirtyFlag(_committed.dirty, PresentationDirtyFlags::Policy);
        if (!presentationDirty || _committed.epoch == 0 || _committed.epoch == _lastRefreshEpoch) {
            return false;
        }
        _lastRefreshEpoch = _committed.epoch;
        return true;
    }

    PresentationParityRecord SkyrimCompatibilitySurface::CompareShadowParity(
        const LegacyCompatibilitySurface& legacy,
        bool remapMode) const
    {
        const auto committed = GetCommittedState();
        PresentationParityRecord record{
            .contextRevision = committed.contextRevision,
            .deviceFamilyRevision = committed.deviceFamilyRevision,
            .gameplayPresentationRevision = committed.gameplayPresentationRevision,
            .epoch = committed.epoch,
            .reason = committed.reason
        };

        const bool projectedIsUsingGamepad = committed.owner == PresentationOwner::Gamepad;
        const bool projectedCursor = committed.cursorOwner == CursorOwner::Gamepad;
        const bool projectedDeviceEnabled =
            !remapMode || committed.owner == PresentationOwner::Gamepad;

        if (legacy.isUsingGamepad != projectedIsUsingGamepad) {
            record.diffs.push_back("isUsingGamepad");
        }
        if (legacy.gamepadControlsCursor != projectedCursor) {
            record.diffs.push_back("gamepadControlsCursor");
        }
        if (legacy.gamepadDeviceEnabled != projectedDeviceEnabled) {
            record.diffs.push_back("gamepadDeviceEnabled");
        }
        record.passes = record.diffs.empty();
        return record;
    }

    PublishedPresentationState SkyrimCompatibilitySurface::GetCommittedState() const
    {
        std::scoped_lock lock(_mutex);
        return _committed;
    }

    HookInstallResult SkyrimCompatibilitySurface::GetInstallResult() const
    {
        std::scoped_lock lock(_mutex);
        return _installResult;
    }

    void SkyrimCompatibilitySurface::ForceInstallResultForTests(const HookInstallResult& result)
    {
        std::scoped_lock lock(_mutex);
        _installResult = result;
        if (result.installed) {
            _installState = detail::InstallState::Installed;
        } else if (result.failClosed) {
            _installState = detail::InstallState::Failed;
        } else {
            _installState = detail::InstallState::NotInstalled;
        }
    }

    void SkyrimCompatibilitySurface::ResetInstallStateForTests()
    {
        std::scoped_lock lock(_mutex);
        _installState = detail::InstallState::NotInstalled;
        _installResult = HookInstallResult{};
    }

    bool SkyrimCompatibilitySurface::TryBeginInstall()
    {
        std::scoped_lock lock(_mutex);
        if (!detail::CanBeginInstall(_installState)) {
            return false;
        }
        _installState = detail::BeginInstall(_installState);
        return true;
    }

    HookInstallResult SkyrimCompatibilitySurface::MarkInstallResultLocked(const HookInstallResult& result)
    {
        _installResult = result;
        return _installResult;
    }

    HookInstallResult SkyrimCompatibilitySurface::MarkInstallSucceeded()
    {
        std::scoped_lock lock(_mutex);
        _installState = detail::CompleteInstall(_installState);
        return MarkInstallResultLocked(
            detail::MakeHookInstallResult(HookInstallStatus::Success, "installed"));
    }

    HookInstallResult SkyrimCompatibilitySurface::MarkInstallFailed(const HookInstallResult& result)
    {
        std::scoped_lock lock(_mutex);
        _installState = detail::FailInstall(_installState);
        return MarkInstallResultLocked(result);
    }

    detail::InstallState SkyrimCompatibilitySurface::GetInstallState() const
    {
        std::scoped_lock lock(_mutex);
        return _installState;
    }

    bool SkyrimCompatibilitySurface::StaticIsUsingGamepadHook()
    {
        return GetSingleton().IsUsingGamepadHook();
    }

    bool SkyrimCompatibilitySurface::StaticIsGamepadCursorHook()
    {
        return GetSingleton().GamepadControlsCursorHook();
    }

    bool SkyrimCompatibilitySurface::StaticIsGamepadDeviceEnabledHook(RE::BSPCGamepadDeviceHandler* device)
    {
        const auto isEnabled = device != nullptr &&
            *reinterpret_cast<void* const*>(reinterpret_cast<const std::uint8_t*>(device) + kGamepadDelegateOffset) != nullptr;
        if (!isEnabled) {
            return false;
        }

        const auto* playerControls = RE::PlayerControls::GetSingleton();
        const auto playerRemapMode = playerControls && playerControls->data.remapMode;

        const auto* menuControls = RE::MenuControls::GetSingleton();
        const auto menuRemapMode = menuControls &&
            *reinterpret_cast<const bool*>(reinterpret_cast<const std::uint8_t*>(menuControls) + kMenuControlsRemapModeOffset);

        if (playerRemapMode || menuRemapMode) {
            return GetSingleton().IsGamepadDeviceEnabledHook(true);
        }

        return true;
    }
}
