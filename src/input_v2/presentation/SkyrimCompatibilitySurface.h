#pragma once

#include <RE/Skyrim.h>

#include "input_v2/presentation/PresentationProjection.h"

#include <cstdint>
#include <mutex>
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
        static SkyrimCompatibilitySurface& GetSingleton();

        void Install();
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

        PublishedPresentationState GetCommittedState() const;

    private:
        static bool StaticIsUsingGamepadHook();
        static bool StaticIsGamepadCursorHook();
        static bool StaticIsGamepadDeviceEnabledHook(RE::BSPCGamepadDeviceHandler* device);

        mutable std::mutex _mutex;
        PublishedPresentationState _committed{};
        std::uint32_t _lastRefreshEpoch{ 0 };
        bool _installed{ false };
    };
}
