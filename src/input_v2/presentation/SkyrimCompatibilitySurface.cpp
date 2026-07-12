#include "pch.h"

#include "input_v2/presentation/SkyrimCompatibilitySurface.h"
#include "input_v2/presentation/SkyrimEngineModeRouter.h"

#include <REL/Pattern.h>
#include <RE/B/BSInputDeviceManager.h>
#include <SKSE/Version.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <sstream>

namespace logger = SKSE::log;

namespace dualpad::input_v2::presentation
{
    namespace
    {
        constexpr REL::ID kIsUsingGamepadId{ 67320 };
        constexpr REL::ID kGamepadHandlerVtblId{ 560029 };
        constexpr auto kSupportedRuntime = SKSE::RUNTIME_SSE_1_5_97;
        constexpr std::uint8_t kMaxDeferredAttempts = 3;
        constexpr std::size_t kBoolSurfaceEntryPatchSize = 8;
        constexpr auto kExpectedBoolSurfaceEntryWindow =
            REL::make_pattern<"48 83 EC 28 48 8B 49 70 48 85 C9 74 11">();
        constexpr std::array<std::string_view, 17> kMenuRefreshAllowlist{
            "Main Menu",
            "InventoryMenu", "Inventory Menu",
            "MagicMenu", "Magic Menu",
            "MapMenu", "Map Menu",
            "JournalMenu", "Journal Menu",
            "ContainerMenu", "Container Menu",
            "BarterMenu", "Barter Menu",
            "Crafting Menu",
            "Book Menu",
            "Dialogue Menu",
            "Lockpicking Menu"
        };

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

        const char* ToLogString(MenuRefreshEligibility eligibility)
        {
            switch (eligibility) {
            case MenuRefreshEligibility::NotMenu:
                return "NotMenu";
            case MenuRefreshEligibility::EligibleStableMenu:
                return "EligibleStableMenu";
            case MenuRefreshEligibility::ObserverPartial:
                return "ObserverPartial";
            case MenuRefreshEligibility::ObserverUnavailable:
                return "ObserverUnavailable";
            case MenuRefreshEligibility::IdentityDegraded:
                return "IdentityDegraded";
            case MenuRefreshEligibility::NoStableTarget:
            default:
                return "NoStableTarget";
            }
        }

        std::uint32_t ToDirtyBits(PresentationDirtyFlags flags)
        {
            return static_cast<std::uint32_t>(static_cast<std::uint8_t>(flags));
        }

        void InspectMenuPresentationReadiness(RE::IMenu& menu, LiveMenuRefreshTarget& live)
        {
            if (!menu.uiMovie) {
                return;
            }

            RE::GFxValue root;
            live.rootReady = menu.uiMovie->GetVariable(&root, "_root") && root.IsObject();
            if (!live.rootReady) {
                return;
            }

            RE::GFxValue callback;
            live.ownedCallbackReady =
                menu.uiMovie->GetVariable(&callback, "_root.DualPad_OnPresentationChanged") &&
                !callback.IsUndefined() &&
                !callback.IsNull();
        }

        void NotifyMenuPresentationChanged(RE::IMenu& menu)
        {
            menu.uiMovie->InvokeNoReturn("_root.DualPad_OnPresentationChanged", nullptr, 0);
        }

        bool IsInstalledStatus(HookInstallStatus status)
        {
            return status == HookInstallStatus::Success ||
                status == HookInstallStatus::AlreadyInstalled;
        }

        bool IsFailClosedStatus(HookInstallStatus status)
        {
            return status == HookInstallStatus::UnsafePartial;
        }

        input::patching::HookOperationalState OperationalStateFor(HookInstallStatus status)
        {
            if (IsInstalledStatus(status)) {
                return input::patching::HookOperationalState::Installed;
            }
            if (status == HookInstallStatus::UnsafePartial) {
                return input::patching::HookOperationalState::UnsafePartial;
            }
            if (status == HookInstallStatus::NotAttempted) {
                return input::patching::HookOperationalState::Disabled;
            }
            return input::patching::HookOperationalState::SafePassthrough;
        }

        input::patching::HookFailureDisposition FailureDispositionFor(HookInstallStatus status)
        {
            if (status == HookInstallStatus::PartialInstall) {
                return input::patching::HookFailureDisposition::RolledBack;
            }
            if (status == HookInstallStatus::UnsafePartial) {
                return input::patching::HookFailureDisposition::FailClosed;
            }
            if (status == HookInstallStatus::UnsupportedRuntime ||
                status == HookInstallStatus::SignatureMismatch ||
                status == HookInstallStatus::Failed) {
                return input::patching::HookFailureDisposition::NotRequired;
            }
            return input::patching::HookFailureDisposition::None;
        }

