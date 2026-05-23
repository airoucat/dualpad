#include "pch.h"
#include "input_v2/menu/MenuInstanceRegistry.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace dualpad::input_v2::menu
{
    namespace
    {
        bool IsPassthroughOverlay(
            const ObservedMenuNode& node,
            const context::CompiledContextCatalog& catalog)
        {
            if (catalog.menuPolicy.ignoreRules.contains(node.menuName)) {
                return true;
            }
            if (context::ContextCatalog::ResolveMenuName(catalog, node.menuName)) {
                return false;
            }
            if (catalog.menuPolicy.trackRules.contains(node.menuName)) {
                return false;
            }

            constexpr auto kAlwaysOpen = 1u << 1;
            constexpr auto kOnStack = 1u << 6;
            constexpr auto kPausesGame = 1u << 0;
            constexpr auto kUsesMenuContext = 1u << 3;
            constexpr auto kModal = 1u << 4;

            return (node.menuFlagsValue & kAlwaysOpen) != 0 &&
                (node.menuFlagsValue & kOnStack) != 0 &&
                (node.menuFlagsValue & (kPausesGame | kUsesMenuContext | kModal)) == 0;
        }

        bool SamePublishedShape(const ReconciledMenuStack& lhs, const ReconciledMenuStack& rhs)
        {
            const auto sameInstances = [](const auto& a, const auto& b) {
                if (a.size() != b.size()) {
                    return false;
                }
                for (std::size_t i = 0; i < a.size(); ++i) {
                    if (a[i].instanceId != b[i].instanceId ||
                        a[i].menuName != b[i].menuName ||
                        a[i].menuPtr != b[i].menuPtr ||
                        a[i].delegatePtr != b[i].delegatePtr ||
                        a[i].moviePtr != b[i].moviePtr ||
                        a[i].depthPriority != b[i].depthPriority ||
                        a[i].identityQuality != b[i].identityQuality ||
                        a[i].firstSeenRevision != b[i].firstSeenRevision ||
                        a[i].observedInLastSnapshot != b[i].observedInLastSnapshot) {
                        return false;
                    }
                }
                return true;
            };

            return lhs.lastCompleteness == rhs.lastCompleteness &&
                sameInstances(lhs.trackedMenus, rhs.trackedMenus) &&
                sameInstances(lhs.passthroughOverlays, rhs.passthroughOverlays);
        }

        void SortInstances(std::vector<TrackedMenuInstance>& instances)
        {
            std::sort(instances.begin(), instances.end(), [](const auto& lhs, const auto& rhs) {
                if (lhs.depthPriority != rhs.depthPriority) {
                    return lhs.depthPriority > rhs.depthPriority;
                }
                if (lhs.firstSeenRevision != rhs.firstSeenRevision) {
                    return lhs.firstSeenRevision > rhs.firstSeenRevision;
                }
                return lhs.instanceId < rhs.instanceId;
            });
        }
    }

    MenuInstanceRegistry& MenuInstanceRegistry::GetSingleton()
    {
        static MenuInstanceRegistry instance;
        return instance;
    }

    const ReconciledMenuStack& MenuInstanceRegistry::GetPublishedStack() const
    {
        return _published;
    }

    std::optional<MenuInstanceId> MenuInstanceRegistry::MatchStablePointer(const ObservedMenuNode& node) const
    {
        if (node.menuPtr == 0) {
            return std::nullopt;
        }
        const auto find = [&](const auto& items) -> std::optional<MenuInstanceId> {
            for (const auto& item : items) {
                if (item.menuPtr == node.menuPtr) {
                    return item.instanceId;
                }
            }
            return std::nullopt;
        };
        if (auto id = find(_published.trackedMenus)) {
            return id;
        }
        return find(_published.passthroughOverlays);
    }

    std::optional<MenuInstanceId> MenuInstanceRegistry::MatchFingerprint(const ObservedMenuNode& node) const
    {
        if (node.delegatePtr == 0 && node.moviePtr == 0) {
            return std::nullopt;
        }

        std::optional<MenuInstanceId> match;
        auto count = 0u;
        const auto scan = [&](const auto& items) {
            for (const auto& item : items) {
                if (item.menuName == node.menuName &&
                    item.delegatePtr == node.delegatePtr &&
                    item.moviePtr == node.moviePtr) {
                    match = item.instanceId;
                    ++count;
                }
            }
        };
        scan(_published.trackedMenus);
        scan(_published.passthroughOverlays);
        return count == 1 ? match : std::nullopt;
    }

    MenuInstanceId MenuInstanceRegistry::AllocateId()
    {
        return _nextId++;
    }

    ReconciledMenuStack MenuInstanceRegistry::ReconcileAndPublish(
        const ObservedMenuSnapshot& snapshot,
        const context::CompiledContextCatalog& catalog)
    {
        if (snapshot.completeness == ObserverCompleteness::Unavailable) {
            return _published;
        }

        ReconciledMenuStack next = _published;
        next.lastCompleteness = snapshot.completeness;
        for (auto& item : next.trackedMenus) {
            item.observedInLastSnapshot = false;
        }
        for (auto& item : next.passthroughOverlays) {
            item.observedInLastSnapshot = false;
        }

        std::unordered_set<MenuInstanceId> seenIds;
        const auto candidateRevision = _published.menuStackRevision + 1;
        auto upsert = [&](std::vector<TrackedMenuInstance>& items, const ObservedMenuNode& node, bool overlay) {
            MenuIdentityQuality quality = MenuIdentityQuality::DegradedIdentity;
            auto id = MatchStablePointer(node);
            if (id) {
                quality = MenuIdentityQuality::StablePointer;
            }
            else if ((id = MatchFingerprint(node))) {
                quality = MenuIdentityQuality::FingerprintRebound;
            }
            else {
                id = AllocateId();
                if (node.menuPtr != 0) {
                    quality = MenuIdentityQuality::StablePointer;
                }
            }

            auto found = std::find_if(items.begin(), items.end(), [&](const auto& item) {
                return item.instanceId == *id;
            });
            if (found == items.end()) {
                items.push_back(TrackedMenuInstance{
                    .instanceId = *id,
                    .menuName = node.menuName,
                    .menuPtr = node.menuPtr,
                    .delegatePtr = node.delegatePtr,
                    .moviePtr = node.moviePtr,
                    .menuFlagsValue = node.menuFlagsValue,
                    .inputContextValue = node.inputContextValue,
                    .depthPriority = node.depthPriority,
                    .observationOrder = node.observationOrder,
                    .identityQuality = quality,
                    .firstSeenRevision = candidateRevision,
                    .lastSeenRevision = candidateRevision,
                    .observedInLastSnapshot = true
                });
            }
            else {
                found->menuName = node.menuName;
                found->menuPtr = node.menuPtr;
                found->delegatePtr = node.delegatePtr;
                found->moviePtr = node.moviePtr;
                found->menuFlagsValue = node.menuFlagsValue;
                found->inputContextValue = node.inputContextValue;
                found->depthPriority = node.depthPriority;
                found->observationOrder = node.observationOrder;
                found->identityQuality = quality;
                found->lastSeenRevision = candidateRevision;
                found->observedInLastSnapshot = true;
            }
            seenIds.insert(*id);

            if (overlay) {
                std::erase_if(next.trackedMenus, [&](const auto& item) { return item.instanceId == *id; });
            }
            else {
                std::erase_if(next.passthroughOverlays, [&](const auto& item) { return item.instanceId == *id; });
            }
        };

        for (const auto& node : snapshot.nodes) {
            if (IsPassthroughOverlay(node, catalog)) {
                upsert(next.passthroughOverlays, node, true);
            }
            else {
                upsert(next.trackedMenus, node, false);
            }
        }

        if (snapshot.completeness == ObserverCompleteness::Complete) {
            std::erase_if(next.trackedMenus, [](const auto& item) { return !item.observedInLastSnapshot; });
            std::erase_if(next.passthroughOverlays, [](const auto& item) { return !item.observedInLastSnapshot; });
        }

        SortInstances(next.trackedMenus);
        SortInstances(next.passthroughOverlays);

        if (!SamePublishedShape(_published, next)) {
            next.menuStackRevision = candidateRevision;
            _published = std::move(next);
        }
        return _published;
    }

    void MenuInstanceRegistry::ResetForTests()
    {
        _published = ReconciledMenuStack{};
        _nextId = 1;
    }
}
