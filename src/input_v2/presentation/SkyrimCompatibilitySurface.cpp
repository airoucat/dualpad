#include "pch.h"

#include "input_v2/presentation/SkyrimCompatibilitySurface.h"

#include <REL/Pattern.h>
#include <SKSE/Version.h>

#include <sstream>

namespace logger = SKSE::log;

namespace dualpad::input_v2::presentation
{
    namespace
    {
        constexpr REL::ID kIsUsingGamepadId{ 67320 };
        constexpr REL::ID kGamepadControlsCursorId{ 67321 };
        constexpr REL::ID kGamepadHandlerVtblId{ 560029 };
        constexpr std::size_t kGamepadIsEnabledVfuncIndex = 0x8;
        constexpr std::ptrdiff_t kGamepadDelegateOffset = 0x08;
        constexpr std::ptrdiff_t kMenuControlsRemapModeOffset = 0x82;
        constexpr auto kSupportedRuntime = SKSE::RUNTIME_SSE_1_5_97;
        constexpr auto kExpectedBoolSurfaceEntryWindow =
            REL::make_pattern<"48 83 EC 28 48 8B 49 70 48 85 C9 74 11">();

        const char* ToLogString(PresentationOwner owner)
        {
            return owner == PresentationOwner::Gamepad ? "Gamepad" : "KeyboardMouse";
        }

        const char* ToLogString(NavigationOwner owner)
        {
            switch (owner) {
            case NavigationOwner::Gamepad:
                return "Gamepad";
            case NavigationOwner::KeyboardMouse:
                return "KeyboardMouse";
            case NavigationOwner::None:
            default:
                return "None";
            }
        }

        const char* ToLogString(CursorOwner owner)
        {
            return owner == CursorOwner::Gamepad ? "Gamepad" : "KeyboardMouse";
        }

        std::uint32_t ToDirtyBits(PresentationDirtyFlags flags)
        {
            return static_cast<std::uint32_t>(static_cast<std::uint8_t>(flags));
        }

        bool NotifyMenuPresentationChanged(RE::IMenu& menu)
        {
            if (!menu.uiMovie) {
                return false;
            }

            RE::GFxValue callback;
            if (!menu.uiMovie->GetVariable(&callback, "_root.DualPad_OnPresentationChanged") ||
                callback.IsUndefined()) {
                return false;
            }

            menu.uiMovie->InvokeNoReturn("_root.DualPad_OnPresentationChanged", nullptr, 0);
            return true;
        }

        bool IsInstalledStatus(HookInstallStatus status)
        {
            return status == HookInstallStatus::Success ||
                status == HookInstallStatus::AlreadyInstalled;
        }

        bool IsFailClosedStatus(HookInstallStatus status)
        {
            (void)status;
            return false;
        }

        bool IsInstallAttemptFailureStatus(HookInstallStatus status)
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

        bool HasRefreshRelevantDirty(PresentationDirtyFlags flags)
        {
            return HasDirtyFlag(flags, PresentationDirtyFlags::Family) ||
                HasDirtyFlag(flags, PresentationDirtyFlags::Owner) ||
                HasDirtyFlag(flags, PresentationDirtyFlags::Cursor) ||
                HasDirtyFlag(flags, PresentationDirtyFlags::Context) ||
                HasDirtyFlag(flags, PresentationDirtyFlags::ActionSets) ||
                HasDirtyFlag(flags, PresentationDirtyFlags::Policy);
        }

