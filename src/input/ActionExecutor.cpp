#include "pch.h"
#include "input/ActionExecutor.h"
#include "input/Action.h"
#include "input/custom/CustomActionDispatcher.h"
#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

namespace logger = SKSE::log;

namespace dualpad::input
{
    ActionExecutor& ActionExecutor::GetSingleton()
    {
        static ActionExecutor instance;
        return instance;
    }

    bool ActionExecutor::Execute(std::string_view actionId, InputContext context)
    {
        logger::info("[DualPad][Executor] Execute: action='{}' context='{}'",
            actionId, ToString(context));

        if (ExecutePluginAction(actionId)) {
            return true;
        }

        logger::warn("[DualPad][Executor] Unknown action: {}", actionId);
        return false;
    }

    bool ActionExecutor::ExecutePluginAction(std::string_view actionId)
    {
        if (custom::CustomActionDispatcher::GetSingleton().Execute(actionId)) {
            return true;
        }

        auto* ui = RE::UI::GetSingleton();
        auto* queue = RE::UIMessageQueue::GetSingleton();
        if (!ui || !queue) {
            return false;
        }

        if (actionId == actions::OpenInventory) {
            if (!ui->IsMenuOpen(RE::InventoryMenu::MENU_NAME)) {
                queue->AddMessage(RE::InventoryMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kShow, nullptr);
                logger::info("[DualPad][Executor] Opening Inventory");
            }
            return true;
        }

        if (actionId == actions::OpenMagic) {
            if (!ui->IsMenuOpen(RE::MagicMenu::MENU_NAME)) {
                queue->AddMessage(RE::MagicMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kShow, nullptr);
                logger::info("[DualPad][Executor] Opening Magic");
            }
            return true;
        }

        if (actionId == actions::OpenMap) {
            if (!ui->IsMenuOpen(RE::MapMenu::MENU_NAME)) {
                queue->AddMessage(RE::MapMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kShow, nullptr);
                logger::info("[DualPad][Executor] Opening Map");
            }
            return true;
        }

        if (actionId == actions::OpenJournal) {
            if (!ui->IsMenuOpen(RE::JournalMenu::MENU_NAME)) {
                queue->AddMessage(RE::JournalMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kShow, nullptr);
                logger::info("[DualPad][Executor] Opening Journal");
            }
            return true;
        }

        if (actionId == actions::OpenFavorites) {
            if (!ui->IsMenuOpen("FavoritesMenu")) {
                queue->AddMessage("FavoritesMenu", RE::UI_MESSAGE_TYPE::kShow, nullptr);
                logger::info("[DualPad][Executor] Opening Favorites");
            }
            return true;
        }

        if (actionId == actions::OpenSkills) {
            if (!ui->IsMenuOpen("StatsMenu")) {
                queue->AddMessage("StatsMenu", RE::UI_MESSAGE_TYPE::kShow, nullptr);
                logger::info("[DualPad][Executor] Opening Skills");
            }
            return true;
        }

        if (actionId == actions::ToggleHUD) {
            logger::info("[DualPad][Executor] Scheduling HUD toggle on main thread");

            auto* taskInterface = SKSE::GetTaskInterface();
            if (taskInterface) {
                taskInterface->AddTask([]() {
                    auto* ui = RE::UI::GetSingleton();
                    if (!ui) {
                        logger::warn("[DualPad][Executor] UI not found");
                        return;
                    }

                    auto hudMenuPtr = ui->GetMenu<RE::HUDMenu>();
                    if (hudMenuPtr) {
                        RE::HUDMenu* hudMenu = hudMenuPtr.get();
                        if (hudMenu && hudMenu->uiMovie) {
                            auto* movie = hudMenu->uiMovie.get();
                            if (movie) {
                                bool isVisible = movie->GetVisible();
                                movie->SetVisible(!isVisible);
                                logger::info("[DualPad][Executor] HUD visibility: {} -> {}",
                                    isVisible, !isVisible);
                            }
                        }
                    }
                    });

                return true;
            }

            logger::warn("[DualPad][Executor] Failed to get TaskInterface");
            return false;
        }

        if (actionId == actions::Wait) {

            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) {
                logger::warn("[DualPad][Executor] Player not found");
                return false;
            }

            if (player->IsInCombat()) {
                logger::warn("[DualPad][Executor] Cannot wait during combat");
                return false;
            }

            const char* sleepWaitMenuName = "Sleep/Wait Menu";
            if (!ui->IsMenuOpen(sleepWaitMenuName)) {
                queue->AddMessage(sleepWaitMenuName, RE::UI_MESSAGE_TYPE::kShow, nullptr);
                logger::info("[DualPad][Executor] Opening Wait Menu");
                return true;
            }

            logger::info("[DualPad][Executor] Wait Menu already open");
            return true;
        }

        if (actionId == actions::QuickSave) {
            auto* saveLoadManager = RE::BGSSaveLoadManager::GetSingleton();
            if (!saveLoadManager) {
                logger::warn("[DualPad][Executor] SaveLoad manager not found");
                return false;
            }

            saveLoadManager->Save("DualPad_QuickSave");
            logger::info("[DualPad][Executor] Requested quick save");
            return true;
        }

        if (actionId == actions::QuickLoad) {
            auto* saveLoadManager = RE::BGSSaveLoadManager::GetSingleton();
            if (!saveLoadManager) {
                logger::warn("[DualPad][Executor] SaveLoad manager not found");
                return false;
            }

            if (!saveLoadManager->LoadMostRecentSaveGame()) {
                logger::warn("[DualPad][Executor] No recent save available for quick load");
                return false;
            }

            logger::info("[DualPad][Executor] Requested quick load");
            return true;
        }

        return false;
    }
}
