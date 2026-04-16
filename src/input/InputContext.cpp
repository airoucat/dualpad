#include "pch.h"
#include "input/InputContext.h"

#include "input/InputContextNames.h"
#include "input/MenuContextPolicy.h"

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        constexpr bool ShouldAdvanceContextEpoch(InputContext previous, InputContext next)
        {
            if (previous == next) {
                return false;
            }

            if (IsGameplayDomainContext(previous) &&
                IsGameplayDomainContext(next)) {
                // Gameplay sub-context flips such as Gameplay <-> Sneaking are
                // part of the same native digital ownership domain. They should
                // not invalidate in-flight hold/toggle transactions.
                return false;
            }

            return true;
        }

    }

    ContextManager& ContextManager::GetSingleton()
    {
        static ContextManager instance;
        return instance;
    }

    InputContext ContextManager::GetCurrentContext() const
    {
        std::scoped_lock lock(_mutex);
        return _currentContext;
    }

    std::uint32_t ContextManager::GetCurrentEpoch() const
    {
        std::scoped_lock lock(_mutex);
        return _contextEpoch;
    }

    void ContextManager::RefreshCurrentContextLocked()
    {
        const auto previousContext = _currentContext;
        if (!_menuStack.empty()) {
            _currentContext = _menuStack.back().context;
        }
        else {
            _currentContext = _baseContext;
        }

        if (ShouldAdvanceContextEpoch(previousContext, _currentContext)) {
            ++_contextEpoch;
        }
    }

    // Samples transient gameplay state directly from the player object.
    InputContext ContextManager::DetectGameplayContext() const
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return InputContext::Gameplay;
        }

        if (player->IsDead()) {
            return InputContext::Death;
        }

        if (player->AsActorValueOwner()) {
            auto health = player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth);
            if (health <= 0.0f && !player->IsDead()) {
                return InputContext::Bleedout;
            }
        }

        if (player->IsInRagdollState()) {
            return InputContext::Ragdoll;
        }

        if (player->IsInKillMove()) {
            return InputContext::KillMove;
        }

        auto* race = player->GetRace();
        if (race) {
            auto raceName = race->GetFormEditorID();
            if (raceName && std::string_view(raceName).find("Werewolf") != std::string_view::npos) {
                return InputContext::Werewolf;
            }
            if (raceName && std::string_view(raceName).find("VampireLord") != std::string_view::npos) {
                return InputContext::VampireLord;
            }
        }

        if (player->IsOnMount()) {
            return InputContext::Riding;
        }

        if (player->IsSneaking()) {
            return InputContext::Sneaking;
        }

        return InputContext::Gameplay;
    }

    void ContextManager::UpdateFrameState()
    {
        auto gameplayContext = DetectGameplayContext();

        std::scoped_lock lock(_mutex);
        if (!IsMenuOwnedContext(_baseContext) &&
            gameplayContext != _baseContext) {
            logger::trace("[DualPad][Context] Gameplay context changed: {} -> {}",
                ToString(_baseContext), ToString(gameplayContext));
            _baseContext = gameplayContext;
            RefreshCurrentContextLocked();
        }
    }

    void ContextManager::UpdateGameplayContext()
    {
        UpdateFrameState();
    }

    void ContextManager::OnMenuOpen(std::string_view menuName)
    {
        const auto runtimeSnapshot = MenuContextPolicy::GetSingleton().CaptureRuntimeSnapshot(menuName);
        const auto decision = MenuContextPolicy::GetSingleton().DecideMenuTracking(menuName, runtimeSnapshot);

        std::scoped_lock lock(_mutex);
        if (!decision.shouldTrack) {
            ++_passthroughMenuCounts[std::string(menuName)];
            logger::trace("[DualPad][Context] Menu passthrough: {}", menuName);
            return;
        }

        logger::info(
            "[DualPad][Context] Menu opened: {} -> Context: {}",
            menuName,
            ToString(decision.context));

        _menuStack.push_back(MenuContextEntry{
            .menuName = std::string(menuName),
            .context = decision.context
        });
        RefreshCurrentContextLocked();
    }

    void ContextManager::OnMenuClose(std::string_view menuName)
    {
        std::scoped_lock lock(_mutex);
        const auto passthroughKey = std::string(menuName);
        if (auto it = _passthroughMenuCounts.find(passthroughKey);
            it != _passthroughMenuCounts.end()) {
            if (it->second > 1) {
                --it->second;
            }
            else {
                _passthroughMenuCounts.erase(it);
            }
            logger::trace("[DualPad][Context] Menu passthrough close: {}", menuName);
            return;
        }

        for (auto it = _menuStack.rbegin(); it != _menuStack.rend(); ++it) {
            if (it->menuName == menuName) {
                logger::info("[DualPad][Context] Menu closed: {}", menuName);
                _menuStack.erase(std::next(it).base());
                RefreshCurrentContextLocked();
                logger::info("[DualPad][Context] Context restored to: {}",
                    ToString(_currentContext));
                return;
            }
        }

        logger::warn(
            "[DualPad][Context] Menu close '{}' was not tracked; recalculating gameplay base context",
            menuName);
        _baseContext = DetectGameplayContext();
        RefreshCurrentContextLocked();

        logger::info("[DualPad][Context] Context restored to: {}",
            ToString(_currentContext));
    }

    void ContextManager::PushContext(InputContext context)
    {
        std::scoped_lock lock(_mutex);
        _contextStack.push_back(_baseContext);
        _baseContext = context;
        RefreshCurrentContextLocked();

        logger::info("[DualPad][Context] Context pushed: {}",
            ToString(context));
    }

    void ContextManager::PopContext()
    {
        std::scoped_lock lock(_mutex);
        if (!_contextStack.empty()) {
            _baseContext = _contextStack.back();
            _contextStack.pop_back();
            RefreshCurrentContextLocked();

            logger::info("[DualPad][Context] Context popped to: {}",
                ToString(_currentContext));
        }
    }

    void ContextManager::SetContext(InputContext context)
    {
        std::scoped_lock lock(_mutex);
        if (_baseContext != context) {
            logger::info("[DualPad][Context] Context set: {} -> {}",
                ToString(_baseContext), ToString(context));
            _baseContext = context;
            RefreshCurrentContextLocked();
        }
    }
}
