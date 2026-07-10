#include "pch.h"
#include "input_v2/context/ContextResolver.h"

#include <format>
#include <sstream>

namespace logger = SKSE::log;

namespace dualpad::input_v2::context
{
    namespace
    {
        constexpr bool IsLegacyMenuOwnedContext(dualpad::input::InputContext context)
        {
            const auto value = static_cast<std::uint16_t>(context);
            return (value >= 100 && value < 2000) || context == dualpad::input::InputContext::Console;
        }

        constexpr bool ShouldAdvanceLegacyEpoch(
            dualpad::input::InputContext previous,
            dualpad::input::InputContext next)
        {
            if (previous == next) {
                return false;
            }
            if (!IsLegacyMenuOwnedContext(previous) &&
                !IsLegacyMenuOwnedContext(next)) {
                return false;
            }
            return true;
        }

        bool HasSameResolutionFields(const ResolvedContextSnapshot& lhs, const ResolvedContextSnapshot& rhs)
        {
            return lhs.hostMode == rhs.hostMode &&
                lhs.gameplaySubstate == rhs.gameplaySubstate &&
                lhs.uiContextId == rhs.uiContextId &&
                lhs.topMenuInstanceId == rhs.topMenuInstanceId &&
                lhs.identityQuality == rhs.identityQuality &&
                lhs.menuObserverCompleteness == rhs.menuObserverCompleteness &&
                lhs.menuIdentityDegraded == rhs.menuIdentityDegraded &&
                lhs.menuStackRevision == rhs.menuStackRevision &&
                lhs.actionSetStack == rhs.actionSetStack &&
                lhs.presentationPolicyId == rhs.presentationPolicyId &&
                lhs.legacyInputContext == rhs.legacyInputContext &&
                lhs.legacyContextEpoch == rhs.legacyContextEpoch;
        }

        const char* ToString(HostMode mode)
        {
            switch (mode) {
            case HostMode::Menu:
                return "Menu";
            case HostMode::Gameplay:
            default:
                return "Gameplay";
            }
        }

        const char* ToString(menu::MenuIdentityQuality quality)
        {
            switch (quality) {
            case menu::MenuIdentityQuality::StablePointer:
                return "StablePointer";
            case menu::MenuIdentityQuality::FingerprintRebound:
                return "FingerprintRebound";
            case menu::MenuIdentityQuality::DegradedIdentity:
            default:
                return "DegradedIdentity";
            }
        }

        std::string Join(const std::vector<std::string>& values)
        {
            if (values.empty()) {
                return "<none>";
            }

            std::ostringstream out;
            for (std::size_t i = 0; i < values.size(); ++i) {
                if (i > 0) {
                    out << ',';
                }
                out << values[i];
            }
            return out.str();
        }

        std::string ContextName(const CompiledContextCatalog& catalog, UiContextId id)
        {
            if (const auto* entry = ContextCatalog::FindById(catalog, id)) {
                return entry->canonicalContextName;
            }
            return std::format("UiContext#{}", static_cast<std::uint16_t>(id));
        }

        std::string TopMenuName(const menu::ReconciledMenuStack& menuStack)
        {
            if (menuStack.trackedMenus.empty()) {
                return "<none>";
            }
            return menuStack.trackedMenus.front().menuName;
        }
    }

    ContextResolver& ContextResolver::GetSingleton()
    {
        static ContextResolver instance;
        return instance;
    }

    ResolvedContextSnapshot ContextResolver::ResolveAndPublish(
        const menu::ReconciledMenuStack& menuStack,
        GameplaySubstate gameplaySubstate,
        const CompiledContextCatalog& catalog)
    {
        std::scoped_lock lock(_mutex);
        ResolvedContextSnapshot next{};
        next.gameplaySubstate = gameplaySubstate;
        next.menuStackRevision = menuStack.menuStackRevision;
        next.menuObserverCompleteness = menuStack.lastCompleteness;
        next.legacyContextEpoch = _published.legacyContextEpoch == 0 ? 1 : _published.legacyContextEpoch;

        if (!menuStack.trackedMenus.empty()) {
            const auto& top = menuStack.trackedMenus.front();
            next.hostMode = HostMode::Menu;
            next.topMenuInstanceId = top.instanceId;
            next.identityQuality = top.identityQuality;
            next.menuIdentityDegraded = top.identityQuality == menu::MenuIdentityQuality::DegradedIdentity;

            auto resolved = ContextCatalog::ResolveMenuName(catalog, top.menuName);
            if (!resolved || top.identityQuality == menu::MenuIdentityQuality::DegradedIdentity) {
                resolved = UiContextId::UnknownTrackedMenu;
            }
            next.uiContextId = *resolved;
        }
        else if (menuStack.lastCompleteness != menu::ObserverCompleteness::Complete) {
            next.hostMode = HostMode::Menu;
            next.identityQuality = menu::MenuIdentityQuality::DegradedIdentity;
            next.menuIdentityDegraded = true;
            next.uiContextId = UiContextId::UnknownTrackedMenu;
        }
        else {
            next.hostMode = HostMode::Gameplay;
            next.uiContextId = UiContextIdFromGameplaySubstate(gameplaySubstate);
        }

        const auto* entry = ContextCatalog::FindById(catalog, next.uiContextId);
        if (!entry) {
            entry = ContextCatalog::FindById(catalog, UiContextId::UnknownTrackedMenu);
            next.uiContextId = UiContextId::UnknownTrackedMenu;
        }

        if (entry) {
            next.presentationPolicyId = entry->presentationPolicyId;
            if (entry->legacyInputContext) {
                next.legacyInputContext = *entry->legacyInputContext;
            }
            else {
                next.legacyInputContext = next.hostMode == HostMode::Gameplay ?
                    dualpad::input::InputContext::Gameplay :
                    dualpad::input::InputContext::Menu;
            }
        }
        else {
            next.presentationPolicyId = "Menu";
            next.legacyInputContext = dualpad::input::InputContext::Menu;
        }

        next.actionSetStack = actions::ActionSetResolver::Resolve(catalog, next.uiContextId);
        if (ShouldAdvanceLegacyEpoch(_published.legacyInputContext, next.legacyInputContext)) {
            ++next.legacyContextEpoch;
        }

        if (!HasSameResolutionFields(_published, next)) {
            next.contextRevision = _published.contextRevision + 1;
            logger::debug(
                "[DualPad][ContextResolve] revision={} host={} topMenu={} topInstance={} identity={} uiContext={} legacyCtx={} legacyEpoch={} menuStackRevision={} actionBase={} layers={} policy={}",
                next.contextRevision,
                ToString(next.hostMode),
                TopMenuName(menuStack),
                next.topMenuInstanceId.value_or(0),
                ToString(next.identityQuality),
                ContextName(catalog, next.uiContextId),
                dualpad::input::ToString(next.legacyInputContext),
                next.legacyContextEpoch,
                next.menuStackRevision,
                next.actionSetStack.baseSetId.empty() ? "<none>" : next.actionSetStack.baseSetId,
                Join(next.actionSetStack.layerIds),
                next.presentationPolicyId.empty() ? "<none>" : next.presentationPolicyId);
            _published = next;
        }
        return _published;
    }

