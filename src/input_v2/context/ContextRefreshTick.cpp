#include "pch.h"
#include "input_v2/context/ContextRefreshTick.h"

#include "input_v2/config/AtomicConfigReloader.h"
#include "input_v2/menu/MenuInstanceRegistry.h"

namespace dualpad::input_v2::context
{
    namespace
    {
        const CompiledContextCatalog& ActiveCatalog()
        {
            auto active = config::AtomicConfigReloader::GetSingleton().GetActiveBundleSnapshot();
            if (active) {
                return active->catalog;
            }
            return ContextCatalog::BuiltInCatalog();
        }
    }

    ContextRefreshTick& ContextRefreshTick::GetSingleton()
    {
        static ContextRefreshTick instance;
        return instance;
    }

    std::uint64_t ContextRefreshTick::BeginFrame()
    {
        std::scoped_lock lock(_mutex);
        return _nextFrameToken++;
    }

    bool ContextRefreshTick::ShouldSkipFrameLocked(std::uint64_t frameToken)
    {
        if (frameToken == 0) {
            return false;
        }
        if (_lastRefreshedFrameToken == frameToken) {
            return true;
        }
        _lastRefreshedFrameToken = frameToken;
        return false;
    }

    void ContextRefreshTick::MarkCombatEvent(bool playerInCombat)
    {
        std::scoped_lock lock(_mutex);
        _combatActive = playerInCombat;
    }

    GameplaySubstate ContextRefreshTick::ResolveGameplaySubstate(
        dualpad::input::InputContext detectedGameplayContext) const
    {
        std::scoped_lock lock(_mutex);
        if (_combatActive) {
            return GameplaySubstate::Combat;
        }
        return ContextResolver::GameplaySubstateFromLegacy(detectedGameplayContext);
    }

    ResolvedContextSnapshot ContextRefreshTick::ResolveAndMirror(
        const menu::ObservedMenuSnapshot& observed,
        dualpad::input::InputContext detectedGameplayContext,
        const CompiledContextCatalog& catalog)
    {
        const auto stack = menu::MenuInstanceRegistry::GetSingleton().ReconcileAndPublish(observed, catalog);
        const auto gameplaySubstate = ResolveGameplaySubstate(detectedGameplayContext);
        const auto resolved = ContextResolver::GetSingleton().ResolveAndPublish(stack, gameplaySubstate, catalog);
        dualpad::input::ContextManager::GetSingleton().ApplyResolvedContext(ContextResolver::ToLegacyMirror(resolved));
        return resolved;
    }

    ResolvedContextSnapshot ContextRefreshTick::RefreshOnMainThread(std::uint64_t frameToken)
    {
        {
            std::scoped_lock lock(_mutex);
            if (ShouldSkipFrameLocked(frameToken)) {
                return ContextResolver::GetSingleton().GetPublishedSnapshot();
            }
        }

        auto& observer = menu::UiMenuObserver::GetSingleton();
        auto observed = observer.GetPublishedSnapshot();
        if (observer.IsDirty()) {
            observed = observer.Capture();
            observer.Publish(observed);
        }

        const auto detectedGameplayContext =
            dualpad::input::ContextManager::GetSingleton().DetectGameplayContext();
        return ResolveAndMirror(observed, detectedGameplayContext, ActiveCatalog());
    }

    ResolvedContextSnapshot ContextRefreshTick::RefreshObservedForTests(
        std::uint64_t frameToken,
        const menu::ObservedMenuSnapshot& observed,
        dualpad::input::InputContext detectedGameplayContext,
        const CompiledContextCatalog& catalog)
    {
        {
            std::scoped_lock lock(_mutex);
            if (ShouldSkipFrameLocked(frameToken)) {
                return ContextResolver::GetSingleton().GetPublishedSnapshot();
            }
        }

        menu::UiMenuObserver::GetSingleton().Publish(observed);
        return ResolveAndMirror(observed, detectedGameplayContext, catalog);
    }

    void ContextRefreshTick::ResetForTests()
    {
        {
            std::scoped_lock lock(_mutex);
            _nextFrameToken = 1;
            _lastRefreshedFrameToken = 0;
            _combatActive = false;
        }
        menu::UiMenuObserver::GetSingleton().ResetForTests();
        menu::MenuInstanceRegistry::GetSingleton().ResetForTests();
        ContextResolver::GetSingleton().ResetForTests();
        dualpad::input::ContextManager::GetSingleton().ApplyResolvedContext(
            LegacyContextMirrorState{
                .context = dualpad::input::InputContext::Gameplay,
                .epoch = 1
            });
    }
}
