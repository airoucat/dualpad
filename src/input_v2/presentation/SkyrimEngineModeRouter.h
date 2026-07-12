#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dualpad::input_v2::presentation
{
    inline constexpr std::uint64_t kIsUsingGamepadRelocationId = 67320;
    inline constexpr std::uint64_t kGamepadHandlerVtableRelocationId = 560029;
    inline constexpr std::size_t kEngineQueryIdentityByteCount = 32;
    inline constexpr std::size_t kHandlerVtableIdentitySlotCount = 10;
    inline constexpr std::size_t kInvalidDeviceVfuncSlot = static_cast<std::size_t>(-1);

    struct EngineHookIdentityManifest
    {
        std::uint64_t queryRelocationId{ kIsUsingGamepadRelocationId };
        std::uintptr_t expectedQueryRva{ 0 };
        std::array<std::uint8_t, kEngineQueryIdentityByteCount> expectedQueryBytes{};
        std::uint64_t handlerVtableRelocationId{ kGamepadHandlerVtableRelocationId };
        std::uintptr_t approvedDeviceVfuncTarget{ 0 };
        bool i0Approved{ false };
    };

    struct EngineHookIdentityObservation
    {
        std::uint64_t queryRelocationId{ kIsUsingGamepadRelocationId };
        std::uint64_t handlerVtableRelocationId{ kGamepadHandlerVtableRelocationId };
        std::uintptr_t moduleBase{ 0 };
        std::uintptr_t resolvedQueryAddress{ 0 };
        std::array<std::uint8_t, kEngineQueryIdentityByteCount> queryBytes{};
        std::uintptr_t resolvedHandlerVtableAddress{ 0 };
        std::array<std::uintptr_t, kHandlerVtableIdentitySlotCount> handlerVtableTargets{};
    };

    enum class EngineHookIdentityStatus : std::uint8_t
    {
        GateNotApproved = 0,
        RelocationIdMismatch,
        QueryRvaMismatch,
        QueryBytesMismatch,
        HandlerVtableMissing,
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

    [[nodiscard]] EngineHookIdentityResult VerifyEngineHookIdentity(
        const EngineHookIdentityManifest& manifest,
        const EngineHookIdentityObservation& observed) noexcept;

    [[nodiscard]] const char* ToString(EngineHookIdentityStatus status) noexcept;

    // Production remains unapproved until I-0 records the exact entry bytes,
    // handler slot and original target from the supported 1.5.97 host.
    [[nodiscard]] EngineHookIdentityManifest ProductionEngineHookIdentityManifest() noexcept;
}
