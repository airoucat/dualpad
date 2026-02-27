#include "pch.h"
#include "input/ActionExecutor.h"

#include "input/GameActions.h"
#include "input/NativeUserEventBridge.h"

#include <RE/Skyrim.h>
#include <array>

namespace
{
    using dualpad::input::TriggerCode;
    using dualpad::input::TriggerPhase;
    using BA = dualpad::input::actions::ButtonAction;

    inline bool IsPressLike(TriggerPhase p)
    {
        return p == TriggerPhase::Press || p == TriggerPhase::Pulse;
    }

    void ShowMenuIfClosed(std::string_view menuName)
    {
        auto* ui = RE::UI::GetSingleton();
        auto* q = RE::UIMessageQueue::GetSingleton();
        if (!ui || !q || menuName.empty()) {
            return;
        }

        RE::BSFixedString menuFixed(menuName.data());
        if (ui->IsMenuOpen(menuFixed)) {
            return;
        }

        q->AddMessage(menuFixed, RE::UI_MESSAGE_TYPE::kShow, nullptr);
    }

    bool TryMapGameActionToTrigger(BA a, TriggerCode& out)
    {
        switch (a) {
        case BA::Confirm:       out = TriggerCode::Cross; return true;
        case BA::Back:          out = TriggerCode::Circle; return true;
        case BA::CloseMenu:     out = TriggerCode::Circle; return true;

        case BA::NavigateUp:    out = TriggerCode::DpadUp; return true;
        case BA::NavigateDown:  out = TriggerCode::DpadDown; return true;
        case BA::NavigateLeft:  out = TriggerCode::DpadLeft; return true;
        case BA::NavigateRight: out = TriggerCode::DpadRight; return true;

        case BA::TabSwitch:     out = TriggerCode::R1; return true;
        case BA::PageUp:        out = TriggerCode::L1; return true;
        case BA::PageDown:      out = TriggerCode::R1; return true;

        case BA::OpenPauseMenu: out = TriggerCode::Options; return true;
        case BA::OpenOptions:   out = TriggerCode::Options; return true;

        case BA::Activate:      out = TriggerCode::Cross; return true;
        case BA::Jump:          out = TriggerCode::Triangle; return true;
        case BA::Shout:         out = TriggerCode::R1; return true;
        case BA::ToggleSneak:   out = TriggerCode::R3; return true;
        case BA::SprintHold:    out = TriggerCode::L3; return true;
        case BA::ToggleRun:     out = TriggerCode::L3; return true;

        case BA::LeftEquip:     out = TriggerCode::DpadLeft; return true;
        case BA::RightEquip:    out = TriggerCode::DpadRight; return true;

        default:
            return false;
        }
    }

    bool IsOneShot(BA a)
    {
        switch (a) {
        case BA::Confirm:
        case BA::Back:
        case BA::CloseMenu:
        case BA::NavigateUp:
        case BA::NavigateDown:
        case BA::NavigateLeft:
        case BA::NavigateRight:
        case BA::TabSwitch:
        case BA::PageUp:
        case BA::PageDown:
        case BA::OpenPauseMenu:
        case BA::OpenOptions:
        case BA::Activate:
        case BA::Jump:
        case BA::Shout:
        case BA::ToggleSneak:
        case BA::ToggleRun:
        case BA::LeftEquip:
        case BA::RightEquip:
            return true;
        default:
            return false;
        }
    }

    // 关键：Native 仅白名单开启，避免再次崩溃
    bool IsNativeAllowed(BA a)
    {
        return false; 
        switch (a) {
        case BA::Confirm:
        case BA::Back:
        case BA::NavigateUp:
        case BA::NavigateDown:
        case BA::NavigateLeft:
        case BA::NavigateRight:
        case BA::Activate:
        case BA::Jump:
        case BA::Shout:
        case BA::ToggleSneak:
        case BA::SprintHold:
        case BA::ToggleRun:
        case BA::LeftEquip:
        case BA::RightEquip:
        case BA::OpenPauseMenu:
        case BA::OpenOptions:
            return true;
        default:
            return false;
        }
    }

    class DirectGameExecutor final : public dualpad::input::IActionExecutor
    {
    public:
        bool ExecuteButton(std::string_view actionId, TriggerPhase phase) override
        {
            const auto act = dualpad::input::actions::ParseButtonAction(actionId);
            const bool fire = IsPressLike(phase);

            switch (act) {
            case BA::OpenInventory: if (fire) ShowMenuIfClosed(RE::InventoryMenu::MENU_NAME); return true;
            case BA::OpenMagic:     if (fire) ShowMenuIfClosed(RE::MagicMenu::MENU_NAME); return true;
            case BA::OpenMap:
            case BA::OpenLocalMap:  if (fire) ShowMenuIfClosed(RE::MapMenu::MENU_NAME); return true;
            case BA::OpenJournal:   if (fire) ShowMenuIfClosed(RE::JournalMenu::MENU_NAME); return true;
            case BA::OpenFavorites: if (fire) ShowMenuIfClosed("FavoritesMenu"); return true;
            case BA::OpenTweenMenu: if (fire) ShowMenuIfClosed("TweenMenu"); return true;
            default:
                return false; // 交给 Native
            }
        }

        bool ExecuteAxis(std::string_view, float) override
        {
            return false;
        }
    };

    class NativeGameExecutor final : public dualpad::input::IActionExecutor
    {
    public:
        bool ExecuteButton(std::string_view actionId, TriggerPhase phase) override
        {
            auto& bridge = dualpad::input::NativeUserEventBridge::GetSingleton();
            if (!bridge.HasSubmitter()) {
                return false;
            }

            const BA act = dualpad::input::actions::ParseButtonAction(actionId);

            // 白名单：仅放行少量安全动作
            if (!IsNativeAllowed(act)) {
                return false;
            }

            TriggerCode code{ TriggerCode::None };
            if (!TryMapGameActionToTrigger(act, code)) {
                return false;
            }

            if (phase == TriggerPhase::Pulse) {
                bridge.Enqueue(code, TriggerPhase::Press);
                bridge.Enqueue(code, TriggerPhase::Release);
                return true;
            }

            if (IsOneShot(act) && phase == TriggerPhase::Release) {
                return true;
            }

            bridge.Enqueue(code, phase);
            return true;
        }

        bool ExecuteAxis(std::string_view, float) override
        {
            return false;
        }
    };

    class CompositeGameExecutor final : public dualpad::input::IActionExecutor
    {
    public:
        CompositeGameExecutor()
        {
            _chain = { &_direct, &_native };
        }

        bool ExecuteButton(std::string_view actionId, TriggerPhase phase) override
        {
            for (auto* e : _chain) {
                if (e && e->ExecuteButton(actionId, phase)) {
                    return true;
                }
            }
            return false;
        }

        bool ExecuteAxis(std::string_view actionId, float value) override
        {
            for (auto* e : _chain) {
                if (e && e->ExecuteAxis(actionId, value)) {
                    return true;
                }
            }
            return false;
        }

    private:
        DirectGameExecutor _direct;
        NativeGameExecutor _native;
        std::array<dualpad::input::IActionExecutor*, 2> _chain{};
    };
}

namespace dualpad::input
{
    IActionExecutor& GetCompositeGameExecutor()
    {
        static CompositeGameExecutor s_exec;
        return s_exec;
    }
}