        bool IsMenuPresentationContext(context::UiContextId id)
        {
            switch (id) {
            case context::UiContextId::UnknownTrackedMenu:
            case context::UiContextId::Inventory:
            case context::UiContextId::Magic:
            case context::UiContextId::Map:
            case context::UiContextId::Journal:
            case context::UiContextId::Dialogue:
            case context::UiContextId::Favorites:
            case context::UiContextId::Tween:
            case context::UiContextId::Container:
            case context::UiContextId::Barter:
            case context::UiContextId::Training:
            case context::UiContextId::LevelUp:
            case context::UiContextId::RaceSex:
            case context::UiContextId::StatsMenu:
            case context::UiContextId::SkillMenu:
            case context::UiContextId::BookMenu:
            case context::UiContextId::MessageBox:
            case context::UiContextId::Quantity:
            case context::UiContextId::Gift:
            case context::UiContextId::Creations:
            case context::UiContextId::Console:
            case context::UiContextId::ItemMenu:
            case context::UiContextId::DebugText:
            case context::UiContextId::MapMenuContext:
            case context::UiContextId::Stats:
            case context::UiContextId::Cursor:
            case context::UiContextId::Book:
            case context::UiContextId::DebugOverlay:
            case context::UiContextId::TFCMode:
            case context::UiContextId::DebugMapMenu:
            case context::UiContextId::Lockpicking:
            case context::UiContextId::Favor:
                return true;
            case context::UiContextId::None:
            case context::UiContextId::Combat:
            case context::UiContextId::Sneaking:
            case context::UiContextId::Riding:
            case context::UiContextId::Werewolf:
            case context::UiContextId::VampireLord:
            case context::UiContextId::Death:
            case context::UiContextId::Bleedout:
            case context::UiContextId::Ragdoll:
            case context::UiContextId::KillMove:
            case context::UiContextId::PassthroughOverlay:
            default:
                return false;
            }
        }

        bool IsRefreshableMenuPresentation(const PublishedPresentationState& state)
        {
            return IsMenuPresentationContext(state.uiContextId) &&
                state.menuObserverCompleteness == menu::ObserverCompleteness::Complete &&
                !state.menuIdentityDegraded;
        }

