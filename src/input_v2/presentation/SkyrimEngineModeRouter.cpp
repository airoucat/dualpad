#include "pch.h"

#include "input_v2/presentation/SkyrimEngineModeRouter.h"

namespace dualpad::input_v2::presentation
{
    EngineHookIdentityResult VerifyEngineHookIdentity(
        const EngineHookIdentityManifest& manifest,
        const EngineHookIdentityObservation& observed) noexcept
    {
        if (!manifest.i0Approved) {
            return { .status = EngineHookIdentityStatus::GateNotApproved };
        }
        if (manifest.queryRelocationId != kIsUsingGamepadRelocationId ||
            manifest.handlerVtableRelocationId != kGamepadHandlerVtableRelocationId ||
            observed.queryRelocationId != manifest.queryRelocationId ||
            observed.handlerVtableRelocationId != manifest.handlerVtableRelocationId) {
            return { .status = EngineHookIdentityStatus::RelocationIdMismatch };
        }
        if (observed.moduleBase == 0 || manifest.expectedQueryRva == 0 ||
            observed.resolvedQueryAddress != observed.moduleBase + manifest.expectedQueryRva) {
            return { .status = EngineHookIdentityStatus::QueryRvaMismatch };
        }
        if (observed.queryBytes != manifest.expectedQueryBytes) {
            return { .status = EngineHookIdentityStatus::QueryBytesMismatch };
        }
        if (observed.resolvedHandlerVtableAddress == 0) {
            return { .status = EngineHookIdentityStatus::HandlerVtableMissing };
        }
        if (manifest.approvedDeviceVfuncTarget == 0) {
            return { .status = EngineHookIdentityStatus::DeviceTargetMissing };
        }

        std::size_t matchedSlot = kInvalidDeviceVfuncSlot;
        std::size_t matchCount = 0;
        for (const auto slot : { std::size_t{ 7 }, std::size_t{ 8 } }) {
            if (observed.handlerVtableTargets[slot] == manifest.approvedDeviceVfuncTarget) {
                matchedSlot = slot;
                ++matchCount;
            }
        }
        if (matchCount == 0) {
            return { .status = EngineHookIdentityStatus::DeviceTargetMissing };
        }
        if (matchCount != 1) {
            return { .status = EngineHookIdentityStatus::DeviceTargetAmbiguous };
        }
        return {
            .status = EngineHookIdentityStatus::Verified,
            .deviceVfuncSlot = matchedSlot
        };
    }

    EngineHookIdentityManifest ProductionEngineHookIdentityManifest() noexcept
    {
        return EngineHookIdentityManifest{
            .expectedQueryRva = 0xC15240,
            .i0Approved = false
        };
    }

    const char* ToString(EngineHookIdentityStatus status) noexcept
    {
        switch (status) {
        case EngineHookIdentityStatus::GateNotApproved:
            return "i0_gate_not_approved";
        case EngineHookIdentityStatus::RelocationIdMismatch:
            return "relocation_id_mismatch";
        case EngineHookIdentityStatus::QueryRvaMismatch:
            return "query_rva_mismatch";
        case EngineHookIdentityStatus::QueryBytesMismatch:
            return "query_bytes_mismatch";
        case EngineHookIdentityStatus::HandlerVtableMissing:
            return "handler_vtable_missing";
        case EngineHookIdentityStatus::DeviceTargetMissing:
            return "device_target_missing";
        case EngineHookIdentityStatus::DeviceTargetAmbiguous:
            return "device_target_ambiguous";
        case EngineHookIdentityStatus::Verified:
            return "verified";
        }
        return "unknown";
    }

    bool SkyrimEngineModeRouter::DecideWithOriginal(
        const std::function<bool()>& originalGateway) const
    {
        return originalGateway ? originalGateway() : false;
    }
}
