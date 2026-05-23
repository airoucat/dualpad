#pragma once

#include "input_v2/presentation/PresentationProjection.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dualpad::input_v2::presentation
{
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
        void Commit(const PublishedPresentationState& state);
        void EnableRollback(const LegacyCompatibilitySurface& legacy);
        void DisableRollback();

        bool IsUsingGamepadHook() const;
        bool GamepadControlsCursorHook() const;
        bool IsGamepadDeviceEnabledHook(bool remapMode) const;
        bool ShouldRefreshMenus();
        PresentationParityRecord CompareShadowParity(
            const LegacyCompatibilitySurface& legacy,
            bool remapMode) const;

        const PublishedPresentationState& GetCommittedState() const;

    private:
        PublishedPresentationState _committed{};
        std::optional<LegacyCompatibilitySurface> _rollback;
        std::uint32_t _lastRefreshEpoch{ 0 };
    };
}