    ResolvedContextSnapshot ContextResolver::GetPublishedSnapshot() const
    {
        std::scoped_lock lock(_mutex);
        return _published;
    }

    void ContextResolver::PublishSnapshotForReplayTests(ResolvedContextSnapshot snapshot)
    {
        std::scoped_lock lock(_mutex);
        _published = std::move(snapshot);
    }

    void ContextResolver::ResetForTests()
    {
        std::scoped_lock lock(_mutex);
        _published = ResolvedContextSnapshot{};
    }

    GameplaySubstate ContextResolver::GameplaySubstateFromLegacy(dualpad::input::InputContext context)
    {
        switch (context) {
        case dualpad::input::InputContext::Combat:
            return GameplaySubstate::Combat;
        case dualpad::input::InputContext::Sneaking:
            return GameplaySubstate::Sneaking;
        case dualpad::input::InputContext::Riding:
            return GameplaySubstate::Riding;
        case dualpad::input::InputContext::Werewolf:
            return GameplaySubstate::Werewolf;
        case dualpad::input::InputContext::VampireLord:
            return GameplaySubstate::VampireLord;
        case dualpad::input::InputContext::Death:
            return GameplaySubstate::Death;
        case dualpad::input::InputContext::Bleedout:
            return GameplaySubstate::Bleedout;
        case dualpad::input::InputContext::Ragdoll:
            return GameplaySubstate::Ragdoll;
        case dualpad::input::InputContext::KillMove:
            return GameplaySubstate::KillMove;
        default:
            return GameplaySubstate::None;
        }
    }

    UiContextId ContextResolver::UiContextIdFromGameplaySubstate(GameplaySubstate substate)
    {
        switch (substate) {
        case GameplaySubstate::Combat:
            return UiContextId::Combat;
        case GameplaySubstate::Sneaking:
            return UiContextId::Sneaking;
        case GameplaySubstate::Riding:
            return UiContextId::Riding;
        case GameplaySubstate::Werewolf:
            return UiContextId::Werewolf;
        case GameplaySubstate::VampireLord:
            return UiContextId::VampireLord;
        case GameplaySubstate::Death:
            return UiContextId::Death;
        case GameplaySubstate::Bleedout:
            return UiContextId::Bleedout;
        case GameplaySubstate::Ragdoll:
            return UiContextId::Ragdoll;
        case GameplaySubstate::KillMove:
            return UiContextId::KillMove;
        case GameplaySubstate::None:
        default:
            return UiContextId::None;
        }
    }

    LegacyContextMirrorState ContextResolver::ToLegacyMirror(const ResolvedContextSnapshot& snapshot)
    {
        return LegacyContextMirrorState{
            .context = snapshot.legacyInputContext,
            .epoch = snapshot.legacyContextEpoch
        };
    }

    ShadowCompareResult ContextResolver::CompareShadowRecords(
        const ShadowCompareRecord& expected,
        const ShadowCompareRecord& actual)
    {
        ShadowCompareResult result{ .passes = true };
        const auto addDiff = [&](std::string diff) {
            result.passes = false;
            result.diffs.push_back(std::move(diff));
        };

        if (expected.topMenuInstanceId != actual.topMenuInstanceId) {
            addDiff("topMenuInstanceId");
        }
        if (expected.identityQuality != actual.identityQuality) {
            addDiff("identityQuality");
        }
        if (expected.menuStackRevision != actual.menuStackRevision) {
            addDiff("menuStackRevision");
        }
        if (expected.uiContextId != actual.uiContextId) {
            addDiff("uiContextId");
        }
        if (!(expected.actionSetStack == actual.actionSetStack)) {
            addDiff("ActionSetStack");
        }
        if (expected.presentationPolicyId != actual.presentationPolicyId) {
            addDiff("presentationPolicyId");
        }
        if (expected.legacyInputContext != actual.legacyInputContext) {
            addDiff("legacyInputContext");
        }
        if (expected.legacyContextEpoch != actual.legacyContextEpoch) {
            addDiff("legacyContextEpoch");
        }
        if (expected.contextRevision != actual.contextRevision) {
            addDiff("contextRevision");
        }
        return result;
    }
}
