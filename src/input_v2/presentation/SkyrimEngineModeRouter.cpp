#include "pch.h"

#include "input_v2/presentation/SkyrimEngineModeRouter.h"

#include <algorithm>
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
    }
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
        if (domain == gameplay::EngineQueryDomain::Unknown ||
            domain == gameplay::EngineQueryDomain::Remap ||
            mode == gameplay::EngineInputMode::Original ||
            ownerTickToken == 0 || contextRevision == 0 ||
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
