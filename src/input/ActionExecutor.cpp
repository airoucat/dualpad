#include "pch.h"
#include "input/ActionExecutor.h"
#include "input/Action.h"
#include "input/PadProfile.h"
#include "input/Screenshot.h"
#include "input/SyntheticPadState.h"
#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        void PulseVirtualButton(std::uint32_t bit, std::string_view reason)
        {
            if (!bit) {
                return;
            }

            SyntheticPadState::GetSingleton().PulseButton(bit);
            logger::info("[DualPad][Executor] Pulsed virtual button 0x{:08X} for {}", bit, reason);
        }
    }

    ActionExecutor& ActionExecutor::GetSingleton()
    {
        static ActionExecutor instance;
        return instance;
    }

    bool ActionExecutor::Execute(std::string_view actionId, InputContext context)
    {
        logger::info("[DualPad][Executor] Execute: action='{}' context='{}'",
            actionId, ToString(context));

        if (ExecuteExtendedAction(actionId)) {
            return true;
        }

        if (ExecuteGameplayAction(actionId)) {
            return true;
        }

        if (ExecuteMenuAction(actionId, context)) {
            return true;
        }

        logger::warn("[DualPad][Executor] Unknown action: {}", actionId);
        return false;
    }

    bool ActionExecutor::ExecuteExtendedAction(std::string_view actionId)
    {
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

        if (actionId == actions::TogglePOV) {
            logger::info("[DualPad][Executor] Scheduling POV toggle on main thread");

            static std::chrono::steady_clock::time_point lastToggleTime;
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastToggleTime).count();

            if (elapsed < 500) {
                logger::trace("[DualPad][Executor] POV toggle debounced ({}ms)", elapsed);
                return true;
            }
            lastToggleTime = now;

            auto* taskInterface = SKSE::GetTaskInterface();
            if (taskInterface) {
                taskInterface->AddTask([]() {
                    auto* player = RE::PlayerCharacter::GetSingleton();
                    auto* camera = RE::PlayerCamera::GetSingleton();

                    if (!player || !camera) {
                        logger::warn("[DualPad][Executor] Player or Camera not found");
                        return;
                    }

                    auto* state = camera->currentState.get();
                    if (!state) {
                        logger::warn("[DualPad][Executor] Camera state is null");
                        return;
                    }

                    try {
                        bool isFirstPerson = (state->id == RE::CameraState::kFirstPerson);

                        if (isFirstPerson) {
                            camera->ForceThirdPerson();
                            logger::info("[DualPad][Executor] Switched to Third Person");
                        }
                        else {
                            camera->ForceFirstPerson();
                            logger::info("[DualPad][Executor] Switched to First Person");
                        }
                    }
                    catch (const std::exception& e) {
                        logger::error("[DualPad][Executor] Exception during POV toggle: {}", e.what());
                    }
                    catch (...) {
                        logger::error("[DualPad][Executor] Unknown exception during POV toggle");
                    }
                    });

                return true;
            }

            logger::warn("[DualPad][Executor] Failed to get TaskInterface");
            return false;
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

        if (actionId == actions::Screenshot) {
            logger::info("[DualPad][Executor] Taking screenshot");

            auto* taskInterface = SKSE::GetTaskInterface();
            if (taskInterface) {
                taskInterface->AddTask([]() {
                    std::string path = dualpad::utils::TakeScreenshot();
                    if (!path.empty()) {
                        logger::info("[DualPad][Executor] Screenshot saved: {}", path);
                    }
                    else {
                        logger::error("[DualPad][Executor] Screenshot failed");
                    }
                    });

                return true;
            }

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

    bool ActionExecutor::ExecuteGameplayAction(std::string_view actionId)
    {
        const auto& bits = GetPadBits(GetActivePadProfile());

        if (actionId == actions::Jump) {
            PulseVirtualButton(bits.jump, actionId);
            return true;
        }
        if (actionId == actions::Activate) {
            PulseVirtualButton(bits.activate, actionId);
            return true;
        }
        if (actionId == actions::Sprint) {
            PulseVirtualButton(bits.sprint, actionId);
            return true;
        }
        if (actionId == actions::Attack) {
            PulseVirtualButton(bits.attack, actionId);
            return true;
        }
        if (actionId == actions::Sneak) {
            PulseVirtualButton(bits.sneak, actionId);
            return true;
        }
        if (actionId == actions::Shout) {
            PulseVirtualButton(bits.r2Button, actionId);
            return true;
        }

        return false;
    }

    bool ActionExecutor::ExecuteMenuAction(std::string_view actionId, InputContext context)
    {
        (void)context;

        const auto& bits = GetPadBits(GetActivePadProfile());

        if (actionId == actions::MenuConfirm || actionId == "Console.Execute"sv) {
            PulseVirtualButton(bits.cross, actionId);
            return true;
        }

        if (actionId == actions::MenuCancel || actionId == "Book.Close"sv) {
            PulseVirtualButton(bits.circle, actionId);
            return true;
        }

        if (actionId == actions::MenuScrollUp ||
            actionId == "Dialogue.PreviousOption"sv ||
            actionId == "Favorites.PreviousItem"sv ||
            actionId == "Console.HistoryUp"sv) {
            PulseVirtualButton(bits.dpadUp, actionId);
            return true;
        }

        if (actionId == actions::MenuScrollDown ||
            actionId == "Dialogue.NextOption"sv ||
            actionId == "Favorites.NextItem"sv ||
            actionId == "Console.HistoryDown"sv) {
            PulseVirtualButton(bits.dpadDown, actionId);
            return true;
        }

        if (actionId == actions::MenuPageUp ||
            actionId == "Book.PreviousPage"sv) {
            PulseVirtualButton(bits.l1, actionId);
            return true;
        }

        if (actionId == actions::MenuPageDown ||
            actionId == "Book.NextPage"sv) {
            PulseVirtualButton(bits.r1, actionId);
            return true;
        }

        if (actionId == "Menu.SortByName"sv) {
            PulseVirtualButton(bits.l1, actionId);
            return true;
        }

        if (actionId == "Menu.SortByValue"sv) {
            PulseVirtualButton(bits.r1, actionId);
            return true;
        }

        return false;
    }
}
