#include "pch.h"
#include "input/ActionExecutor.h"
#include "input/Action.h"
#include "input/Screenshot.h"
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

        // =====================
        // 打开菜单类
        // =====================

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

        // =====================
        // 视角与界面类
        // =====================

        // 切换第一/第三人称视角
        // 切换第一/第三人称视角
        // 切换第一/第三人称视角
        if (actionId == actions::TogglePOV) {
            logger::info("[DualPad][Executor] Scheduling POV toggle on main thread");

            // 防抖：避免快速连续切换
            static std::chrono::steady_clock::time_point lastToggleTime;
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastToggleTime).count();

            if (elapsed < 500) {
                logger::trace("[DualPad][Executor] POV toggle debounced ({}ms)", elapsed);
                return true;
            }
            lastToggleTime = now;

            // 在主线程执行相机切换
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

        // 切换 HUD 显示/隐藏
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

        // 截图（Windows API，可以在任何线程调用）
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

        // =====================
        // 游戏功能类
        // =====================

        // 打开等待/休息菜单
        if (actionId == actions::Wait) {
            // 检查是否可以等待
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) {
                logger::warn("[DualPad][Executor] Player not found");
                return false;
            }

            if (player->IsInCombat()) {
                logger::warn("[DualPad][Executor] Cannot wait during combat");
                return false;
            }

            // 尝试打开等待菜单
            const char* sleepWaitMenuName = "Sleep/Wait Menu";
            if (!ui->IsMenuOpen(sleepWaitMenuName)) {
                queue->AddMessage(sleepWaitMenuName, RE::UI_MESSAGE_TYPE::kShow, nullptr);
                logger::info("[DualPad][Executor] Opening Wait Menu");
                return true;
            }

            logger::info("[DualPad][Executor] Wait Menu already open");
            return true;
        }

        // 快速存档（暂未实现）
        if (actionId == actions::QuickSave) {
            (void)actionId;
            return true;
        }

        // 快速读档（暂未实现）
        if (actionId == actions::QuickLoad) {
            (void)actionId;
            return true;
        }

        return false;
    }

    bool ActionExecutor::ExecuteGameplayAction(std::string_view actionId)
    {
        return false;
    }

    bool ActionExecutor::ExecuteMenuAction(std::string_view actionId, InputContext context)
    {
        (void)context;
        return false;
    }
}