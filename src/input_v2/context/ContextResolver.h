#pragma once

#include "input_v2/compat/LegacyInputContextCompat.h"
#include "input_v2/actions/ActionSetResolver.h"
#include "input_v2/context/ContextCatalog.h"
#include "input_v2/menu/MenuInstanceRegistry.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dualpad::input_v2::context
{
    enum class HostMode : std::uint8_t
    {
        Gameplay = 0,
        Menu
    };

    enum class GameplaySubstate : std::uint8_t
    {
        None = 0,
        Combat,
        Sneaking,
        Riding,
        Werewolf,
        VampireLord,
        Death,
        Bleedout,
        Ragdoll,
        KillMove
    };

    using PresentationPolicyId = std::string;

    struct ResolvedContextSnapshot
    {
        HostMode hostMode{ HostMode::Gameplay };
        GameplaySubstate gameplaySubstate{ GameplaySubstate::None };
        UiContextId uiContextId{ UiContextId::None };
        std::optional<menu::MenuInstanceId> topMenuInstanceId;
        menu::MenuIdentityQuality identityQuality{ menu::MenuIdentityQuality::StablePointer };
        std::uint32_t menuStackRevision{ 0 };
        actions::ActionSetStack actionSetStack;
        PresentationPolicyId presentationPolicyId;
        std::uint32_t contextRevision{ 0 };
        dualpad::input::InputContext legacyInputContext{ dualpad::input::InputContext::Gameplay };
        std::uint32_t legacyContextEpoch{ 1 };

        friend bool operator==(const ResolvedContextSnapshot&, const ResolvedContextSnapshot&) = default;
    };

    struct LegacyContextMirrorState
    {
        dualpad::input::InputContext context{ dualpad::input::InputContext::Gameplay };
        std::uint32_t epoch{ 1 };
    };

    struct ShadowCompareRecord
    {
        std::optional<menu::MenuInstanceId> topMenuInstanceId;
        menu::MenuIdentityQuality identityQuality{ menu::MenuIdentityQuality::StablePointer };
        std::uint32_t menuStackRevision{ 0 };
        UiContextId uiContextId{ UiContextId::None };
        actions::ActionSetStack actionSetStack;
        PresentationPolicyId presentationPolicyId;
        dualpad::input::InputContext legacyInputContext{ dualpad::input::InputContext::Gameplay };
        std::uint32_t legacyContextEpoch{ 1 };
        std::uint32_t contextRevision{ 0 };
    };

    struct ShadowCompareResult
    {
        bool passes{ false };
        std::vector<std::string> diffs;
    };

    class ContextResolver
    {
    public:
        static ContextResolver& GetSingleton();

        ResolvedContextSnapshot ResolveAndPublish(
            const menu::ReconciledMenuStack& menuStack,
            GameplaySubstate gameplaySubstate,
            const CompiledContextCatalog& catalog);

        const ResolvedContextSnapshot& GetPublishedSnapshot() const;
        void ResetForTests();

        static GameplaySubstate GameplaySubstateFromLegacy(dualpad::input::InputContext context);
        static UiContextId UiContextIdFromGameplaySubstate(GameplaySubstate substate);
        static LegacyContextMirrorState ToLegacyMirror(const ResolvedContextSnapshot& snapshot);
        static ShadowCompareResult CompareShadowRecords(
            const ShadowCompareRecord& expected,
            const ShadowCompareRecord& actual);

    private:
        ResolvedContextSnapshot _published{};
    };
}
