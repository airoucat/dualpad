#pragma once

#include "input_v2/gameplay/EngineModeProjection.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace dualpad::input_v2::presentation
{
    inline constexpr std::uint64_t kIsUsingGamepadRelocationId = 67320;
    inline constexpr std::uint64_t kGamepadHandlerCompleteObjectLocatorRelocationId = 560029;
    inline constexpr std::uint64_t kGamepadHandlerVtableRelocationId = 285457;
    inline constexpr std::size_t kEngineQueryIdentityByteCount = 32;
    inline constexpr std::size_t kHandlerVtableIdentitySlotCount = 10;
    inline constexpr std::size_t kInvalidDeviceVfuncSlot = static_cast<std::size_t>(-1);

    struct EngineHookIdentityManifest
    {
        std::uint64_t queryRelocationId{ kIsUsingGamepadRelocationId };
        std::uintptr_t expectedQueryRva{ 0 };
        std::array<std::uint8_t, kEngineQueryIdentityByteCount> expectedQueryBytes{};
        std::uint64_t handlerCompleteObjectLocatorRelocationId{
            kGamepadHandlerCompleteObjectLocatorRelocationId
        };
        std::uint64_t handlerVtableRelocationId{ kGamepadHandlerVtableRelocationId };
        std::uintptr_t expectedHandlerCompleteObjectLocatorTargetRva{ 0 };
        std::size_t approvedDeviceVfuncSlot{ kInvalidDeviceVfuncSlot };
        std::uintptr_t approvedDeviceVfuncTarget{ 0 };
        bool i0Approved{ false };
    };

    struct EngineHookIdentityObservation
    {
        std::uint64_t queryRelocationId{ kIsUsingGamepadRelocationId };
        std::uint64_t handlerCompleteObjectLocatorRelocationId{
            kGamepadHandlerCompleteObjectLocatorRelocationId
        };
        std::uint64_t handlerVtableRelocationId{ kGamepadHandlerVtableRelocationId };
        std::uintptr_t moduleBase{ 0 };
        std::uintptr_t resolvedQueryAddress{ 0 };
        std::array<std::uint8_t, kEngineQueryIdentityByteCount> queryBytes{};
        std::uintptr_t resolvedHandlerCompleteObjectLocatorAddress{ 0 };
        std::uintptr_t handlerCompleteObjectLocatorTarget{ 0 };
        std::uintptr_t resolvedHandlerVtableAddress{ 0 };
        std::array<std::uintptr_t, kHandlerVtableIdentitySlotCount> handlerVtableTargets{};
        std::uintptr_t runtimeGamepadDeviceAddress{ 0 };
        std::uintptr_t runtimeGamepadDeviceVtableAddress{ 0 };
    };

    enum class EngineHookIdentityStatus : std::uint8_t
    {
        GateNotApproved = 0,
        RelocationIdMismatch,
        QueryRvaMismatch,
        QueryBytesMismatch,
        HandlerCompleteObjectLocatorMissing,
        HandlerCompleteObjectLocatorMismatch,
        HandlerVtableMissing,
        RuntimeDeviceMissing,
        RuntimeHandlerVtableMismatch,
        DeviceSlotMismatch,
        DeviceTargetMissing,
        DeviceTargetAmbiguous,
        Verified
    };

    struct EngineHookIdentityResult
    {
        EngineHookIdentityStatus status{ EngineHookIdentityStatus::GateNotApproved };
        std::size_t deviceVfuncSlot{ kInvalidDeviceVfuncSlot };

        [[nodiscard]] bool verified() const noexcept
        {
            return status == EngineHookIdentityStatus::Verified;
        }
    };

    struct EngineHookIdentityProbe
    {
        EngineHookIdentityStatus verificationStatus{
            EngineHookIdentityStatus::GateNotApproved
        };
        bool staticQueryIdentityMatched{ false };
        bool handlerCompleteObjectLocatorMatched{ false };
        bool runtimeGamepadDevicePresent{ false };
        bool runtimeHandlerVtableMatched{ false };
        bool patchEligible{ false };
        std::uintptr_t queryRva{ 0 };
        std::uintptr_t handlerCompleteObjectLocatorRva{ 0 };
        std::uintptr_t handlerCompleteObjectLocatorTargetRva{ 0 };
        std::uintptr_t handlerVtableRva{ 0 };
        std::uintptr_t runtimeGamepadDeviceAddress{ 0 };
        std::uintptr_t runtimeGamepadDeviceVtableRva{ 0 };
        std::array<std::uintptr_t, kHandlerVtableIdentitySlotCount>
            handlerVfuncTargetRvas{};
    };

    [[nodiscard]] EngineHookIdentityResult VerifyEngineHookIdentity(
        const EngineHookIdentityManifest& manifest,
        const EngineHookIdentityObservation& observed) noexcept;

    [[nodiscard]] EngineHookIdentityProbe BuildEngineHookIdentityProbe(
        const EngineHookIdentityManifest& manifest,
        const EngineHookIdentityObservation& observed) noexcept;

    [[nodiscard]] const char* ToString(EngineHookIdentityStatus status) noexcept;
    [[nodiscard]] std::string ToDebugString(const EngineHookIdentityProbe& probe);

    // Production remains unapproved until I-0 records the exact entry bytes,
    // handler slot and original target from the supported 1.5.97 host.
    [[nodiscard]] EngineHookIdentityManifest ProductionEngineHookIdentityManifest() noexcept;

    class SkyrimEngineModeRouter
    {
    public:
        [[nodiscard]] bool DecideWithOriginal(
            const std::function<bool()>& originalGateway) const;

        [[nodiscard]] bool DecideForSnapshot(
            std::uintptr_t returnAddress,
            const gameplay::EngineModeDecisionSnapshot& snapshot,
            bool originalValue) const noexcept;

        [[nodiscard]] gameplay::EngineQueryDomain ClassifyDirectCaller(
            std::uintptr_t returnAddress) const noexcept;

        [[nodiscard]] class EngineQueryScope EnterScopedOverride(
            gameplay::EngineQueryDomain domain,
            gameplay::EngineInputMode mode,
            std::uint64_t ownerTickToken,
            std::uint32_t contextRevision) noexcept;

        [[nodiscard]] class EngineQueryScope EnterEventLocalLookOverride(
            gameplay::EngineInputMode mode) noexcept;
    };

    class EngineQueryScope
    {
    public:
        EngineQueryScope() noexcept = default;
        EngineQueryScope(const EngineQueryScope&) = delete;
        EngineQueryScope& operator=(const EngineQueryScope&) = delete;
        EngineQueryScope(EngineQueryScope&& other) noexcept;
        EngineQueryScope& operator=(EngineQueryScope&& other) noexcept;
        ~EngineQueryScope();

    private:
        friend class SkyrimEngineModeRouter;
        explicit EngineQueryScope(std::uint64_t scopeId) noexcept : _scopeId(scopeId) {}
        void Release() noexcept;

        std::uint64_t _scopeId{ 0 };
    };

    struct EngineCallerShadowManifest
    {
        std::array<gameplay::EngineCallerRule, 26> rules{};
        std::size_t populatedRuleCount{ 0 };
        std::size_t releaseRelevantUnknownCount{ 26 };
        bool i1Approved{ false };
    };

    [[nodiscard]] bool CallerShadowTableReleaseReady(
        const EngineCallerShadowManifest& manifest) noexcept;
    [[nodiscard]] EngineCallerShadowManifest ProductionEngineCallerShadowManifest() noexcept;
}