        HookInstallResult VerifyHookSites(
            std::uintptr_t usingGamepadAddress,
            std::uintptr_t cursorAddress,
            std::uintptr_t gamepadHandlerVtblAddress)
        {
            if (!REL::verify_code(usingGamepadAddress, kExpectedBoolSurfaceEntryWindow)) {
                return detail::MakeHookInstallResult(
                    HookInstallStatus::SignatureMismatch,
                    "is_using_gamepad_entry_signature_mismatch");
            }
            if (!REL::verify_code(cursorAddress, kExpectedBoolSurfaceEntryWindow)) {
                return detail::MakeHookInstallResult(
                    HookInstallStatus::SignatureMismatch,
                    "gamepad_cursor_entry_signature_mismatch");
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

        _hooksEnabled.store(false, std::memory_order_release);
        _originalUsingGamepadTarget.store(0, std::memory_order_release);
        _originalCursorTarget.store(0, std::memory_order_release);
        _originalDeviceEnabledTarget.store(0, std::memory_order_release);

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
            REL::Relocation<std::uintptr_t> usingGamepadHook{ kIsUsingGamepadId };
            REL::Relocation<std::uintptr_t> cursorHook{ kGamepadControlsCursorId };
            REL::Relocation<std::uintptr_t> gamepadHandlerVtbl{ kGamepadHandlerVtblId };

            auto gate = VerifyHookSites(
                usingGamepadHook.address(),
                cursorHook.address(),
                gamepadHandlerVtbl.address());
            if (IsInstallAttemptFailureStatus(gate.status)) {
                gate = MarkInstallFailed(gate);
                logger::error(
                    "[DualPad][SkyrimCompat] Hook signature gate failed: {}",
                    ToDebugString(gate));
                return gate;
            }

            progress = detail::HookInstallProgress::PatchStarted;
            const auto originalUsingGamepad = SKSE::GetTrampoline().write_branch<5>(
                usingGamepadHook.address(),
                StaticIsUsingGamepadHook);
            if (originalUsingGamepad == 0) {
                auto result = detail::EvaluateHookPatchFailure(
                    progress,
                    "is_using_gamepad_entry_patch_failed");
                result = MarkInstallFailed(result);
                logger::error("[DualPad][SkyrimCompat] Hook patch failed: {}", ToDebugString(result));
                return result;
            }
            _originalUsingGamepadTarget.store(originalUsingGamepad, std::memory_order_release);

            const auto originalCursor = SKSE::GetTrampoline().write_branch<5>(
                cursorHook.address(),
                StaticIsGamepadCursorHook);
            if (originalCursor == 0) {
                auto result = detail::EvaluateHookPatchFailure(
                    progress,
                    "gamepad_cursor_entry_patch_failed");
                result = MarkInstallFailed(result);
                logger::error("[DualPad][SkyrimCompat] Hook patch partially failed: {}", ToDebugString(result));
                return result;
            }
            _originalCursorTarget.store(originalCursor, std::memory_order_release);

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
            _originalDeviceEnabledTarget.store(originalEnabledHook, std::memory_order_release);

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
        if (!_hooksEnabled.load(std::memory_order_acquire)) {
            return CallOriginalIsUsingGamepad();
        }
        return GetCommittedState().owner == PresentationOwner::Gamepad;
    }

    bool SkyrimCompatibilitySurface::GamepadControlsCursorHook() const
    {
        if (!_hooksEnabled.load(std::memory_order_acquire)) {
            return CallOriginalGamepadControlsCursor();
        }
        return GetCommittedState().cursorOwner == CursorOwner::Gamepad;
    }

    bool SkyrimCompatibilitySurface::IsGamepadDeviceEnabledHook(bool remapMode) const
    {
        if (!_hooksEnabled.load(std::memory_order_acquire)) {
            return CallOriginalGamepadDeviceEnabled(nullptr);
        }
        if (!remapMode) {
            return true;
        }
        return GetCommittedState().owner == PresentationOwner::Gamepad;
    }

    bool SkyrimCompatibilitySurface::ShouldRefreshMenus()
    {
        std::scoped_lock lock(_mutex);
        return HasPendingMenuRefreshLocked();
    }

    bool SkyrimCompatibilitySurface::RefreshMenusIfNeeded()
    {
        if (!ShouldRefreshMenus()) {
            return false;
        }

        const auto queued = QueueMenuRefreshTask();
        const auto state = GetCommittedState();
        logger::debug(
            "[DualPad][SkyrimCompat] menu_refresh_request queued={} owner={} navigationOwner={} cursorOwner={} epoch={} dirty=0x{:02X} contextRevision={} gameplayPresentationRevision={}",
            queued,
            ToLogString(state.owner),
            ToLogString(state.navigationOwner),
            ToLogString(state.cursorOwner),
            state.epoch,
            ToDirtyBits(state.dirty),
            state.contextRevision,
            state.gameplayPresentationRevision);
        return queued;
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
        } else if (IsInstallAttemptFailureStatus(result.status)) {
            _installState = detail::InstallState::Failed;
        } else {
            _installState = detail::InstallState::NotInstalled;
        }
        _hooksEnabled.store(result.installed, std::memory_order_release);
    }

    void SkyrimCompatibilitySurface::ForceOriginalHookOutputsForTests(const LegacyCompatibilitySurface& legacy)
    {
        std::scoped_lock lock(_mutex);
        _originalHookOutputs = legacy;
        _originalUsingGamepadTarget.store(0, std::memory_order_release);
        _originalCursorTarget.store(0, std::memory_order_release);
        _originalDeviceEnabledTarget.store(0, std::memory_order_release);
    }

    void SkyrimCompatibilitySurface::ForceHooksEnabledForTests(bool enabled)
    {
        _hooksEnabled.store(enabled, std::memory_order_release);
    }

    void SkyrimCompatibilitySurface::SetMenuRefreshTaskSinkForTests(MenuRefreshTaskSink sink)
    {
        std::scoped_lock lock(_mutex);
        _refreshTaskSinkForTests = std::move(sink);
    }

