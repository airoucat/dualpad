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
        if (!ui) {
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

        return false;
    }
}
