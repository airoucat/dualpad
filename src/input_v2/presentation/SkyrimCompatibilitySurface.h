#pragma once

#include <RE/Skyrim.h>

#include "input_v2/presentation/PresentationProjection.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace dualpad::input_v2::presentation
{
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

        bool TryBeginInstall();
        void MarkInstallSucceeded();
        void MarkInstallFailed();
        detail::InstallState GetInstallState() const;

        mutable std::mutex _mutex;
        PublishedPresentationState _committed{};
        std::uint32_t _lastRefreshEpoch{ 0 };
        detail::InstallState _installState{ detail::InstallState::NotInstalled };
    };
}
