#include "pch.h"

#include "input_v2/presentation/SkyrimEngineModeRouter.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <utility>

namespace dualpad::input_v2::presentation
{
    namespace
    {
        struct ScopedEngineDecision
        {
            std::uint64_t id{ 0 };
            gameplay::EngineQueryDomain domain{ gameplay::EngineQueryDomain::Unknown };
            gameplay::EngineInputMode mode{ gameplay::EngineInputMode::Original };
            std::uint64_t ownerTickToken{ 0 };
            std::uint32_t contextRevision{ 0 };
        };

        thread_local std::array<ScopedEngineDecision, 8> g_engineScopes{};
        thread_local std::size_t g_engineScopeDepth{ 0 };
        thread_local std::uint64_t g_nextEngineScopeId{ 0 };

        bool ModeValue(gameplay::EngineInputMode mode, bool originalValue) noexcept
        {
            switch (mode) {
            case gameplay::EngineInputMode::KeyboardMouse:
                return false;
            case gameplay::EngineInputMode::Gamepad:
                return true;
            case gameplay::EngineInputMode::Original:
                return originalValue;
            }
            return originalValue;
        }

        std::uintptr_t RelativeToModule(
            std::uintptr_t address,
            std::uintptr_t moduleBase) noexcept
        {
            return moduleBase != 0 && address >= moduleBase ? address - moduleBase : 0;
        }
    }
    EngineHookIdentityResult VerifyEngineHookIdentity(
        const EngineHookIdentityManifest& manifest,
        const EngineHookIdentityObservation& observed) noexcept
    {
        if (!manifest.i0Approved) {
            return { .status = EngineHookIdentityStatus::GateNotApproved };
        }
        if (manifest.queryRelocationId != kIsUsingGamepadRelocationId ||
            manifest.handlerCompleteObjectLocatorRelocationId !=
                kGamepadHandlerCompleteObjectLocatorRelocationId ||
            manifest.handlerVtableRelocationId != kGamepadHandlerVtableRelocationId ||
            observed.queryRelocationId != manifest.queryRelocationId ||
            observed.handlerCompleteObjectLocatorRelocationId !=
                manifest.handlerCompleteObjectLocatorRelocationId ||
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
        if (observed.resolvedHandlerCompleteObjectLocatorAddress == 0 ||
            observed.handlerCompleteObjectLocatorTarget == 0) {
            return {
                .status = EngineHookIdentityStatus::HandlerCompleteObjectLocatorMissing
            };
        }
        if (manifest.expectedHandlerCompleteObjectLocatorTargetRva == 0 ||
            observed.resolvedHandlerCompleteObjectLocatorAddress +
                    sizeof(std::uintptr_t) !=
                observed.resolvedHandlerVtableAddress ||
            observed.handlerCompleteObjectLocatorTarget !=
                observed.moduleBase +
                    manifest.expectedHandlerCompleteObjectLocatorTargetRva) {
            return {
                .status = EngineHookIdentityStatus::HandlerCompleteObjectLocatorMismatch
            };
        }
        if (observed.runtimeGamepadDeviceAddress == 0 ||
            observed.runtimeGamepadDeviceVtableAddress == 0) {
            return { .status = EngineHookIdentityStatus::RuntimeDeviceMissing };
        }
        if (observed.runtimeGamepadDeviceVtableAddress !=
            observed.resolvedHandlerVtableAddress) {
            return { .status = EngineHookIdentityStatus::RuntimeHandlerVtableMismatch };
        }
        if (manifest.approvedDeviceVfuncSlot != 7) {
            return { .status = EngineHookIdentityStatus::DeviceSlotMismatch };
        }
        if (manifest.approvedDeviceVfuncTarget == 0) {
            return { .status = EngineHookIdentityStatus::DeviceTargetMissing };
        }
        if (observed.handlerVtableTargets[manifest.approvedDeviceVfuncSlot] !=
            manifest.approvedDeviceVfuncTarget) {
            return {
                .status = observed.handlerVtableTargets[8] ==
                        manifest.approvedDeviceVfuncTarget ?
                    EngineHookIdentityStatus::DeviceSlotMismatch :
                    EngineHookIdentityStatus::DeviceTargetMissing
            };
        }
        if (observed.handlerVtableTargets[8] == manifest.approvedDeviceVfuncTarget) {
            return { .status = EngineHookIdentityStatus::DeviceTargetAmbiguous };
        }
        return {
            .status = EngineHookIdentityStatus::Verified,
            .deviceVfuncSlot = manifest.approvedDeviceVfuncSlot
        };
    }

    EngineHookIdentityManifest ProductionEngineHookIdentityManifest() noexcept
    {
        return EngineHookIdentityManifest{
            .expectedQueryRva = 0xC15240,
            .expectedQueryBytes = {
                0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x49, 0x70,
                0x48, 0x85, 0xC9, 0x74, 0x11, 0x48, 0x8B, 0x01,
                0xFF, 0x50, 0x38, 0x84, 0xC0, 0x74, 0x07, 0xB0,
                0x01, 0x48, 0x83, 0xC4, 0x28, 0xC3, 0x32, 0xC0
            },
            .expectedHandlerCompleteObjectLocatorTargetRva = 0x194DA20,
            .approvedDeviceVfuncSlot = 7,
            .i0Approved = false
        };
    }

    EngineHookIdentityProbe BuildEngineHookIdentityProbe(
        const EngineHookIdentityManifest& manifest,
        const EngineHookIdentityObservation& observed) noexcept
    {
        EngineHookIdentityProbe probe{
            .verificationStatus = VerifyEngineHookIdentity(manifest, observed).status,
            .staticQueryIdentityMatched = observed.moduleBase != 0 &&
                manifest.expectedQueryRva != 0 &&
                observed.queryRelocationId == manifest.queryRelocationId &&
                observed.handlerCompleteObjectLocatorRelocationId ==
                    manifest.handlerCompleteObjectLocatorRelocationId &&
                observed.handlerVtableRelocationId ==
                    manifest.handlerVtableRelocationId &&
                observed.resolvedQueryAddress ==
                    observed.moduleBase + manifest.expectedQueryRva &&
                observed.queryBytes == manifest.expectedQueryBytes,
            .handlerCompleteObjectLocatorMatched =
                observed.moduleBase != 0 &&
                observed.handlerCompleteObjectLocatorRelocationId ==
                    manifest.handlerCompleteObjectLocatorRelocationId &&
                observed.handlerVtableRelocationId ==
                    manifest.handlerVtableRelocationId &&
                manifest.expectedHandlerCompleteObjectLocatorTargetRva != 0 &&
                observed.resolvedHandlerCompleteObjectLocatorAddress != 0 &&
                observed.resolvedHandlerCompleteObjectLocatorAddress +
                        sizeof(std::uintptr_t) ==
                    observed.resolvedHandlerVtableAddress &&
                observed.handlerCompleteObjectLocatorTarget ==
                    observed.moduleBase +
                        manifest.expectedHandlerCompleteObjectLocatorTargetRva,
            .runtimeGamepadDevicePresent =
                observed.runtimeGamepadDeviceAddress != 0 &&
                observed.runtimeGamepadDeviceVtableAddress != 0,
            .runtimeHandlerVtableMatched =
                observed.runtimeGamepadDeviceVtableAddress != 0 &&
                observed.runtimeGamepadDeviceVtableAddress ==
                    observed.resolvedHandlerVtableAddress,
            .queryRva = RelativeToModule(
                observed.resolvedQueryAddress,
                observed.moduleBase),
            .handlerCompleteObjectLocatorRva = RelativeToModule(
                observed.resolvedHandlerCompleteObjectLocatorAddress,
                observed.moduleBase),
            .handlerCompleteObjectLocatorTargetRva = RelativeToModule(
                observed.handlerCompleteObjectLocatorTarget,
                observed.moduleBase),
            .handlerVtableRva = RelativeToModule(
                observed.resolvedHandlerVtableAddress,
                observed.moduleBase),
            .runtimeGamepadDeviceAddress = observed.runtimeGamepadDeviceAddress,
            .runtimeGamepadDeviceVtableRva = RelativeToModule(
                observed.runtimeGamepadDeviceVtableAddress,
                observed.moduleBase)
        };
        probe.patchEligible = probe.verificationStatus ==
            EngineHookIdentityStatus::Verified;
        for (std::size_t slot = 0; slot < probe.handlerVfuncTargetRvas.size(); ++slot) {
            probe.handlerVfuncTargetRvas[slot] = RelativeToModule(
                observed.handlerVtableTargets[slot],
                observed.moduleBase);
        }
        return probe;
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
        case EngineHookIdentityStatus::HandlerCompleteObjectLocatorMissing:
            return "handler_col_missing";
        case EngineHookIdentityStatus::HandlerCompleteObjectLocatorMismatch:
            return "handler_col_mismatch";
        case EngineHookIdentityStatus::HandlerVtableMissing:
            return "handler_vtable_missing";
        case EngineHookIdentityStatus::RuntimeDeviceMissing:
            return "runtime_gamepad_device_missing";
        case EngineHookIdentityStatus::RuntimeHandlerVtableMismatch:
            return "runtime_handler_vtable_mismatch";
        case EngineHookIdentityStatus::DeviceSlotMismatch:
            return "device_slot_mismatch";
        case EngineHookIdentityStatus::DeviceTargetMissing:
            return "device_target_missing";
        case EngineHookIdentityStatus::DeviceTargetAmbiguous:
            return "device_target_ambiguous";
        case EngineHookIdentityStatus::Verified:
            return "verified";
        }
        return "unknown";
    }