    void SkyrimCompatibilitySurface::CompleteQueuedRefreshForTests()
    {
        std::scoped_lock lock(_mutex);
        _lastRefreshCompletedEpoch = _committed.epoch;
        _lastRefreshCompletedKey = RefreshKeyLocked();
        _refreshQueued = false;
    }

    void SkyrimCompatibilitySurface::ResetInstallStateForTests()
    {
        std::scoped_lock lock(_mutex);
        _installState = detail::InstallState::NotInstalled;
        _installResult = HookInstallResult{};
        _hooksEnabled.store(true, std::memory_order_release);
        _originalUsingGamepadTarget.store(0, std::memory_order_release);
        _originalCursorTarget.store(0, std::memory_order_release);
        _originalDeviceEnabledTarget.store(0, std::memory_order_release);
    }

    void SkyrimCompatibilitySurface::ResetRefreshStateForTests()
    {
        std::scoped_lock lock(_mutex);
        _lastRefreshQueuedEpoch = 0;
        _lastRefreshCompletedEpoch = 0;
        _lastRefreshQueuedKey.clear();
        _lastRefreshCompletedKey.clear();
        _refreshQueued = false;
        _refreshTaskSinkForTests = {};
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

    bool SkyrimCompatibilitySurface::QueueMenuRefreshTask()
    {
        MenuRefreshTaskSink testSink;
        {
            std::scoped_lock lock(_mutex);
            if (!HasPendingMenuRefreshLocked()) {
                return false;
            }
            _refreshQueued = true;
            _lastRefreshQueuedEpoch = _committed.epoch;
            _lastRefreshQueuedKey = RefreshKeyLocked();
            testSink = _refreshTaskSinkForTests;
        }

        bool queued = false;
        if (testSink) {
            queued = testSink(DoRefreshMenus);
        } else if (auto* taskInterface = SKSE::GetTaskInterface(); taskInterface) {
            taskInterface->AddUITask(DoRefreshMenus);
            queued = true;
        }

        if (queued) {
            return true;
        }

        {
            std::scoped_lock lock(_mutex);
            _refreshQueued = false;
            _lastRefreshQueuedEpoch = 0;
            _lastRefreshQueuedKey.clear();
        }
        logger::debug("[DualPad][SkyrimCompat] menu_refresh_skipped reason=no_skse_task_interface");
        return false;
    }

    bool SkyrimCompatibilitySurface::HasPendingMenuRefreshLocked() const
    {
        if (_refreshQueued ||
            _committed.epoch == 0 ||
            !IsRefreshableMenuPresentation(_committed) ||
            !HasRefreshRelevantDirty(_committed.dirty)) {
            return false;
        }

        const auto key = RefreshKeyLocked();
        return key != _lastRefreshQueuedKey && key != _lastRefreshCompletedKey;
    }

    std::string SkyrimCompatibilitySurface::RefreshKeyLocked() const
    {
        std::ostringstream out;
        out << _committed.epoch
            << "|ctx=" << _committed.contextRevision
            << "|ui=" << static_cast<std::uint16_t>(_committed.uiContextId)
            << "|gameplay=" << _committed.gameplayPresentationRevision
            << "|policy=" << _committed.presentationPolicyId
            << "|base=" << _committed.actionSetStack.baseSetId
            << "|layers=";
        for (const auto& layer : _committed.actionSetStack.layerIds) {
            out << layer << ',';
        }
        out << "|anchors=";
        for (const auto& anchor : _committed.actionSetStack.scopeAnchorIds) {
            out << anchor << ',';
        }
        return out.str();
    }

    bool SkyrimCompatibilitySurface::CallOriginalIsUsingGamepad() const
    {
        using Hook = bool (*)();
        if (const auto target = _originalUsingGamepadTarget.load(std::memory_order_acquire); target != 0) {
            return reinterpret_cast<Hook>(target)();
        }
        std::scoped_lock lock(_mutex);
        return _originalHookOutputs.isUsingGamepad;
    }

    bool SkyrimCompatibilitySurface::CallOriginalGamepadControlsCursor() const
    {
        using Hook = bool (*)();
        if (const auto target = _originalCursorTarget.load(std::memory_order_acquire); target != 0) {
            return reinterpret_cast<Hook>(target)();
        }
        std::scoped_lock lock(_mutex);
        return _originalHookOutputs.gamepadControlsCursor;
    }

    bool SkyrimCompatibilitySurface::CallOriginalGamepadDeviceEnabled(RE::BSPCGamepadDeviceHandler* device) const
    {
        using Hook = bool (*)(RE::BSPCGamepadDeviceHandler*);
        if (const auto target = _originalDeviceEnabledTarget.load(std::memory_order_acquire); target != 0) {
            return reinterpret_cast<Hook>(target)(device);
        }
        std::scoped_lock lock(_mutex);
        return _originalHookOutputs.gamepadDeviceEnabled;
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
        _hooksEnabled.store(true, std::memory_order_release);
        return MarkInstallResultLocked(
            detail::MakeHookInstallResult(HookInstallStatus::Success, "installed"));
    }

    HookInstallResult SkyrimCompatibilitySurface::MarkInstallFailed(const HookInstallResult& result)
    {
        std::scoped_lock lock(_mutex);
        _installState = detail::FailInstall(_installState);
        _hooksEnabled.store(false, std::memory_order_release);
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
        auto& surface = GetSingleton();
        if (!surface._hooksEnabled.load(std::memory_order_acquire)) {
            return surface.CallOriginalGamepadDeviceEnabled(device);
        }

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
            return surface.IsGamepadDeviceEnabledHook(true);
        }

        return true;
    }

