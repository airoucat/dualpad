#include "pch.h"
#include "input/InputContext.h"
#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

namespace logger = SKSE::log;

namespace dualpad::input
{
    ContextManager& ContextManager::GetSingleton()
    {
        static ContextManager instance;
        return instance;
    }

    InputContext ContextManager::GetCurrentContext() const
    {
        return _currentContext;
    }

    void ContextManager::RefreshCurrentContext()
    {
        if (!_menuStack.empty()) {
            _currentContext = _menuStack.back().context;
            return;
        }

        _currentContext = _baseContext;
    }

    bool ContextManager::ShouldTrackMenu(std::string_view menuName) const
    {
        // These overlays are frequently open during normal gameplay and should not
        // steal the logical input context from the active gameplay/menu state.
        return menuName != "HUD Menu" &&
            menuName != "Fader Menu" &&
            menuName != "Cursor Menu" &&
            menuName != "Mist Menu" &&
            menuName != "Tutorial Menu" &&
            menuName != "LoadWaitSpinner";
    }

    // Maps Skyrim menu names to the plugin's context enum.
    InputContext ContextManager::MenuNameToContext(std::string_view menuName) const
    {

        if (menuName == RE::InventoryMenu::MENU_NAME) return InputContext::InventoryMenu;
        if (menuName == RE::MagicMenu::MENU_NAME) return InputContext::MagicMenu;
        if (menuName == RE::MapMenu::MENU_NAME) return InputContext::MapMenu;
        if (menuName == RE::JournalMenu::MENU_NAME) return InputContext::JournalMenu;

        if (menuName == "DialogueMenu") return InputContext::DialogueMenu;
        if (menuName == "FavoritesMenu") return InputContext::FavoritesMenu;
        if (menuName == "TweenMenu") return InputContext::TweenMenu;
        if (menuName == "ContainerMenu") return InputContext::ContainerMenu;
        if (menuName == "BarterMenu") return InputContext::BarterMenu;
        if (menuName == "Training Menu") return InputContext::TrainingMenu;
        if (menuName == "LevelUp Menu") return InputContext::LevelUpMenu;
        if (menuName == "RaceSex Menu") return InputContext::RaceSexMenu;
        if (menuName == "StatsMenu") return InputContext::StatsMenu;
        if (menuName == "SkillMenu") return InputContext::SkillMenu;
        if (menuName == "Book Menu") return InputContext::BookMenu;
        if (menuName == "MessageBoxMenu") return InputContext::MessageBoxMenu;
        if (menuName == "QuantityMenu") return InputContext::QuantityMenu;
        if (menuName == "GiftMenu") return InputContext::GiftMenu;
        if (menuName == "Creations Menu") return InputContext::CreationsMenu;

        if (menuName == "Console") return InputContext::Console;
        if (menuName == "Lockpicking Menu") return InputContext::Lockpicking;
        if (menuName == "Loading Menu") return InputContext::Menu;

        return InputContext::Menu;
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

    void ContextManager::UpdateGameplayContext()
    {

        // Menu-owned contexts stay fixed until the matching close event arrives.
        auto ctxValue = static_cast<std::uint16_t>(_baseContext);
        if ((ctxValue >= 100 && ctxValue < 2000) || ctxValue == 200) {
            return;
        }

        auto newContext = DetectGameplayContext();
        if (newContext != _baseContext) {
            logger::trace("[DualPad][Context] Gameplay context changed: {} -> {}",
                ToString(_baseContext), ToString(newContext));
            _baseContext = newContext;
            RefreshCurrentContext();
        }
    }

    void ContextManager::OnMenuOpen(std::string_view menuName)
    {
        if (!ShouldTrackMenu(menuName)) {
            logger::trace("[DualPad][Context] Ignoring passive menu open: {}", menuName);
            return;
        }

        auto newContext = MenuNameToContext(menuName);

        logger::info("[DualPad][Context] Menu opened: {} -> Context: {}",
            menuName, ToString(newContext));

        _menuStack.push_back(MenuContextEntry{
            .menuName = std::string(menuName),
            .context = newContext
            });
        RefreshCurrentContext();
    }

    void ContextManager::OnMenuClose(std::string_view menuName)
    {
        if (!ShouldTrackMenu(menuName)) {
            logger::trace("[DualPad][Context] Ignoring passive menu close: {}", menuName);
            return;
        }

        logger::info("[DualPad][Context] Menu closed: {}", menuName);

        for (auto it = _menuStack.rbegin(); it != _menuStack.rend(); ++it) {
            if (it->menuName == menuName) {
                _menuStack.erase(std::next(it).base());
                RefreshCurrentContext();
                logger::info("[DualPad][Context] Context restored to: {}",
                    ToString(_currentContext));
                return;
            }
        }

        _baseContext = DetectGameplayContext();
        RefreshCurrentContext();

        logger::info("[DualPad][Context] Context restored to: {}",
            ToString(_currentContext));
    }

    void ContextManager::PushContext(InputContext context)
    {
        _contextStack.push_back(_baseContext);
        _baseContext = context;
        RefreshCurrentContext();

        logger::info("[DualPad][Context] Context pushed: {}",
            ToString(context));
    }

    void ContextManager::PopContext()
    {
        if (!_contextStack.empty()) {
            _baseContext = _contextStack.back();
            _contextStack.pop_back();
            RefreshCurrentContext();

            logger::info("[DualPad][Context] Context popped to: {}",
                ToString(_currentContext));
        }
    }

    void ContextManager::SetContext(InputContext context)
    {
        if (_baseContext != context) {
            logger::info("[DualPad][Context] Context set: {} -> {}",
                ToString(_baseContext), ToString(context));
            _baseContext = context;
            RefreshCurrentContext();
        }
    }
}
