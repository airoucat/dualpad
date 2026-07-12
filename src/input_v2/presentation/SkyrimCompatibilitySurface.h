#pragma once

#include <RE/Skyrim.h>

#include "input_v2/presentation/PresentationProjection.h"
#include "input/injection/HookPatchTransaction.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dualpad::input_v2::presentation
{
    struct MenuRefreshTarget
    {
        std::string menuName;
        menu::MenuInstanceId instanceId{ 0 };
        std::uintptr_t menuPtr{ 0 };
        std::uintptr_t moviePtr{ 0 };
        std::uint32_t menuStackRevision{ 0 };
        std::uint32_t contextRevision{ 0 };
        std::uint32_t presentationEpoch{ 0 };

        friend bool operator==(const MenuRefreshTarget&, const MenuRefreshTarget&) = default;
    };

    struct LiveMenuRefreshTarget
    {
        bool uiAvailable{ false };
        std::uintptr_t menuPtr{ 0 };
        std::uintptr_t moviePtr{ 0 };
        bool rootReady{ false };
        bool ownedCallbackReady{ false };
    };

    enum class MenuRefreshTargetValidation : std::uint8_t
    {
        ReadyOwnedCallback = 0,
        ReadyRefreshPlatform,
        DeferredNotReady,
        Superseded,
        Disallowed
    };

    bool IsMenuRefreshTargetAllowlisted(std::string_view menuName) noexcept;
    MenuRefreshTarget MakeMenuRefreshTarget(const PublishedPresentationState& state);
    MenuRefreshTargetValidation ValidateMenuRefreshTarget(
        const MenuRefreshTarget& captured,
        const MenuRefreshTarget& current,
        const LiveMenuRefreshTarget& live) noexcept;

    namespace detail
    {
        enum class InstallState : std::uint8_t
        {
            NotInstalled = 0,
            Installing,
            Installed,
            Failed
        };

        struct VfuncPatchSite
        {
            std::uintptr_t relocationBase{ 0 };
            std::size_t index{ 0 };
        };

        constexpr VfuncPatchSite MakeVfuncPatchSite(
            std::uintptr_t vtableBase,
            std::size_t index)
        {
            return VfuncPatchSite{
                .relocationBase = vtableBase,
                .index = index
            };
        }

        constexpr bool CanBeginInstall(InstallState state)
        {
            return state == InstallState::NotInstalled;
        }

        constexpr InstallState BeginInstall(InstallState state)
        {
            return CanBeginInstall(state) ? InstallState::Installing : state;
        }

        constexpr InstallState CompleteInstall(InstallState state)
        {
            return state == InstallState::Installing ? InstallState::Installed : state;
        }

        constexpr InstallState FailInstall(InstallState state)
        {
            return state == InstallState::Installing ? InstallState::Failed : state;
        }
    }

    enum class HookInstallStatus : std::uint8_t
    {
        NotAttempted = 0,
        Success,
        UnsupportedRuntime,
        SignatureMismatch,
        AlreadyInstalled,
        Failed,
        PartialInstall,
        UnsafePartial
    };

    struct HookInstallResult
    {
        HookInstallStatus status{ HookInstallStatus::NotAttempted };
        bool installed{ false };
        bool failClosed{ false };
        input::patching::HookOperationalState operationalState{
            input::patching::HookOperationalState::Disabled
        };
        input::patching::HookFailureDisposition disposition{
            input::patching::HookFailureDisposition::None
        };
        std::string debugReason;
    };

    namespace detail
    {
        HookInstallResult MakeHookInstallResult(
            HookInstallStatus status,
            std::string_view debugReason);

        HookInstallResult EvaluateHookInstallGate(
            bool runtimeSupported,
            bool signaturesMatch,
            std::string_view debugReason = {});

        HookInstallResult EvaluateHookTransactionResult(
            input::patching::PatchTransactionOutcome outcome,
            std::string_view debugReason);
    }

    bool IsHookInstallFailure(const HookInstallResult& result);
    const char* ToString(HookInstallStatus status);
    std::string ToDebugString(const HookInstallResult& result);

    struct LegacyCompatibilitySurface
    {
        bool isUsingGamepad{ false };
        bool gamepadControlsCursor{ false };
        bool gamepadDeviceEnabled{ false };
    };

    struct PresentationParityRecord
    {
        bool passes{ false };
        std::uint32_t contextRevision{ 0 };
        std::uint32_t deviceFamilyRevision{ 0 };
        std::uint32_t gameplayPresentationRevision{ 0 };
        std::uint32_t epoch{ 0 };
        PresentationDecisionReason reason{ PresentationDecisionReason::None };
        std::vector<std::string> diffs;
    };

    class SkyrimCompatibilitySurface
    {
    public:
        using MenuRefreshTask = void (*)();
        using MenuRefreshTaskSink = std::function<bool(MenuRefreshTask)>;

        static SkyrimCompatibilitySurface& GetSingleton();

        HookInstallResult Install();
        void Commit(const PublishedPresentationState& state);
        void CommitPreOutputGameplayPresentationHandoff(PresentationOwner owner);
        void EnableRollback(const LegacyCompatibilitySurface& legacy);
        void DisableRollback();

        bool IsUsingGamepadHook() const;
        bool GamepadControlsCursorHook() const;
        bool IsGamepadDeviceEnabledHook(bool remapMode) const;
        bool ShouldRefreshMenus();
        bool RefreshMenusIfNeeded();
        PresentationParityRecord CompareShadowParity(
            const LegacyCompatibilitySurface& legacy,
            bool remapMode) const;

        PublishedPresentationState GetCommittedState() const;
        HookInstallResult GetInstallResult() const;
        void ForceInstallResultForTests(const HookInstallResult& result);
        void ForceOriginalHookOutputsForTests(const LegacyCompatibilitySurface& legacy);
        void ForceHooksEnabledForTests(bool enabled);
        void SetMenuRefreshTaskSinkForTests(MenuRefreshTaskSink sink);
        void CompleteQueuedRefreshForTests();
        void DeferQueuedRefreshForTests();
        std::string MakeRefreshKeyForTests(const PublishedPresentationState& state) const;
        void ResetInstallStateForTests();
        void ResetRefreshStateForTests();

    private:
        static bool StaticIsUsingGamepadHook(void* self);
        static void DoRefreshMenus();

        struct MenuRefreshRequest
        {
            std::uint64_t serial{ 0 };
            std::uint32_t epoch{ 0 };
            std::string key;
            MenuRefreshTarget target;
            PresentationDirtyFlags requestedDirty{ PresentationDirtyFlags::None };
            std::uint8_t deferredAttempts{ 0 };
        };

        enum class MenuRefreshExecutionResult : std::uint8_t
        {
            Completed = 0,
            DeferredNotReady,
            Superseded
        };

        bool TryBeginInstall();
        bool QueueMenuRefreshTask();
        bool HasSchedulableMenuRefreshLocked() const;
        void CaptureMenuRefreshIntentLocked();
        MenuRefreshRequest MakeRefreshRequestLocked(std::uint8_t deferredAttempts = 0);
        static bool IsRefreshableMenuPresentation(const PublishedPresentationState& state);
        static std::string MakeRefreshKey(const PublishedPresentationState& state);
        static const char* ToString(MenuRefreshExecutionResult result);
        void CompleteRefreshRequestForTests(MenuRefreshExecutionResult result);
        void CompleteRefreshRequest(
            const MenuRefreshRequest& request,
            MenuRefreshExecutionResult result,
            std::size_t refreshed,
            std::size_t notified,
            std::size_t skippedNotReady);
        bool CallOriginalIsUsingGamepad(void* self = nullptr) const;
        bool CallOriginalGamepadControlsCursor(void* self = nullptr) const;
        bool CallOriginalGamepadDeviceEnabled(RE::BSPCGamepadDeviceHandler* device) const;
        HookInstallResult MarkInstallResultLocked(const HookInstallResult& result);
        HookInstallResult MarkInstallSucceeded();
        HookInstallResult MarkInstallFailed(const HookInstallResult& result);
        detail::InstallState GetInstallState() const;

        mutable std::mutex _mutex;
        PublishedPresentationState _committed{};
        std::uint64_t _nextRefreshSerial{ 0 };
        std::uint32_t _lastRefreshQueuedEpoch{ 0 };
        std::uint32_t _lastRefreshCompletedEpoch{ 0 };
        std::string _lastRefreshQueuedKey;
        std::string _lastRefreshCompletedKey;
        std::string _lastDeferredHeldRequeueKey;
        std::optional<MenuRefreshRequest> _refreshInFlight;
        std::optional<MenuRefreshRequest> _refreshPendingLatest;
        std::optional<MenuRefreshRequest> _refreshDeferredHeld;
        MenuRefreshTaskSink _refreshTaskSinkForTests;
        detail::InstallState _installState{ detail::InstallState::NotInstalled };
        HookInstallResult _installResult{};
        std::atomic_bool _hooksEnabled{ true };
        std::atomic<std::uintptr_t> _originalUsingGamepadTarget{ 0 };
        LegacyCompatibilitySurface _originalHookOutputs{
            .isUsingGamepad = true,
            .gamepadControlsCursor = true,
            .gamepadDeviceEnabled = true
        };
    };
}