    void SkyrimCompatibilitySurface::DoRefreshMenus()
    {
        auto& surface = GetSingleton();
        std::size_t refreshed = 0;
        std::size_t notified = 0;
        std::size_t skippedNotReady = 0;

        const auto stateAtStart = surface.GetCommittedState();
        if (!IsRefreshableMenuPresentation(stateAtStart)) {
            {
                std::scoped_lock lock(surface._mutex);
                surface._refreshQueued = false;
            }
            logger::debug(
                "[DualPad][SkyrimCompat] menu_refresh_skipped reason=unstable_menu_context uiContext={} completeness={} degraded={} epoch={} dirty=0x{:02X}",
                static_cast<std::uint16_t>(stateAtStart.uiContextId),
                static_cast<unsigned>(stateAtStart.menuObserverCompleteness),
                stateAtStart.menuIdentityDegraded,
                stateAtStart.epoch,
                ToDirtyBits(stateAtStart.dirty));
            return;
        }

        if (auto* ui = RE::UI::GetSingleton(); ui) {
            for (auto& menu : ui->menuStack) {
                if (!menu) {
                    ++skippedNotReady;
                    continue;
                }
                if (!menu->uiMovie) {
                    ++skippedNotReady;
                    continue;
                }

                menu->RefreshPlatform();
                ++refreshed;
                if (NotifyMenuPresentationChanged(*menu)) {
                    ++notified;
                }
            }
        }

        {
            std::scoped_lock lock(surface._mutex);
            surface._lastRefreshCompletedEpoch = surface._committed.epoch;
            surface._lastRefreshCompletedKey = surface.RefreshKeyLocked();
            surface._refreshQueued = false;
        }

        const auto state = surface.GetCommittedState();
        logger::debug(
            "[DualPad][SkyrimCompat] menu_refresh_done menus={} notified={} skippedNotReady={} owner={} navigationOwner={} cursorOwner={} epoch={} dirty=0x{:02X}",
            refreshed,
            notified,
            skippedNotReady,
            ToLogString(state.owner),
            ToLogString(state.navigationOwner),
            ToLogString(state.cursorOwner),
            state.epoch,
            ToDirtyBits(state.dirty));
    }
}
