#include "input_v2/presentation/SkyrimCompatibilitySurface.h"

namespace dualpad::input_v2::presentation
{
    void SkyrimCompatibilitySurface::Commit(const PublishedPresentationState& state)
    {
        _committed = state;
    }

    void SkyrimCompatibilitySurface::EnableRollback(const LegacyCompatibilitySurface& legacy)
    {
        _rollback = legacy;
    }

    void SkyrimCompatibilitySurface::DisableRollback()
    {
        _rollback.reset();
    }

    bool SkyrimCompatibilitySurface::IsUsingGamepadHook() const
    {
        if (_rollback) {
            return _rollback->isUsingGamepad;
        }
        return _committed.owner == PresentationOwner::Gamepad;
    }

    bool SkyrimCompatibilitySurface::GamepadControlsCursorHook() const
    {
        if (_rollback) {
            return _rollback->gamepadControlsCursor;
        }
        return _committed.cursorOwner == CursorOwner::Gamepad;
    }

    bool SkyrimCompatibilitySurface::IsGamepadDeviceEnabledHook(bool remapMode) const
    {
        if (!remapMode) {
            return true;
        }
        if (_rollback) {
            return _rollback->gamepadDeviceEnabled;
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
}
