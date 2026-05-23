#include "pch.h"
#include "input/InputContext.h"

#include "input/InputContextNames.h"
#include "input_v2/context/ContextResolver.h"

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

    void ContextManager::SetCurrentContextLocked(InputContext context)
    {
        const auto previousContext = _currentContext;
        _currentContext = context;

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
            if (!IsMenuOwnedContext(_currentContext)) {
                SetCurrentContextLocked(_baseContext);
            }
        }
    }

    void ContextManager::UpdateGameplayContext()
    {
        UpdateFrameState();
    }

    void ContextManager::OnMenuOpen(std::string_view menuName)
    {
        logger::trace("[DualPad][Context] Menu open shim observed '{}'; PH2 resolver owns menu truth", menuName);
    }

    void ContextManager::OnMenuClose(std::string_view menuName)
    {
        logger::trace("[DualPad][Context] Menu close shim observed '{}'; PH2 resolver owns menu truth", menuName);
    }

    void ContextManager::ApplyResolvedContext(const dualpad::input_v2::context::LegacyContextMirrorState& state)
    {
        std::scoped_lock lock(_mutex);
        _currentContext = state.context;
        _contextEpoch = state.epoch;
        if (!IsMenuOwnedContext(state.context)) {
            _baseContext = state.context;
        }
    }

    void ContextManager::PushContext(InputContext context)
    {
        std::scoped_lock lock(_mutex);
        _contextStack.push_back(_baseContext);
        _baseContext = context;
        SetCurrentContextLocked(context);

        logger::info("[DualPad][Context] Context pushed: {}",
            ToString(context));
    }

    void ContextManager::PopContext()
    {
        std::scoped_lock lock(_mutex);
        if (!_contextStack.empty()) {
            _baseContext = _contextStack.back();
            _contextStack.pop_back();
            SetCurrentContextLocked(_baseContext);

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
            SetCurrentContextLocked(context);
        }
    }
}
