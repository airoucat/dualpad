#pragma once

#include "input/InputContext.h"
#include "input_v2/context/ContextCatalog.h"
#include "input_v2/context/ContextResolver.h"
#include "input_v2/menu/UiMenuObserver.h"

#include <cstdint>
#include <mutex>

namespace dualpad::input_v2::context
{
    class ContextRefreshTick
    {
    public:
        static ContextRefreshTick& GetSingleton();

        std::uint64_t BeginFrame();
        ResolvedContextSnapshot RefreshOnMainThread(std::uint64_t frameToken);
        void MarkCombatEvent(bool playerInCombat);

        ResolvedContextSnapshot RefreshObservedForTests(
            std::uint64_t frameToken,
            const menu::ObservedMenuSnapshot& observed,
            dualpad::input::InputContext detectedGameplayContext,
            const CompiledContextCatalog& catalog);
        void ResetForTests();

    private:
        ContextRefreshTick() = default;

        ResolvedContextSnapshot ResolveAndMirror(
            const menu::ObservedMenuSnapshot& observed,
            dualpad::input::InputContext detectedGameplayContext,
            const CompiledContextCatalog& catalog);
        GameplaySubstate ResolveGameplaySubstate(dualpad::input::InputContext detectedGameplayContext) const;
        bool ShouldSkipFrameLocked(std::uint64_t frameToken);

        mutable std::mutex _mutex;
        std::uint64_t _nextFrameToken{ 1 };
        std::uint64_t _lastRefreshedFrameToken{ 0 };
        bool _combatActive{ false };
    };
}
