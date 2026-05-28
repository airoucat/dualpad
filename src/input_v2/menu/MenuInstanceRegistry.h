#pragma once

#include "input_v2/context/ContextCatalog.h"
#include "input_v2/menu/UiMenuObserver.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dualpad::input_v2::menu
{
    using MenuInstanceId = std::uint64_t;

    enum class MenuIdentityQuality : std::uint8_t
    {
        StablePointer = 0,
        FingerprintRebound,
        DegradedIdentity
    };

    struct TrackedMenuInstance
    {
        MenuInstanceId instanceId{ 0 };
        std::string menuName;
        std::uintptr_t menuPtr{ 0 };
        std::uintptr_t delegatePtr{ 0 };
        std::uintptr_t moviePtr{ 0 };
        std::uint32_t menuFlagsValue{ 0 };
        std::uint32_t inputContextValue{ 0 };
        std::int32_t depthPriority{ 0 };
        std::uint32_t observationOrder{ 0 };
        MenuIdentityQuality identityQuality{ MenuIdentityQuality::StablePointer };
        std::uint32_t firstSeenRevision{ 0 };
        std::uint32_t lastSeenRevision{ 0 };
        bool observedInLastSnapshot{ true };
    };

    struct ReconciledMenuStack
    {
        std::vector<TrackedMenuInstance> trackedMenus;
        std::vector<TrackedMenuInstance> passthroughOverlays;
        std::uint32_t menuStackRevision{ 0 };
        ObserverCompleteness lastCompleteness{ ObserverCompleteness::Complete };
    };

    class MenuInstanceRegistry
    {
    public:
        static MenuInstanceRegistry& GetSingleton();

        const ReconciledMenuStack& GetPublishedStack() const;
        ReconciledMenuStack ReconcileAndPublish(
            const ObservedMenuSnapshot& snapshot,
            const context::CompiledContextCatalog& catalog);
        void ResetForTests();

    private:
        std::optional<MenuInstanceId> MatchStablePointer(const ObservedMenuNode& node) const;
        std::optional<MenuInstanceId> MatchFingerprint(const ObservedMenuNode& node) const;
        MenuInstanceId AllocateId();

        ReconciledMenuStack _published{};
        MenuInstanceId _nextId{ 1 };
    };
}
