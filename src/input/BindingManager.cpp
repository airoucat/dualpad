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

        Binding MakeAxisBinding(InputContext context, PadAxisId axis, std::string_view actionId)
        {
            Binding binding{};
            binding.context = context;
            binding.actionId = std::string(actionId);
            binding.trigger.type = TriggerType::Axis;
            binding.trigger.code = static_cast<std::uint32_t>(axis);
            return binding;
        }

        Binding MakeComboBinding(
            InputContext context,
            std::uint32_t code,
            std::initializer_list<std::uint32_t> modifiers,
            std::string_view actionId)
        {
            Binding binding{};
            binding.context = context;
            binding.actionId = std::string(actionId);
            binding.trigger.type = TriggerType::Combo;
            binding.trigger.code = code;
            binding.trigger.modifiers.assign(modifiers.begin(), modifiers.end());
            (std::sort)(binding.trigger.modifiers.begin(), binding.trigger.modifiers.end());
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

    // Explicit legacy defaults are retired; the standard fallback set below
    // now provides the baseline gameplay/menu bindings when no INI is present.
    void BindingManager::InitDefaultBindings()
    {
        logger::info("[DualPad][BindingMgr] Initializing default bindings");
        logger::info("[DualPad][BindingMgr] Legacy explicit default bindings retired; relying on standard fallback set");
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

        static constexpr std::array kGenericMenuContexts = {
            InputContext::Menu,
            InputContext::InventoryMenu,
            InputContext::MagicMenu,
            InputContext::TweenMenu,
            InputContext::ContainerMenu,
            InputContext::BarterMenu,
            InputContext::TrainingMenu,
            InputContext::LevelUpMenu,
            InputContext::RaceSexMenu,
            InputContext::StatsMenu,
            InputContext::SkillMenu,
            InputContext::MessageBoxMenu,
            InputContext::QuantityMenu,
            InputContext::GiftMenu
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

        const auto addBaseMenuBindings = [&](InputContext context) {
            addIfMissing(MakeButtonBinding(context, bits.cross, actions::MenuConfirm));
            addIfMissing(MakeButtonBinding(context, bits.circle, actions::MenuCancel));
            addIfMissing(MakeButtonBinding(context, bits.triangle, actions::MenuDownloadAll));
            addIfMissing(MakeButtonBinding(context, bits.dpadUp, actions::MenuScrollUp));
            addIfMissing(MakeButtonBinding(context, bits.dpadDown, actions::MenuScrollDown));
            addIfMissing(MakeButtonBinding(context, bits.dpadLeft, actions::MenuLeft));
            addIfMissing(MakeButtonBinding(context, bits.dpadRight, actions::MenuRight));
            addIfMissing(MakeAxisBinding(context, PadAxisId::LeftStickX, "Menu.LeftStick"));
            addIfMissing(MakeAxisBinding(context, PadAxisId::LeftStickY, "Menu.LeftStick"));
        };

        for (const auto context : kGameplayContexts) {
            addIfMissing(MakeButtonBinding(context, bits.cross, actions::Activate));
            addIfMissing(MakeButtonBinding(context, bits.square, actions::ReadyWeapon));
            addIfMissing(MakeButtonBinding(context, bits.circle, actions::TweenMenu));
            addIfMissing(MakeButtonBinding(context, bits.triangle, actions::Jump));
            addIfMissing(MakeButtonBinding(context, bits.l1, actions::Sprint));
            addIfMissing(MakeButtonBinding(context, bits.create, actions::Wait));
            addIfMissing(MakeButtonBinding(context, bits.l3, actions::Sneak));
            addIfMissing(MakeButtonBinding(context, bits.options, actions::OpenJournal));
            addIfMissing(MakeButtonBinding(context, bits.r1, actions::Shout));
            addIfMissing(MakeButtonBinding(context, bits.r3, actions::TogglePOV));
            addIfMissing(MakeButtonBinding(context, bits.dpadLeft, actions::Hotkey1));
            addIfMissing(MakeButtonBinding(context, bits.dpadRight, actions::Hotkey2));
            addIfMissing(MakeButtonBinding(context, bits.dpadUp, actions::Favorites));
            addIfMissing(MakeAxisBinding(context, PadAxisId::LeftStickX, "Game.Move"));
            addIfMissing(MakeAxisBinding(context, PadAxisId::LeftStickY, "Game.Move"));
            addIfMissing(MakeAxisBinding(context, PadAxisId::RightStickX, "Game.Look"));
            addIfMissing(MakeAxisBinding(context, PadAxisId::RightStickY, "Game.Look"));
            addIfMissing(MakeAxisBinding(context, PadAxisId::LeftTrigger, "Game.LeftTrigger"));
            addIfMissing(MakeAxisBinding(context, PadAxisId::RightTrigger, "Game.RightTrigger"));
        }

        for (const auto context : kGenericMenuContexts) {
            addBaseMenuBindings(context);
        }

        addIfMissing(MakeButtonBinding(InputContext::InventoryMenu, bits.r1, actions::InventoryChargeItem));

        addBaseMenuBindings(InputContext::DialogueMenu);
        addIfMissing(MakeButtonBinding(InputContext::DialogueMenu, bits.dpadUp, actions::DialoguePreviousOption));
        addIfMissing(MakeButtonBinding(InputContext::DialogueMenu, bits.dpadDown, actions::DialogueNextOption));

        addBaseMenuBindings(InputContext::FavoritesMenu);
        addIfMissing(MakeButtonBinding(InputContext::FavoritesMenu, bits.cross, actions::FavoritesAccept));
        addIfMissing(MakeButtonBinding(InputContext::FavoritesMenu, bits.circle, actions::FavoritesCancel));
        addIfMissing(MakeButtonBinding(InputContext::FavoritesMenu, bits.dpadUp, actions::FavoritesUp));
        addIfMissing(MakeButtonBinding(InputContext::FavoritesMenu, bits.dpadDown, actions::FavoritesDown));
        addIfMissing(MakeAxisBinding(InputContext::FavoritesMenu, PadAxisId::LeftStickX, actions::FavoritesLeftStick));
        addIfMissing(MakeAxisBinding(InputContext::FavoritesMenu, PadAxisId::LeftStickY, actions::FavoritesLeftStick));

        addBaseMenuBindings(InputContext::JournalMenu);
        addIfMissing(MakeButtonBinding(InputContext::JournalMenu, bits.square, actions::JournalXButton));
        addIfMissing(MakeButtonBinding(InputContext::JournalMenu, bits.triangle, actions::JournalYButton));
        addIfMissing(MakeAxisBinding(InputContext::JournalMenu, PadAxisId::LeftTrigger, actions::JournalTabLeft));
        addIfMissing(MakeAxisBinding(InputContext::JournalMenu, PadAxisId::RightTrigger, actions::JournalTabRight));

        addBaseMenuBindings(InputContext::BookMenu);
        addIfMissing(MakeButtonBinding(InputContext::BookMenu, bits.circle, actions::BookClose));
        addIfMissing(MakeButtonBinding(InputContext::BookMenu, bits.dpadLeft, actions::BookPreviousPage));
        addIfMissing(MakeButtonBinding(InputContext::BookMenu, bits.dpadRight, actions::BookNextPage));

        addBaseMenuBindings(InputContext::Book);
        addIfMissing(MakeButtonBinding(InputContext::Book, bits.circle, actions::BookClose));
        addIfMissing(MakeButtonBinding(InputContext::Book, bits.dpadLeft, actions::BookPreviousPage));
        addIfMissing(MakeButtonBinding(InputContext::Book, bits.dpadRight, actions::BookNextPage));

        addIfMissing(MakeButtonBinding(InputContext::MapMenu, bits.cross, actions::MapClick));
        addIfMissing(MakeButtonBinding(InputContext::MapMenu, bits.circle, actions::MapCancel));
        addIfMissing(MakeButtonBinding(InputContext::MapMenu, bits.square, actions::MapLocalMap));
        addIfMissing(MakeButtonBinding(InputContext::MapMenu, bits.triangle, actions::MapPlayerPosition));
        addIfMissing(MakeButtonBinding(InputContext::MapMenu, bits.dpadLeft, actions::MapOpenJournal));
        addIfMissing(MakeAxisBinding(InputContext::MapMenu, PadAxisId::LeftStickX, actions::MapCursor));
        addIfMissing(MakeAxisBinding(InputContext::MapMenu, PadAxisId::LeftStickY, actions::MapCursor));
        addIfMissing(MakeAxisBinding(InputContext::MapMenu, PadAxisId::RightStickX, actions::MapLook));
        addIfMissing(MakeAxisBinding(InputContext::MapMenu, PadAxisId::RightStickY, actions::MapLook));
        addIfMissing(MakeAxisBinding(InputContext::MapMenu, PadAxisId::LeftTrigger, actions::MapZoomOut));
        addIfMissing(MakeAxisBinding(InputContext::MapMenu, PadAxisId::RightTrigger, actions::MapZoomIn));

        addIfMissing(MakeButtonBinding(InputContext::Console, bits.cross, actions::ConsoleExecute));
        addIfMissing(MakeButtonBinding(InputContext::Console, bits.circle, actions::MenuCancel));
        addIfMissing(MakeButtonBinding(InputContext::Console, bits.dpadUp, actions::ConsolePickNext));
        addIfMissing(MakeButtonBinding(InputContext::Console, bits.dpadDown, actions::ConsolePickPrevious));
        addIfMissing(MakeButtonBinding(InputContext::Console, bits.l1, actions::ConsolePreviousFocus));
        addIfMissing(MakeButtonBinding(InputContext::Console, bits.r1, actions::ConsoleNextFocus));

        addIfMissing(MakeAxisBinding(InputContext::ItemMenu, PadAxisId::LeftTrigger, actions::ItemLeftEquip));
        addIfMissing(MakeAxisBinding(InputContext::ItemMenu, PadAxisId::RightTrigger, actions::ItemRightEquip));
        addIfMissing(MakeButtonBinding(InputContext::ItemMenu, bits.r3, actions::ItemZoom));
        addIfMissing(MakeButtonBinding(InputContext::ItemMenu, bits.square, actions::ItemXButton));
        addIfMissing(MakeButtonBinding(InputContext::ItemMenu, bits.triangle, actions::ItemYButton));
        addIfMissing(MakeAxisBinding(InputContext::ItemMenu, PadAxisId::RightStickX, actions::ItemRotate));
        addIfMissing(MakeAxisBinding(InputContext::ItemMenu, PadAxisId::RightStickY, actions::ItemRotate));

        addIfMissing(MakeAxisBinding(InputContext::Stats, PadAxisId::LeftStickX, actions::StatsRotate));
        addIfMissing(MakeAxisBinding(InputContext::Stats, PadAxisId::LeftStickY, actions::StatsRotate));

        addIfMissing(MakeAxisBinding(InputContext::Cursor, PadAxisId::RightStickX, actions::CursorMove));
        addIfMissing(MakeAxisBinding(InputContext::Cursor, PadAxisId::RightStickY, actions::CursorMove));
        addIfMissing(MakeButtonBinding(InputContext::Cursor, bits.cross, actions::CursorClick));

        addIfMissing(MakeButtonBinding(InputContext::DebugOverlay, bits.l1, actions::DebugOverlayPreviousFocus));
        addIfMissing(MakeButtonBinding(InputContext::DebugOverlay, bits.r1, actions::DebugOverlayNextFocus));
        addIfMissing(MakeButtonBinding(InputContext::DebugOverlay, bits.dpadUp, actions::DebugOverlayUp));
        addIfMissing(MakeButtonBinding(InputContext::DebugOverlay, bits.dpadDown, actions::DebugOverlayDown));
        addIfMissing(MakeButtonBinding(InputContext::DebugOverlay, bits.dpadLeft, actions::DebugOverlayLeft));
        addIfMissing(MakeButtonBinding(InputContext::DebugOverlay, bits.dpadRight, actions::DebugOverlayRight));
        addIfMissing(MakeButtonBinding(InputContext::DebugOverlay, bits.create, actions::DebugOverlayToggleMinimize));
        addIfMissing(MakeButtonBinding(InputContext::DebugOverlay, bits.r3, actions::DebugOverlayToggleMove));
        addIfMissing(MakeButtonBinding(InputContext::DebugOverlay, bits.circle, actions::DebugOverlayB));
        addIfMissing(MakeButtonBinding(InputContext::DebugOverlay, bits.triangle, actions::DebugOverlayY));
        addIfMissing(MakeButtonBinding(InputContext::DebugOverlay, bits.square, actions::DebugOverlayX));
        addIfMissing(MakeAxisBinding(InputContext::DebugOverlay, PadAxisId::LeftTrigger, actions::DebugOverlayLeftTrigger));
        addIfMissing(MakeAxisBinding(InputContext::DebugOverlay, PadAxisId::RightTrigger, actions::DebugOverlayRightTrigger));

        addIfMissing(MakeButtonBinding(InputContext::TFCMode, bits.square, actions::TFCLockToZPlane));
        addIfMissing(MakeButtonBinding(InputContext::TFCMode, bits.l1, actions::TFCWorldZDown));
        addIfMissing(MakeButtonBinding(InputContext::TFCMode, bits.r1, actions::TFCWorldZUp));
        addIfMissing(MakeAxisBinding(InputContext::TFCMode, PadAxisId::LeftTrigger, actions::TFCCameraZDown));
        addIfMissing(MakeAxisBinding(InputContext::TFCMode, PadAxisId::RightTrigger, actions::TFCCameraZUp));

        addIfMissing(MakeAxisBinding(InputContext::DebugMapMenu, PadAxisId::LeftStickX, actions::DebugMapMove));
        addIfMissing(MakeAxisBinding(InputContext::DebugMapMenu, PadAxisId::LeftStickY, actions::DebugMapMove));
        addIfMissing(MakeAxisBinding(InputContext::DebugMapMenu, PadAxisId::RightStickX, actions::DebugMapLook));
        addIfMissing(MakeAxisBinding(InputContext::DebugMapMenu, PadAxisId::RightStickY, actions::DebugMapLook));
        addIfMissing(MakeAxisBinding(InputContext::DebugMapMenu, PadAxisId::LeftTrigger, actions::DebugMapZoomOut));
        addIfMissing(MakeAxisBinding(InputContext::DebugMapMenu, PadAxisId::RightTrigger, actions::DebugMapZoomIn));

        addIfMissing(MakeButtonBinding(InputContext::Lockpicking, bits.circle, actions::LockpickingCancel));
        addIfMissing(MakeButtonBinding(InputContext::Lockpicking, bits.square, actions::LockpickingDebugMode));
        addIfMissing(MakeAxisBinding(InputContext::Lockpicking, PadAxisId::LeftStickX, actions::LockpickingRotatePick));
        addIfMissing(MakeAxisBinding(InputContext::Lockpicking, PadAxisId::LeftStickY, actions::LockpickingRotatePick));
        addIfMissing(MakeAxisBinding(InputContext::Lockpicking, PadAxisId::RightStickX, actions::LockpickingRotateLock));
        addIfMissing(MakeAxisBinding(InputContext::Lockpicking, PadAxisId::RightStickY, actions::LockpickingRotateLock));

        addIfMissing(MakeButtonBinding(InputContext::CreationsMenu, bits.cross, actions::CreationsAccept));
        addIfMissing(MakeButtonBinding(InputContext::CreationsMenu, bits.circle, actions::CreationsCancel));
        addIfMissing(MakeButtonBinding(InputContext::CreationsMenu, bits.dpadUp, actions::CreationsUp));
        addIfMissing(MakeButtonBinding(InputContext::CreationsMenu, bits.dpadDown, actions::CreationsDown));
        addIfMissing(MakeButtonBinding(InputContext::CreationsMenu, bits.dpadLeft, actions::CreationsLeft));
        addIfMissing(MakeButtonBinding(InputContext::CreationsMenu, bits.dpadRight, actions::CreationsRight));
        addIfMissing(MakeButtonBinding(InputContext::CreationsMenu, bits.options, actions::CreationsOptions));
        addIfMissing(MakeButtonBinding(InputContext::CreationsMenu, bits.triangle, actions::CreationsLoadOrderAndDelete));
        addIfMissing(MakeButtonBinding(InputContext::CreationsMenu, bits.r1, actions::CreationsLikeUnlike));
        addIfMissing(MakeButtonBinding(InputContext::CreationsMenu, bits.l1, actions::CreationsSearchEdit));
        addIfMissing(MakeButtonBinding(InputContext::CreationsMenu, bits.square, actions::CreationsPurchaseCredits));
        addIfMissing(MakeAxisBinding(InputContext::CreationsMenu, PadAxisId::LeftStickX, actions::CreationsLeftStick));
        addIfMissing(MakeAxisBinding(InputContext::CreationsMenu, PadAxisId::LeftStickY, actions::CreationsLeftStick));
        addIfMissing(MakeAxisBinding(InputContext::CreationsMenu, PadAxisId::LeftTrigger, actions::CreationsCategorySideBar));
        addIfMissing(MakeAxisBinding(InputContext::CreationsMenu, PadAxisId::RightTrigger, actions::CreationsFilter));

        addIfMissing(MakeButtonBinding(InputContext::Favor, bits.circle, actions::FavorCancel));

        if (addedCount != 0) {
            logger::info("[DualPad][BindingMgr] Added {} standard fallback bindings", addedCount);
        }

        return addedCount;
    }
}
