#include "pch.h"

#include "input_v2/presentation/SkyrimCompatibilitySurface.h"

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
    }

    SkyrimCompatibilitySurface& SkyrimCompatibilitySurface::GetSingleton()
    {
        static SkyrimCompatibilitySurface surface;
        return surface;
    }

    void SkyrimCompatibilitySurface::Install()
    {
        if (!TryBeginInstall()) {
            if (GetInstallState() == detail::InstallState::Failed) {
                logger::error("[DualPad][SkyrimCompat] Previous hook install failed; refusing silent retry");
            }
            return;
        }

        try {
            REL::Relocation<std::uintptr_t> usingGamepadHook{ kIsUsingGamepadId, kIsUsingGamepadCallOffset };
            SKSE::GetTrampoline().write_call<6>(usingGamepadHook.address(), StaticIsUsingGamepadHook);

            REL::Relocation<std::uintptr_t> cursorHook{ kGamepadControlsCursorId, kGamepadControlsCursorCallOffset };
            SKSE::GetTrampoline().write_call<6>(cursorHook.address(), StaticIsGamepadCursorHook);

            REL::Relocation<std::uintptr_t> gamepadHandlerVtbl{ kGamepadHandlerVtblId };
            const auto patchSite = detail::MakeVfuncPatchSite(
                gamepadHandlerVtbl.address(),
                kGamepadIsEnabledVfuncIndex);
            REL::Relocation<std::uintptr_t> gamepadHandlerHook{ patchSite.relocationBase };
            gamepadHandlerHook.write_vfunc(patchSite.index, StaticIsGamepadDeviceEnabledHook);

            MarkInstallSucceeded();
        } catch (...) {
            MarkInstallFailed();
            logger::error("[DualPad][SkyrimCompat] Hook install failed");
            throw;
        }

        logger::info("[DualPad][SkyrimCompat] Installed input_v2 public surface hooks");
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

    bool SkyrimCompatibilitySurface::TryBeginInstall()
    {
        std::scoped_lock lock(_mutex);
        if (!detail::CanBeginInstall(_installState)) {
            return false;
        }
        _installState = detail::BeginInstall(_installState);
        return true;
    }

    void SkyrimCompatibilitySurface::MarkInstallSucceeded()
    {
        std::scoped_lock lock(_mutex);
        _installState = detail::CompleteInstall(_installState);
    }

    void SkyrimCompatibilitySurface::MarkInstallFailed()
    {
        std::scoped_lock lock(_mutex);
        _installState = detail::FailInstall(_installState);
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