    std::string ToDebugString(const EngineHookIdentityProbe& probe)
    {
        std::ostringstream stream;
        stream << "verification=" << ToString(probe.verificationStatus)
               << " staticQueryIdentityMatched="
               << (probe.staticQueryIdentityMatched ? "true" : "false")
               << " handlerColMatched="
               << (probe.handlerCompleteObjectLocatorMatched ? "true" : "false")
               << " runtimeDevicePresent="
               << (probe.runtimeGamepadDevicePresent ? "true" : "false")
               << " runtimeVtableMatched="
               << (probe.runtimeHandlerVtableMatched ? "true" : "false")
               << " patchEligible=" << (probe.patchEligible ? "true" : "false")
               << std::hex << std::uppercase
               << " queryRva=0x" << probe.queryRva
               << " handlerColRva=0x" << probe.handlerCompleteObjectLocatorRva
               << " handlerColTargetRva=0x"
               << probe.handlerCompleteObjectLocatorTargetRva
               << " handlerVtableRva=0x" << probe.handlerVtableRva
               << " runtimeDevice=0x" << probe.runtimeGamepadDeviceAddress
               << " runtimeVtableRva=0x" << probe.runtimeGamepadDeviceVtableRva;
        for (std::size_t slot = 0; slot < probe.handlerVfuncTargetRvas.size(); ++slot) {
            stream << " slot" << slot << "TargetRva=0x"
                   << probe.handlerVfuncTargetRvas[slot];
        }
        return stream.str();
    }

