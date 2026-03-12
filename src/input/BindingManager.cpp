#include "pch.h"
#include "input/BindingManager.h"
#include "input/Action.h"
#include "input/PadProfile.h"
#include "input/mapping/PadEvent.h"
#include <SKSE/SKSE.h>

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        Binding MakeButtonBinding(InputContext context, std::uint32_t code, std::string_view actionId)
        {
            Binding binding{};
            binding.context = context;
            binding.actionId = std::string(actionId);
            binding.trigger.type = TriggerType::Button;
            binding.trigger.code = code;
            return binding;
        }

        std::uint32_t ModifiersToMask(const std::vector<std::uint32_t>& modifiers)
        {
            std::uint32_t mask = 0;
            for (const auto modifier : modifiers) {
                mask |= modifier;
            }
            return mask;
        }
    }

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

    void BindingManager::AddBindingIfMissing(const Binding& binding)
    {
        std::scoped_lock lock(_mutex);

        auto& contextBindings = _bindings[binding.context];
        if (contextBindings.contains(binding.trigger)) {
            return;
        }

        contextBindings.emplace(binding.trigger, binding.actionId);
        logger::trace("[DualPad][BindingMgr] Added fallback binding: context={} trigger={:08X} action={}",
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

    void BindingManager::ClearBindings()
    {
        std::scoped_lock lock(_mutex);
        _bindings.clear();
    }

    void BindingManager::MergeBindings(InputContext sourceContext, InputContext targetContext, bool overwriteExisting)
    {
        std::scoped_lock lock(_mutex);

        const auto sourceIt = _bindings.find(sourceContext);
        if (sourceIt == _bindings.end()) {
            return;
        }

        auto& targetBindings = _bindings[targetContext];
        for (const auto& [trigger, actionId] : sourceIt->second) {
            if (!overwriteExisting && targetBindings.contains(trigger)) {
                continue;
            }

            targetBindings[trigger] = actionId;
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

    std::optional<Binding> BindingManager::FindBestBindingForTriggerSubset(
        const Trigger& trigger,
        InputContext context,
        bool allowEmptyModifiers) const
    {
        std::scoped_lock lock(_mutex);

        auto ctxIt = _bindings.find(context);
        if (ctxIt == _bindings.end()) {
            return std::nullopt;
        }

        const auto requestedMask = ModifiersToMask(trigger.modifiers);
        const auto requestedModifierCount = trigger.modifiers.size();

        const Trigger* bestTrigger = nullptr;
        const std::string* bestActionId = nullptr;
        std::uint32_t bestModifierMask = 0;
        std::size_t bestModifierCount = 0;

        for (const auto& [candidateTrigger, actionId] : ctxIt->second) {
            if (candidateTrigger.type != trigger.type || candidateTrigger.code != trigger.code) {
                continue;
            }

            const auto candidateModifierCount = candidateTrigger.modifiers.size();
            if (candidateModifierCount > requestedModifierCount) {
                continue;
            }
            if (!allowEmptyModifiers && candidateModifierCount == 0) {
                continue;
            }

            const auto candidateModifierMask = ModifiersToMask(candidateTrigger.modifiers);
            if ((candidateModifierMask & ~requestedMask) != 0) {
                continue;
            }

            if (!bestTrigger ||
                candidateModifierCount > bestModifierCount ||
                (candidateModifierCount == bestModifierCount &&
                    candidateModifierMask > bestModifierMask)) {
                bestTrigger = &candidateTrigger;
                bestActionId = &actionId;
                bestModifierMask = candidateModifierMask;
                bestModifierCount = candidateModifierCount;
            }
        }

        if (!bestTrigger || !bestActionId) {
            return std::nullopt;
        }

        Binding binding{};
        binding.trigger = *bestTrigger;
        binding.actionId = *bestActionId;
        binding.context = context;
        return binding;
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

    // These defaults keep the plugin usable when no INI file is present.
    void BindingManager::InitDefaultBindings()
    {
        logger::info("[DualPad][BindingMgr] Initializing default bindings");

        {
            Binding b;
            b.context = InputContext::Gameplay;
            b.actionId = "Game.OpenInventory";
            b.trigger.type = TriggerType::Gesture;
            b.trigger.code = mapping_codes::kTpLeftPress;
            AddBinding(b);
        }

        {
            Binding b;
            b.context = InputContext::Gameplay;
            b.actionId = "Game.OpenMap";
            b.trigger.type = TriggerType::Gesture;
            b.trigger.code = mapping_codes::kTpSwipeUp;
            AddBinding(b);
        }

        {
            Binding b;
            b.context = InputContext::Gameplay;
            b.actionId = "Game.OpenMagic";
            b.trigger.type = TriggerType::Gesture;
            b.trigger.code = mapping_codes::kTpRightPress;
            AddBinding(b);
        }

        {
            Binding b;
            b.context = InputContext::Gameplay;
            b.actionId = "Game.QuickSave";
            b.trigger.type = TriggerType::Button;
            b.trigger.code = 0x00400000;
            AddBinding(b);
        }

        {
            Binding b;
            b.context = InputContext::Gameplay;
            b.actionId = "Game.QuickLoad";
            b.trigger.type = TriggerType::Button;
            b.trigger.code = 0x00800000;
            AddBinding(b);
        }

        logger::info("[DualPad][BindingMgr] Default bindings initialized");
    }

    std::size_t BindingManager::ApplyStandardFallbackBindings()
    {
        const auto& bits = GetPadBits(GetActivePadProfile());

        static constexpr std::array kGameplayContexts = {
            InputContext::Gameplay,
            InputContext::Combat,
            InputContext::Sneaking,
            InputContext::Riding,
            InputContext::Werewolf,
            InputContext::VampireLord
        };

        static constexpr std::array kMenuContexts = {
            InputContext::Menu,
            InputContext::InventoryMenu,
            InputContext::MagicMenu,
            InputContext::MapMenu,
            InputContext::JournalMenu,
            InputContext::DialogueMenu,
            InputContext::FavoritesMenu,
            InputContext::TweenMenu,
            InputContext::ContainerMenu,
            InputContext::BarterMenu,
            InputContext::TrainingMenu,
            InputContext::LevelUpMenu,
            InputContext::RaceSexMenu,
            InputContext::StatsMenu,
            InputContext::SkillMenu,
            InputContext::BookMenu,
            InputContext::MessageBoxMenu,
            InputContext::QuantityMenu,
            InputContext::GiftMenu,
            InputContext::CreationsMenu,
            InputContext::Book
        };

        std::size_t addedCount = 0;
        const auto addIfMissing = [&](const Binding& binding) {
            std::scoped_lock lock(_mutex);
            auto& contextBindings = _bindings[binding.context];
            if (contextBindings.contains(binding.trigger)) {
                return;
            }

            contextBindings.emplace(binding.trigger, binding.actionId);
            ++addedCount;
        };

        for (const auto context : kGameplayContexts) {
            addIfMissing(MakeButtonBinding(context, bits.cross, actions::Activate));
            addIfMissing(MakeButtonBinding(context, bits.triangle, actions::Jump));
            addIfMissing(MakeButtonBinding(context, bits.l1, actions::Sprint));
            addIfMissing(MakeButtonBinding(context, bits.l3, actions::Sneak));
            addIfMissing(MakeButtonBinding(context, bits.r1, actions::Shout));
            addIfMissing(MakeButtonBinding(context, bits.r3, actions::TogglePOV));
        }

        for (const auto context : kMenuContexts) {
            addIfMissing(MakeButtonBinding(context, bits.cross, actions::MenuConfirm));
            addIfMissing(MakeButtonBinding(context, bits.circle, actions::MenuCancel));
            addIfMissing(MakeButtonBinding(context, bits.dpadUp, actions::MenuScrollUp));
            addIfMissing(MakeButtonBinding(context, bits.dpadDown, actions::MenuScrollDown));
            addIfMissing(MakeButtonBinding(context, bits.dpadLeft, actions::MenuLeft));
            addIfMissing(MakeButtonBinding(context, bits.dpadRight, actions::MenuRight));
            addIfMissing(MakeButtonBinding(context, bits.l1, actions::MenuPageUp));
            addIfMissing(MakeButtonBinding(context, bits.r1, actions::MenuPageDown));
        }

        if (addedCount != 0) {
            logger::info("[DualPad][BindingMgr] Added {} standard fallback bindings", addedCount);
        }

        return addedCount;
    }
}
