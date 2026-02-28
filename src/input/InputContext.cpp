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

    InputContext ContextManager::MenuNameToContext(std::string_view menuName) const
    {
        // 主要菜单
        if (menuName == RE::InventoryMenu::MENU_NAME) return InputContext::InventoryMenu;
        if (menuName == RE::MagicMenu::MENU_NAME) return InputContext::MagicMenu;
        if (menuName == RE::MapMenu::MENU_NAME) return InputContext::MapMenu;
        if (menuName == RE::JournalMenu::MENU_NAME) return InputContext::JournalMenu;

        // 其他菜单
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

        // 特殊菜单
        if (menuName == "Console") return InputContext::Console;
        if (menuName == "Lockpicking Menu") return InputContext::Lockpicking;
        if (menuName == "Loading Menu") return InputContext::Menu;

        // 默认为通用菜单
        return InputContext::Menu;
    }

    InputContext ContextManager::DetectGameplayContext() const
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return InputContext::Gameplay;
        }

        // 死亡
        if (player->IsDead()) {
            return InputContext::Death;
        }

        // 濒死（检查生命值）
        if (player->AsActorValueOwner()) {
            auto health = player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth);
            if (health <= 0.0f && !player->IsDead()) {
                return InputContext::Bleedout;
            }
        }

        // 布娃娃
        if (player->IsInRagdollState()) {
            return InputContext::Ragdoll;
        }

        // 处决动画
        if (player->IsInKillMove()) {
            return InputContext::KillMove;
        }

        // 变身状态（通过 race 检查）
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

        // 骑马
        if (player->IsOnMount()) {
            return InputContext::Riding;
        }

        // 潜行
        if (player->IsSneaking()) {
            return InputContext::Sneaking;
        }

        // 战斗状态由战斗事件处理

        return InputContext::Gameplay;
    }

    void ContextManager::UpdateGameplayContext()
    {
        // 如果当前在菜单中,不更新游戏状态
        auto ctxValue = static_cast<std::uint16_t>(_currentContext);
        if ((ctxValue >= 100 && ctxValue < 2000) || ctxValue == 200) {
            return;
        }

        auto newContext = DetectGameplayContext();
        if (newContext != _currentContext) {
            logger::trace("[DualPad][Context] Gameplay context changed: {} -> {}",
                ToString(_currentContext), ToString(newContext));
            _currentContext = newContext;
        }
    }

    void ContextManager::OnMenuOpen(std::string_view menuName)
    {
        auto newContext = MenuNameToContext(menuName);

        logger::info("[DualPad][Context] Menu opened: {} -> Context: {}",
            menuName, ToString(newContext));

        _contextStack.push_back(_currentContext);
        _currentContext = newContext;
    }

    void ContextManager::OnMenuClose(std::string_view menuName)
    {
        logger::info("[DualPad][Context] Menu closed: {}", menuName);

        if (!_contextStack.empty()) {
            _currentContext = _contextStack.back();
            _contextStack.pop_back();
        }
        else {
            _currentContext = DetectGameplayContext();
        }

        logger::info("[DualPad][Context] Context restored to: {}",
            ToString(_currentContext));
    }

    void ContextManager::PushContext(InputContext context)
    {
        _contextStack.push_back(_currentContext);
        _currentContext = context;

        logger::info("[DualPad][Context] Context pushed: {}",
            ToString(context));
    }

    void ContextManager::PopContext()
    {
        if (!_contextStack.empty()) {
            _currentContext = _contextStack.back();
            _contextStack.pop_back();

            logger::info("[DualPad][Context] Context popped to: {}",
                ToString(_currentContext));
        }
    }

    void ContextManager::SetContext(InputContext context)
    {
        if (_currentContext != context) {
            logger::info("[DualPad][Context] Context set: {} -> {}",
                ToString(_currentContext), ToString(context));
            _currentContext = context;
        }
    }
}