        bool IsInstallAttemptFailureStatus(HookInstallStatus status)
        {
            return status == HookInstallStatus::UnsupportedRuntime ||
                status == HookInstallStatus::SignatureMismatch ||
                status == HookInstallStatus::Failed ||
                status == HookInstallStatus::PartialInstall ||
                status == HookInstallStatus::UnsafePartial;
        }

        input::patching::Bytes ReadPatchBytes(std::uintptr_t address, std::size_t size)
        {
            input::patching::Bytes bytes(size);
            std::memcpy(bytes.data(), reinterpret_cast<const void*>(address), size);
            return bytes;
        }

        bool CompareWritePatchBytes(
            std::uintptr_t address,
            const input::patching::Bytes& expected,
            const input::patching::Bytes& desired)
        {
            if (expected.empty() || expected.size() != desired.size()) {
                return false;
            }
            return REL::safe_write(
                address,
                desired.data(),
                desired.size(),
                expected.data(),
                expected.size());
        }

        std::uintptr_t AllocateVerifiedTrampolineBytes(const input::patching::Bytes& bytes)
        {
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

        std::uintptr_t AllocateAbsoluteJumpStub(std::uintptr_t destination)
        {
            return AllocateVerifiedTrampolineBytes(input::patching::MakeAbsoluteJump(destination));
        }

        std::uintptr_t AllocateEntryGateway(
            std::uintptr_t source,
            const input::patching::Bytes& original)
        {
            auto gateway = original;
            const auto resume = input::patching::MakeAbsoluteJump(source + original.size());
            gateway.insert(gateway.end(), resume.begin(), resume.end());
            return AllocateVerifiedTrampolineBytes(gateway);
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

        HookInstallResult VerifyHookSites(
            std::uintptr_t usingGamepadAddress,
            std::uintptr_t gamepadHandlerVtblAddress,
            std::string_view probePhase)
        {
            const auto identityManifest = ProductionEngineHookIdentityManifest();
            if (!REL::verify_code(usingGamepadAddress, kExpectedBoolSurfaceEntryWindow)) {
                return detail::MakeHookInstallResult(
                    HookInstallStatus::SignatureMismatch,
                    "is_using_gamepad_entry_signature_mismatch");
            }
            EngineHookIdentityObservation observed{
                .moduleBase = REL::Module::get().base(),
                .resolvedQueryAddress = usingGamepadAddress,
                .resolvedHandlerVtableAddress = gamepadHandlerVtblAddress
            };
            const auto queryBytes = ReadPatchBytes(
                usingGamepadAddress,
                kEngineQueryIdentityByteCount);
            std::copy(queryBytes.begin(), queryBytes.end(), observed.queryBytes.begin());
            for (std::size_t slot = 0; slot < observed.handlerVtableTargets.size(); ++slot) {
                const auto slotBytes = ReadPatchBytes(
                    gamepadHandlerVtblAddress + sizeof(std::uintptr_t) * slot,
                    sizeof(std::uintptr_t));
                std::memcpy(
                    &observed.handlerVtableTargets[slot],
                    slotBytes.data(),
                    sizeof(std::uintptr_t));
            }
            if (auto* manager = RE::BSInputDeviceManager::GetSingleton(); manager) {
                if (auto* handler = manager->GetGamepadHandler(); handler) {
                    observed.runtimeGamepadDeviceAddress =
                        reinterpret_cast<std::uintptr_t>(handler);
                    std::memcpy(
                        &observed.runtimeGamepadDeviceVtableAddress,
                        handler,
                        sizeof(std::uintptr_t));
                }
            }
            const auto probe = BuildEngineHookIdentityProbe(identityManifest, observed);
            logger::info(
                "[DualPad][SkyrimCompat][I0Probe] phase={} {}",
                probePhase,
                ToDebugString(probe));
            if (!identityManifest.i0Approved) {
                return detail::MakeHookInstallResult(
                    HookInstallStatus::SignatureMismatch,
                    ToString(EngineHookIdentityStatus::GateNotApproved));
            }
            const auto identity = VerifyEngineHookIdentity(identityManifest, observed);
            if (!identity.verified()) {
                return detail::MakeHookInstallResult(
                    HookInstallStatus::SignatureMismatch,
                    ToString(identity.status));
            }
            return detail::MakeHookInstallResult(HookInstallStatus::Success, "hook_sites_verified");
        }
    }

    bool IsMenuRefreshTargetAllowlisted(std::string_view menuName) noexcept
    {
        return std::find(kMenuRefreshAllowlist.begin(), kMenuRefreshAllowlist.end(), menuName) !=
            kMenuRefreshAllowlist.end();
    }

    MenuRefreshTarget MakeMenuRefreshTarget(const PublishedPresentationState& state)
    {
        return {
            .menuName = state.targetMenuName,
            .instanceId = state.targetMenuInstanceId,
            .menuPtr = state.targetMenuPtr,
            .moviePtr = state.targetMenuMoviePtr,
            .menuStackRevision = state.menuStackRevision,
            .contextRevision = state.contextRevision,
            .presentationEpoch = state.epoch
        };
    }

    MenuRefreshTargetValidation ValidateMenuRefreshTarget(
        const MenuRefreshTarget& captured,
        const MenuRefreshTarget& current,
        const LiveMenuRefreshTarget& live) noexcept
    {
        if (!IsMenuRefreshTargetAllowlisted(captured.menuName) ||
            captured.instanceId == 0 ||
            captured.menuPtr == 0 ||
            captured.moviePtr == 0) {
            return MenuRefreshTargetValidation::Disallowed;
        }
        if (captured != current) {
            return MenuRefreshTargetValidation::Superseded;
        }
        if (!live.uiAvailable || live.menuPtr == 0 || live.moviePtr == 0 || !live.rootReady) {
            return MenuRefreshTargetValidation::DeferredNotReady;
        }
        if (live.menuPtr != captured.menuPtr || live.moviePtr != captured.moviePtr) {
            return MenuRefreshTargetValidation::Superseded;
        }
        return live.ownedCallbackReady ?
            MenuRefreshTargetValidation::ReadyOwnedCallback :
            MenuRefreshTargetValidation::ReadyRefreshPlatform;
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
                .operationalState = OperationalStateFor(status),
                .disposition = FailureDispositionFor(status),
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

        HookInstallResult EvaluateHookTransactionResult(
            input::patching::PatchTransactionOutcome outcome,
            std::string_view debugReason)
        {
            switch (outcome) {
            case input::patching::PatchTransactionOutcome::Installed:
                return MakeHookInstallResult(HookInstallStatus::Success, debugReason);
            case input::patching::PatchTransactionOutcome::RolledBack:
                return MakeHookInstallResult(HookInstallStatus::PartialInstall, debugReason);
            case input::patching::PatchTransactionOutcome::UnsafePartial:
                return MakeHookInstallResult(HookInstallStatus::UnsafePartial, debugReason);
            case input::patching::PatchTransactionOutcome::FailedNoWrite:
            default:
                return MakeHookInstallResult(HookInstallStatus::Failed, debugReason);
            }
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
        case HookInstallStatus::UnsafePartial:
            return "unsafe_partial";
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
            " operational_state=" +
            input::patching::ToString(result.operationalState) +
            " disposition=" +
            input::patching::ToString(result.disposition) +
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

        bool patchResiduePossible = false;
        try {
            REL::Relocation<std::uintptr_t> usingGamepadHook{ kIsUsingGamepadId };
            REL::Relocation<std::uintptr_t> gamepadHandlerVtbl{ kGamepadHandlerVtblId };

            auto gate = VerifyHookSites(
                usingGamepadHook.address(),
                gamepadHandlerVtbl.address(),
                "plugin_load");
            if (IsInstallAttemptFailureStatus(gate.status)) {
                gate = MarkInstallFailed(gate);
                logger::error(
                    "[DualPad][SkyrimCompat] Hook signature gate failed: {}",
                    ToDebugString(gate));
                return gate;
            }

            const auto usingAddress = usingGamepadHook.address();

            const auto originalUsingBytes = ReadPatchBytes(usingAddress, kBoolSurfaceEntryPatchSize);

            const auto usingGateway = AllocateEntryGateway(usingAddress, originalUsingBytes);
            const auto usingHookStub = AllocateAbsoluteJumpStub(
                reinterpret_cast<std::uintptr_t>(StaticIsUsingGamepadHook));
            const auto usingReplacement = input::patching::MakeRelativePatch(
                usingAddress,
                usingHookStub,
                input::patching::RelativePatchOpcode::Jump,
                kBoolSurfaceEntryPatchSize);
            if (usingGateway == 0 || usingReplacement.empty()) {
                auto result = detail::MakeHookInstallResult(
                    HookInstallStatus::Failed,
                    "transaction_preparation_failed");
                result = MarkInstallFailed(result);
                logger::error("[DualPad][SkyrimCompat] Hook transaction preparation failed: {}", ToDebugString(result));
                return result;
            }

            _originalUsingGamepadTarget.store(usingGateway, std::memory_order_release);

            std::vector<input::patching::PatchSite> sites;
            const auto addSite = [&sites](
                                     std::string name,
                                     std::uintptr_t address,
                                     input::patching::Bytes original,
                                     input::patching::Bytes replacement) {
                const auto size = original.size();
                sites.push_back(input::patching::PatchSite{
                    .name = std::move(name),
                    .original = std::move(original),
                    .replacement = std::move(replacement),
                    .read = [address, size]() { return ReadPatchBytes(address, size); },
                    .compareWrite = [address](const auto& expected, const auto& desired) {
                        return CompareWritePatchBytes(address, expected, desired);
                    }
                });
            };
            addSite("is_using_gamepad_entry", usingAddress, originalUsingBytes, usingReplacement);

            const auto transaction = input::patching::ExecutePatchTransaction(sites);
            patchResiduePossible =
                transaction.outcome == input::patching::PatchTransactionOutcome::Installed ||
                transaction.outcome == input::patching::PatchTransactionOutcome::UnsafePartial;
            if (transaction.outcome != input::patching::PatchTransactionOutcome::Installed) {
                auto result = detail::EvaluateHookTransactionResult(
                    transaction.outcome,
                    std::string(input::patching::ToString(transaction.outcome)) +
                        ":" + transaction.failedSite);
                if (transaction.outcome != input::patching::PatchTransactionOutcome::UnsafePartial) {
                    _originalUsingGamepadTarget.store(0, std::memory_order_release);
                }
                result = MarkInstallFailed(result);
                logger::error(
                    "[DualPad][SkyrimCompat] Hook transaction failed applied={} rolledBack={}: {}",
                    transaction.appliedSites,
                    transaction.rolledBackSites,
                    ToDebugString(result));
                return result;
            }

            auto result = MarkInstallSucceeded();
            logger::info(
                "[DualPad][SkyrimCompat] Installed verified original-first engine query gateway: {}",
                ToDebugString(result));
            return result;
        } catch (...) {
            auto result = detail::MakeHookInstallResult(
                patchResiduePossible ? HookInstallStatus::UnsafePartial : HookInstallStatus::Failed,
                patchResiduePossible ?
                    "exception_with_possible_patch_residue" :
                    "exception_before_transaction_commit");
            result = MarkInstallFailed(result);
            logger::error("[DualPad][SkyrimCompat] Hook install failed: {}", ToDebugString(result));
            return result;
        }
    }

    void SkyrimCompatibilitySurface::Commit(const PublishedPresentationState& state)
    {
        std::scoped_lock lock(_mutex);
        _committed = state;
        CaptureMenuRefreshIntentLocked();
    }

    void SkyrimCompatibilitySurface::CommitPreOutputGameplayPresentationHandoff(PresentationOwner owner)
    {
        std::scoped_lock lock(_mutex);
        if (_committed.gameplayMenuEntryIntentOwner != owner) {
            _committed.gameplayMenuEntryIntentOwner = owner;
            ++_committed.gameplayMenuEntryIntentRevision;
        }
        logger::info(
            "[DualPad][PresentationHandoff] event=menu_entry_intent intentOwner={} owner={} navigationOwner={} cursorOwner={} epoch={} contextRevision={} gameplayPresentationRevision={}",
            ToLogString(_committed.gameplayMenuEntryIntentOwner),
            ToLogString(_committed.owner),
            ToLogString(_committed.navigationOwner),
            ToLogString(_committed.cursorOwner),
            _committed.epoch,
            _committed.contextRevision,
            _committed.gameplayPresentationRevision);
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
        return SkyrimEngineModeRouter{}.DecideWithOriginal(
            [this]() { return CallOriginalIsUsingGamepad(); });
    }

    bool SkyrimCompatibilitySurface::GamepadControlsCursorHook() const
    {
        return CallOriginalGamepadControlsCursor();
    }

    bool SkyrimCompatibilitySurface::IsGamepadDeviceEnabledHook(bool remapMode) const
    {
        (void)remapMode;
        return CallOriginalGamepadDeviceEnabled(nullptr);
    }

    bool SkyrimCompatibilitySurface::ShouldRefreshMenus()
    {
        std::scoped_lock lock(_mutex);
        return HasSchedulableMenuRefreshLocked();
    }

    bool SkyrimCompatibilitySurface::RefreshMenusIfNeeded()
    {
        const auto queued = QueueMenuRefreshTask();
        if (!queued) {
            return false;
        }

        const auto state = GetCommittedState();
        logger::info(
            "[DualPad][MenuRefreshTrace] event=request queued=true target={} instance={} menuStackRevision={} owner={} navigationOwner={} cursorOwner={} eligibility={} epoch={} dirty=0x{:02X} contextRevision={} gameplayPresentationRevision={}",
            state.targetMenuName,
            state.targetMenuInstanceId,
            state.menuStackRevision,
            ToLogString(state.owner),
            ToLogString(state.navigationOwner),
            ToLogString(state.cursorOwner),
            ToLogString(state.menuRefreshEligibility),
            state.epoch,
            ToDirtyBits(state.dirty),
            state.contextRevision,
            state.gameplayPresentationRevision);
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
        CompleteRefreshRequestForTests(MenuRefreshExecutionResult::Completed);
    }

    void SkyrimCompatibilitySurface::DeferQueuedRefreshForTests()
    {
        CompleteRefreshRequestForTests(MenuRefreshExecutionResult::DeferredNotReady);
    }

    std::string SkyrimCompatibilitySurface::MakeRefreshKeyForTests(const PublishedPresentationState& state) const
    {
        return MakeRefreshKey(state);
    }

    void SkyrimCompatibilitySurface::ResetInstallStateForTests()
    {
        std::scoped_lock lock(_mutex);
        _installState = detail::InstallState::NotInstalled;
        _installResult = HookInstallResult{};
        _hooksEnabled.store(true, std::memory_order_release);
        _originalUsingGamepadTarget.store(0, std::memory_order_release);
    }

    void SkyrimCompatibilitySurface::ResetRefreshStateForTests()
    {
        std::scoped_lock lock(_mutex);
        _nextRefreshSerial = 0;
        _lastRefreshQueuedEpoch = 0;
        _lastRefreshCompletedEpoch = 0;
        _lastRefreshQueuedKey.clear();
        _lastRefreshCompletedKey.clear();
        _lastDeferredHeldRequeueKey.clear();
        _refreshInFlight.reset();
        _refreshPendingLatest.reset();
        _refreshDeferredHeld.reset();
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
        MenuRefreshRequest request;
        {
            std::scoped_lock lock(_mutex);
            if (!HasSchedulableMenuRefreshLocked()) {
                return false;
            }
            _refreshInFlight = *_refreshPendingLatest;
            _refreshPendingLatest.reset();
            request = *_refreshInFlight;
            _lastRefreshQueuedEpoch = request.epoch;
            _lastRefreshQueuedKey = request.key;
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
            if (_refreshInFlight && _refreshInFlight->serial == request.serial) {
                if (!_refreshPendingLatest) {
                    _refreshPendingLatest = request;
                }
                _refreshInFlight.reset();
                _lastRefreshQueuedEpoch = 0;
                _lastRefreshQueuedKey.clear();
            }
        }
        logger::info("[DualPad][MenuRefreshTrace] event=skipped reason=no_skse_task_interface");
        return false;
    }

    bool SkyrimCompatibilitySurface::HasSchedulableMenuRefreshLocked() const
    {
        return _refreshPendingLatest.has_value() && !_refreshInFlight.has_value();
    }

    void SkyrimCompatibilitySurface::CaptureMenuRefreshIntentLocked()
    {
        if (!IsRefreshableMenuPresentation(_committed)) {
            _refreshPendingLatest.reset();
            _refreshDeferredHeld.reset();
            _lastDeferredHeldRequeueKey.clear();
            return;
        }
        const auto key = MakeRefreshKey(_committed);
        if (key == _lastRefreshCompletedKey) {
            if (_refreshDeferredHeld && _refreshDeferredHeld->key == key) {
                _refreshDeferredHeld.reset();
            }
            if (_lastDeferredHeldRequeueKey == key) {
                _lastDeferredHeldRequeueKey.clear();
            }
            return;
        }

        if (_refreshDeferredHeld) {
            if (_refreshDeferredHeld->key != key) {
                _refreshDeferredHeld.reset();
                _lastDeferredHeldRequeueKey.clear();
            } else if (!_refreshInFlight && !_refreshPendingLatest) {
                if (!HasRefreshRelevantDirty(_committed.dirty) &&
                    _lastDeferredHeldRequeueKey == key) {
                    return;
                }
                auto retry = *_refreshDeferredHeld;
                retry.serial = ++_nextRefreshSerial;
                retry.deferredAttempts = kMaxDeferredAttempts;
                _refreshPendingLatest = std::move(retry);
                _lastDeferredHeldRequeueKey = key;
                _refreshDeferredHeld.reset();
                return;
            }
        }

        if (_committed.epoch == 0 || !HasRefreshRelevantDirty(_committed.dirty)) {
            return;
        }

        if (_refreshInFlight && _refreshInFlight->key == key) {
            return;
        }
        if (_refreshPendingLatest && _refreshPendingLatest->key == key) {
            return;
        }

        _refreshPendingLatest = MakeRefreshRequestLocked();
    }

    SkyrimCompatibilitySurface::MenuRefreshRequest SkyrimCompatibilitySurface::MakeRefreshRequestLocked(
        std::uint8_t deferredAttempts)
    {
        return MenuRefreshRequest{
            .serial = ++_nextRefreshSerial,
            .epoch = _committed.epoch,
            .key = MakeRefreshKey(_committed),
            .target = MakeMenuRefreshTarget(_committed),
            .requestedDirty = _committed.dirty,
            .deferredAttempts = deferredAttempts
        };
    }

    bool SkyrimCompatibilitySurface::IsRefreshableMenuPresentation(const PublishedPresentationState& state)
    {
        return state.menuRefreshEligibility == MenuRefreshEligibility::EligibleStableMenu &&
            state.targetMenuInstanceId != 0 &&
            state.targetMenuPtr != 0 &&
            state.targetMenuMoviePtr != 0 &&
            IsMenuRefreshTargetAllowlisted(state.targetMenuName);
    }

    std::string SkyrimCompatibilitySurface::MakeRefreshKey(const PublishedPresentationState& state)
    {
        std::ostringstream out;
        out << state.epoch
            << "|ctx=" << state.contextRevision
            << "|stack=" << state.menuStackRevision
            << "|target=" << state.targetMenuName
            << "|instance=" << state.targetMenuInstanceId
            << "|menuPtr=" << state.targetMenuPtr
            << "|moviePtr=" << state.targetMenuMoviePtr
            << "|ui=" << static_cast<std::uint16_t>(state.uiContextId)
            << "|eligibility=" << static_cast<unsigned>(state.menuRefreshEligibility)
            << "|policy=" << state.presentationPolicyId
            << "|base=" << state.actionSetStack.baseSetId
            << "|layers=";
        for (const auto& layer : state.actionSetStack.layerIds) {
            out << layer << ',';
        }
        out << "|anchors=";
        for (const auto& anchor : state.actionSetStack.scopeAnchorIds) {
            out << anchor << ',';
        }
        return out.str();
    }

    const char* SkyrimCompatibilitySurface::ToString(MenuRefreshExecutionResult result)
    {
        switch (result) {
        case MenuRefreshExecutionResult::Completed:
            return "Completed";
        case MenuRefreshExecutionResult::DeferredNotReady:
            return "DeferredNotReady";
        case MenuRefreshExecutionResult::Superseded:
        default:
            return "Superseded";
        }
    }

    void SkyrimCompatibilitySurface::RecordI0RuntimeIdentityProbe() const
    {
        if (REL::Module::get().version() != kSupportedRuntime) {
            logger::warn(
                "[DualPad][SkyrimCompat][I0Probe] phase=data_loaded unsupported_runtime={}",
                REL::Module::get().version().string());
            return;
        }
        try {
            REL::Relocation<std::uintptr_t> usingGamepadHook{ kIsUsingGamepadId };
            REL::Relocation<std::uintptr_t> gamepadHandlerVtbl{ kGamepadHandlerVtblId };
            const auto result = VerifyHookSites(
                usingGamepadHook.address(),
                gamepadHandlerVtbl.address(),
                "data_loaded");
            if (result.debugReason !=
                ::dualpad::input_v2::presentation::ToString(
                    EngineHookIdentityStatus::GateNotApproved)) {
                logger::warn(
                    "[DualPad][SkyrimCompat][I0Probe] phase=data_loaded observation_failed={}",
                    result.debugReason);
            }
        } catch (const std::exception& error) {
            logger::error(
                "[DualPad][SkyrimCompat][I0Probe] phase=data_loaded exception={}",
                error.what());
        } catch (...) {
            logger::error(
                "[DualPad][SkyrimCompat][I0Probe] phase=data_loaded unknown_exception");
        }
    }

    void SkyrimCompatibilitySurface::CompleteRefreshRequestForTests(MenuRefreshExecutionResult result)
    {
        MenuRefreshRequest request;
        {
            std::scoped_lock lock(_mutex);
            if (!_refreshInFlight) {
                return;
            }
            request = *_refreshInFlight;
        }
        const auto refreshed = result == MenuRefreshExecutionResult::Completed ? 1U : 0U;
        const auto skippedNotReady = result == MenuRefreshExecutionResult::DeferredNotReady ? 1U : 0U;
        CompleteRefreshRequest(request, result, refreshed, 0, skippedNotReady);
    }

    void SkyrimCompatibilitySurface::CompleteRefreshRequest(
        const MenuRefreshRequest& request,
        MenuRefreshExecutionResult result,
        std::size_t refreshed,
        std::size_t notified,
        std::size_t skippedNotReady)
    {
        bool scheduleNext = false;
        bool heldDeferred = false;
        std::uint8_t nextAttempt = request.deferredAttempts;
        {
            std::scoped_lock lock(_mutex);
            if (!_refreshInFlight || _refreshInFlight->serial != request.serial) {
                logger::info(
                    "[DualPad][MenuRefreshTrace] event=stale serial={} result={}",
                    request.serial,
                    ToString(result));
                return;
            }

            _refreshInFlight.reset();
            if (result == MenuRefreshExecutionResult::Completed) {
                _lastRefreshCompletedEpoch = request.epoch;
                _lastRefreshCompletedKey = request.key;
                if (_refreshDeferredHeld && _refreshDeferredHeld->key == request.key) {
                    _refreshDeferredHeld.reset();
                }
            } else if (result == MenuRefreshExecutionResult::DeferredNotReady) {
                if (request.deferredAttempts < kMaxDeferredAttempts) {
                    auto retry = request;
                    retry.serial = ++_nextRefreshSerial;
                    retry.deferredAttempts = static_cast<std::uint8_t>(request.deferredAttempts + 1);
                    nextAttempt = retry.deferredAttempts;
                    if (!_refreshPendingLatest || _refreshPendingLatest->key == request.key) {
                        _refreshPendingLatest = std::move(retry);
                    }
                } else if (!_refreshPendingLatest) {
                    auto held = request;
                    held.deferredAttempts = kMaxDeferredAttempts;
                    _refreshDeferredHeld = std::move(held);
                    heldDeferred = true;
                }
            } else if (_refreshDeferredHeld && _refreshDeferredHeld->key == request.key) {
                _refreshDeferredHeld.reset();
            }

            if (_refreshPendingLatest && _refreshDeferredHeld) {
                _refreshDeferredHeld.reset();
            }

            if (!IsRefreshableMenuPresentation(_committed)) {
                _refreshDeferredHeld.reset();
                _lastDeferredHeldRequeueKey.clear();
            } else if (_refreshDeferredHeld &&
                MakeRefreshKey(_committed) != _refreshDeferredHeld->key) {
                _refreshDeferredHeld.reset();
                _lastDeferredHeldRequeueKey.clear();
            }

            if (heldDeferred && !_refreshDeferredHeld) {
                heldDeferred = false;
            }

            scheduleNext = _refreshPendingLatest.has_value();
        }

        if (heldDeferred) {
            logger::info(
                "[DualPad][MenuRefreshTrace] event=deferred_held serial={} epoch={} key='{}'",
                request.serial,
                request.epoch,
                request.key);
        }

        logger::info(
            "[DualPad][MenuRefreshTrace] event=complete serial={} result={} refreshed={} notified={} skippedNotReady={} retryAttempt={}",
            request.serial,
            ToString(result),
            refreshed,
            notified,
            skippedNotReady,
            nextAttempt);

        if (scheduleNext) {
            (void)QueueMenuRefreshTask();
        }
    }

    bool SkyrimCompatibilitySurface::CallOriginalIsUsingGamepad(void* self) const
    {
        using Hook = bool (*)(void*);
        if (const auto target = _originalUsingGamepadTarget.load(std::memory_order_acquire);
            target != 0 && self != nullptr) {
            return reinterpret_cast<Hook>(target)(self);
        }
        std::scoped_lock lock(_mutex);
        return _originalHookOutputs.isUsingGamepad;
    }

    bool SkyrimCompatibilitySurface::CallOriginalGamepadControlsCursor(void* self) const
    {
        (void)self;
        std::scoped_lock lock(_mutex);
        return _originalHookOutputs.gamepadControlsCursor;
    }

    bool SkyrimCompatibilitySurface::CallOriginalGamepadDeviceEnabled(RE::BSPCGamepadDeviceHandler* device) const
    {
        (void)device;
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

    bool SkyrimCompatibilitySurface::StaticIsUsingGamepadHook(void* self)
    {
        auto& surface = GetSingleton();
        return SkyrimEngineModeRouter{}.DecideWithOriginal(
            [&surface, self]() { return surface.CallOriginalIsUsingGamepad(self); });
    }

    void SkyrimCompatibilitySurface::DoRefreshMenus()
    {
        auto& surface = GetSingleton();
        std::size_t refreshed = 0;
        std::size_t notified = 0;
        std::size_t skippedNotReady = 0;
        MenuRefreshRequest request;

        {
            std::scoped_lock lock(surface._mutex);
            if (!surface._refreshInFlight) {
                logger::info("[DualPad][MenuRefreshTrace] event=skipped reason=no_inflight_request");
                return;
            }
            request = *surface._refreshInFlight;
        }

        const auto stateAtStart = surface.GetCommittedState();
        if (!IsRefreshableMenuPresentation(stateAtStart) ||
            MakeRefreshKey(stateAtStart) != request.key) {
            logger::info(
                "[DualPad][MenuRefreshTrace] event=skipped reason=superseded serial={} uiContext={} eligibility={} epoch={} dirty=0x{:02X}",
                request.serial,
                static_cast<std::uint16_t>(stateAtStart.uiContextId),
                ToLogString(stateAtStart.menuRefreshEligibility),
                stateAtStart.epoch,
                ToDirtyBits(stateAtStart.dirty));
            surface.CompleteRefreshRequest(
                request,
                MenuRefreshExecutionResult::Superseded,
                refreshed,
                notified,
                skippedNotReady);
            return;
        }

        RE::GPtr<RE::IMenu> targetMenu;
        LiveMenuRefreshTarget live{};
        if (auto* ui = RE::UI::GetSingleton(); ui) {
            live.uiAvailable = true;
            targetMenu = ui->GetMenu(request.target.menuName);
            if (targetMenu) {
                live.menuPtr = reinterpret_cast<std::uintptr_t>(targetMenu.get());
                if (targetMenu->uiMovie) {
                    live.moviePtr = reinterpret_cast<std::uintptr_t>(targetMenu->uiMovie.get());
                    InspectMenuPresentationReadiness(*targetMenu, live);
                }
            }
        }

        const auto stateBeforeInvoke = surface.GetCommittedState();
        const auto validation = ValidateMenuRefreshTarget(
            request.target,
            MakeMenuRefreshTarget(stateBeforeInvoke),
            live);
        auto result = MenuRefreshExecutionResult::Completed;
        if (!IsRefreshableMenuPresentation(stateBeforeInvoke) ||
            MakeRefreshKey(stateBeforeInvoke) != request.key) {
            result = MenuRefreshExecutionResult::Superseded;
        }
        switch (result == MenuRefreshExecutionResult::Superseded ?
                    MenuRefreshTargetValidation::Superseded :
                    validation) {
        case MenuRefreshTargetValidation::ReadyOwnedCallback:
            NotifyMenuPresentationChanged(*targetMenu);
            ++notified;
            break;
        case MenuRefreshTargetValidation::ReadyRefreshPlatform:
            targetMenu->RefreshPlatform();
            ++refreshed;
            break;
        case MenuRefreshTargetValidation::DeferredNotReady:
            ++skippedNotReady;
            result = MenuRefreshExecutionResult::DeferredNotReady;
            break;
        case MenuRefreshTargetValidation::Disallowed:
        case MenuRefreshTargetValidation::Superseded:
        default:
            result = MenuRefreshExecutionResult::Superseded;
            break;
        }

        const auto stateAtEnd = surface.GetCommittedState();
        if (!IsRefreshableMenuPresentation(stateAtEnd) ||
            MakeRefreshKey(stateAtEnd) != request.key ||
            MakeMenuRefreshTarget(stateAtEnd) != request.target) {
            result = MenuRefreshExecutionResult::Superseded;
        }

        logger::info(
            "[DualPad][MenuRefreshTrace] event=done serial={} result={} target={} instance={} menuStackRevision={} refreshed={} notified={} skippedNotReady={} owner={} navigationOwner={} cursorOwner={} epoch={} requestedDirty=0x{:02X} currentDirty=0x{:02X}",
            request.serial,
            ToString(result),
            request.target.menuName,
            request.target.instanceId,
            request.target.menuStackRevision,
            refreshed,
            notified,
            skippedNotReady,
            ToLogString(stateAtEnd.owner),
            ToLogString(stateAtEnd.navigationOwner),
            ToLogString(stateAtEnd.cursorOwner),
            stateAtEnd.epoch,
            ToDirtyBits(request.requestedDirty),
            ToDirtyBits(stateAtEnd.dirty));
        surface.CompleteRefreshRequest(request, result, refreshed, notified, skippedNotReady);
    }
}
