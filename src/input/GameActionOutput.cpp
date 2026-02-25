#include "pch.h"
#include "input/GameActionOutput.h"
#include "input/ActionRouter.h"

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <string_view>

namespace logger = SKSE::log;

namespace
{
    void ShowMenuIfClosed(std::string_view menuName)
    {
        auto* ui = RE::UI::GetSingleton();
        auto* q = RE::UIMessageQueue::GetSingleton();
        if (!ui || !q || menuName.empty()) {
            return;
        }

        // 统一转成 BSFixedString，兼容 IsMenuOpen / AddMessage
        RE::BSFixedString menuFixed(menuName.data());

        if (ui->IsMenuOpen(menuFixed)) {
            return;
        }

        q->AddMessage(menuFixed, RE::UI_MESSAGE_TYPE::kShow, nullptr);
    }

    void DispatchGameAction(std::string_view actionId)
    {
        if (actionId == "Game.OpenInventory") {
            ShowMenuIfClosed(RE::InventoryMenu::MENU_NAME);
        }
        else if (actionId == "Game.OpenMagic") {
            ShowMenuIfClosed(RE::MagicMenu::MENU_NAME);
        }
        else if (actionId == "Game.OpenMap") {
            ShowMenuIfClosed(RE::MapMenu::MENU_NAME);
        }
        else if (actionId == "Game.OpenJournal") {
            ShowMenuIfClosed(RE::JournalMenu::MENU_NAME);
        }
    }
}

namespace dualpad::input
{
    void PumpActionEvents()
    {
        auto events = ActionRouter::GetSingleton().Drain();
        for (auto& e : events) {
            // 只执行 Game.*，Input.* 仅日志观察
            if (e.actionId.rfind("Game.", 0) == 0) {
                logger::info("[DualPad] Dispatch {}", e.actionId);
                DispatchGameAction(e.actionId);
            }
        }
    }
}