    bool SkyrimEngineModeRouter::DecideWithOriginal(
        const std::function<bool()>& originalGateway) const
    {
        const bool originalValue = originalGateway ? originalGateway() : false;
        if (g_engineScopeDepth == 0) {
            return originalValue;
        }

        const auto& scoped = g_engineScopes[g_engineScopeDepth - 1];
        if (scoped.domain != gameplay::EngineQueryDomain::GameplayLookTransform ||
            scoped.ownerTickToken != 0 || scoped.contextRevision != 0) {
            return originalValue;
        }
        return ModeValue(scoped.mode, originalValue);
    }

    bool SkyrimEngineModeRouter::DecideForSnapshot(
        std::uintptr_t returnAddress,
        const gameplay::EngineModeDecisionSnapshot& snapshot,
        bool originalValue) const noexcept
    {
        (void)returnAddress;
        if (g_engineScopeDepth == 0) {
            return originalValue;
        }
        const auto& scoped = g_engineScopes[g_engineScopeDepth - 1];
        if (scoped.domain == gameplay::EngineQueryDomain::Unknown ||
            scoped.domain == gameplay::EngineQueryDomain::Remap ||
            scoped.mode == gameplay::EngineInputMode::Original) {
            return originalValue;
        }
        const auto index = static_cast<std::size_t>(scoped.domain);
        if (index >= snapshot.byDomain.size()) {
            return originalValue;
        }
        const auto& decision = snapshot.byDomain[index];
        if (decision.ownerTickToken == 0 ||
            decision.ownerTickToken != scoped.ownerTickToken ||
            decision.contextRevision != scoped.contextRevision ||
            decision.runtimeGeneration != snapshot.generation ||
            decision.domain != scoped.domain ||
            decision.mode != scoped.mode ||
            decision.causality == gameplay::EngineDecisionCausality::OriginalOnly) {
            return originalValue;
        }
        return ModeValue(scoped.mode, originalValue);
    }

