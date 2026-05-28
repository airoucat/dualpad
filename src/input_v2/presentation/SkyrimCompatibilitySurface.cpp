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
        if (_installed) {
            return;
        }

        REL::Relocation<std::uintptr_t> usingGamepadHook{ kIsUsingGamepadId, kIsUsingGamepadCallOffset };
        SKSE::GetTrampoline().write_call<6>(usingGamepadHook.address(), StaticIsUsingGamepadHook);

        REL::Relocation<std::uintptr_t> cursorHook{ kGamepadControlsCursorId, kGamepadControlsCursorCallOffset };
        SKSE::GetTrampoline().write_call<6>(cursorHook.address(), StaticIsGamepadCursorHook);

        REL::Relocation<std::uintptr_t> gamepadHandlerVtbl{ kGamepadHandlerVtblId };
        REL::Relocation<std::uintptr_t> gamepadHandlerHook{
            gamepadHandlerVtbl.address() + (kGamepadIsEnabledVfuncIndex * sizeof(std::uintptr_t))
        };
        gamepadHandlerHook.write_vfunc(kGamepadIsEnabledVfuncIndex, StaticIsGamepadDeviceEnabledHook);

        _installed = true;
        logger::info("[DualPad][SkyrimCompat] Installed input_v2 public surface hooks");
    }

    void SkyrimCompatibilitySurface::Commit(const PublishedPresentationState& state)
    {
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
        return _committed.owner == PresentationOwner::Gamepad;
    }

    bool SkyrimCompatibilitySurface::GamepadControlsCursorHook() const
    {
        return _committed.cursorOwner == CursorOwner::Gamepad;
    }

    bool SkyrimCompatibilitySurface::IsGamepadDeviceEnabledHook(bool remapMode) const
    {
        if (!remapMode) {
            return true;
        }
        return _committed.owner == PresentationOwner::Gamepad;
    }

    bool SkyrimCompatibilitySurface::ShouldRefreshMenus()
    {
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
        PresentationParityRecord record{
            .contextRevision = _committed.contextRevision,
            .deviceFamilyRevision = _committed.deviceFamilyRevision,
            .gameplayPresentationRevision = _committed.gameplayPresentationRevision,
            .epoch = _committed.epoch,
            .reason = _committed.reason
        };

        const bool projectedIsUsingGamepad = _committed.owner == PresentationOwner::Gamepad;
        const bool projectedCursor = _committed.cursorOwner == CursorOwner::Gamepad;
        const bool projectedDeviceEnabled =
            !remapMode || _committed.owner == PresentationOwner::Gamepad;

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

    const PublishedPresentationState& SkyrimCompatibilitySurface::GetCommittedState() const
    {
        return _committed;
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
