#include "pch.h"
#include "input/BindingManager.h"
#include <SKSE/SKSE.h>

namespace logger = SKSE::log;

namespace dualpad::input
{
    BindingManager& BindingManager::GetSingleton()
    {
        static BindingManager instance;
        return instance;
    }

    void BindingManager::AddBinding(const Binding& binding)
    {
        std::scoped_lock lock(_mutex);

        _bindings[binding.context][binding.trigger] = binding.actionId;

        logger::trace("[DualPad][BindingMgr] Added binding: context={} trigger={:08X} action={}",
            static_cast<int>(binding.context), binding.trigger.code, binding.actionId);
    }

    void BindingManager::RemoveBinding(const Trigger& trigger, InputContext context)
    {
        std::scoped_lock lock(_mutex);

        auto ctxIt = _bindings.find(context);
        if (ctxIt != _bindings.end()) {
            ctxIt->second.erase(trigger);
        }
    }
    std::optional<std::string> BindingManager::GetActionForTrigger(
        const Trigger& trigger,
        InputContext context) const
    {
        std::scoped_lock lock(_mutex);

        auto ctxIt = _bindings.find(context);
        if (ctxIt == _bindings.end()) {
            return std::nullopt;
        }

        auto triggerIt = ctxIt->second.find(trigger);
        if (triggerIt == ctxIt->second.end()) {
            return std::nullopt;
        }

        return triggerIt->second;
    }

    std::optional<Trigger> BindingManager::GetTriggerForAction(
        std::string_view actionId,
        InputContext context) const
    {
        std::scoped_lock lock(_mutex);

        auto ctxIt = _bindings.find(context);
        if (ctxIt == _bindings.end()) {
            return std::nullopt;
        }

        for (const auto& [trigger, action] : ctxIt->second) {
            if (action == actionId) {
                return trigger;
            }
        }

        return std::nullopt;
    }

    void BindingManager::InitDefaultBindings()
    {
        logger::info("[DualPad][BindingMgr] Initializing default bindings");

        // Gameplay Ä¬ÈÏ°ó¶¨
        {
            Binding b;
            b.context = InputContext::Gameplay;
            b.actionId = "Game.OpenInventory";
            b.trigger.type = TriggerType::Gesture;
            b.trigger.code = 0x01000000; // TpLeftPress
            AddBinding(b);
        }

        {
            Binding b;
            b.context = InputContext::Gameplay;
            b.actionId = "Game.OpenMap";
            b.trigger.type = TriggerType::Gesture;
            b.trigger.code = 0x08000000; // TpSwipeUp
            AddBinding(b);
        }

        {
            Binding b;
            b.context = InputContext::Gameplay;
            b.actionId = "Game.OpenMagic";
            b.trigger.type = TriggerType::Gesture;
            b.trigger.code = 0x04000000; // TpRightPress
            AddBinding(b);
        }

        {
            Binding b;
            b.context = InputContext::Gameplay;
            b.actionId = "Game.QuickSave";
            b.trigger.type = TriggerType::Button;
            b.trigger.code = 0x00400000; // BackLeft
            AddBinding(b);
        }

        {
            Binding b;
            b.context = InputContext::Gameplay;
            b.actionId = "Game.QuickLoad";
            b.trigger.type = TriggerType::Button;
            b.trigger.code = 0x00800000; // BackRight
            AddBinding(b);
        }

        logger::info("[DualPad][BindingMgr] Default bindings initialized");
    }
}