    gameplay::EngineQueryDomain SkyrimEngineModeRouter::ClassifyDirectCaller(
        std::uintptr_t returnAddress) const noexcept
    {
        const auto manifest = ProductionEngineCallerShadowManifest();
        const auto populated = std::min(manifest.populatedRuleCount, manifest.rules.size());
        const auto found = std::find_if(
            manifest.rules.begin(),
            manifest.rules.begin() + populated,
            [returnAddress](const auto& rule) { return rule.callerRva == returnAddress; });
        return found == manifest.rules.begin() + populated ?
            gameplay::EngineQueryDomain::Unknown : found->domain;
    }

    EngineQueryScope SkyrimEngineModeRouter::EnterScopedOverride(
        gameplay::EngineQueryDomain domain,
        gameplay::EngineInputMode mode,
        std::uint64_t ownerTickToken,
        std::uint32_t contextRevision) noexcept
    {
        const bool eventLocalLook =
            domain == gameplay::EngineQueryDomain::GameplayLookTransform &&
            ownerTickToken == 0 && contextRevision == 0;
        if (domain == gameplay::EngineQueryDomain::Unknown ||
            domain == gameplay::EngineQueryDomain::Remap ||
            mode == gameplay::EngineInputMode::Original ||
            (!eventLocalLook && (ownerTickToken == 0 || contextRevision == 0)) ||
            g_engineScopeDepth == g_engineScopes.size()) {
            return {};
        }
        const auto id = ++g_nextEngineScopeId;
        g_engineScopes[g_engineScopeDepth++] = ScopedEngineDecision{
            .id = id,
            .domain = domain,
            .mode = mode,
            .ownerTickToken = ownerTickToken,
            .contextRevision = contextRevision
        };
        return EngineQueryScope{ id };
    }

    EngineQueryScope::EngineQueryScope(EngineQueryScope&& other) noexcept :
        _scopeId(std::exchange(other._scopeId, 0))
    {}

    EngineQueryScope& EngineQueryScope::operator=(EngineQueryScope&& other) noexcept
    {
        if (this != &other) {
            Release();
            _scopeId = std::exchange(other._scopeId, 0);
        }
        return *this;
    }

    EngineQueryScope::~EngineQueryScope()
    {
        Release();
    }

    void EngineQueryScope::Release() noexcept
    {
        if (_scopeId == 0) {
            return;
        }
        if (g_engineScopeDepth != 0 && g_engineScopes[g_engineScopeDepth - 1].id == _scopeId) {
            g_engineScopes[--g_engineScopeDepth] = ScopedEngineDecision{};
        } else {
            g_engineScopes.fill(ScopedEngineDecision{});
            g_engineScopeDepth = 0;
        }
        _scopeId = 0;
    }

    bool CallerShadowTableReleaseReady(
        const EngineCallerShadowManifest& manifest) noexcept
    {
        if (!manifest.i1Approved ||
            manifest.populatedRuleCount != manifest.rules.size() ||
            manifest.releaseRelevantUnknownCount != 0) {
            return false;
        }
        return std::all_of(manifest.rules.begin(), manifest.rules.end(), [](const auto& rule) {
            return rule.callerRva != 0 &&
                (rule.domain != gameplay::EngineQueryDomain::Unknown ||
                    rule.causality == gameplay::EngineDecisionCausality::OriginalOnly);
        });
    }

    EngineCallerShadowManifest ProductionEngineCallerShadowManifest() noexcept
    {
        return {};
    }
}
