#include "pch.h"
#include "input/injection/NativeInputInjector.h"

#include "input/Action.h"
#include "input/RuntimeConfig.h"
#include "input/injection/NativeInputPreControlMapHook.h"
#include "input/injection/PadEventSnapshotDispatcher.h"
#include "input/injection/SeInputEventQueueAccess.h"

#include <array>
#include <span>

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        using ContextID = RE::ControlMap::InputContextID;

        constexpr NativeButtonActionBinding kButtonBindings[] = {
            { actions::Jump, "Jump", ContextID::kGameplay, 0x8000 },
            { actions::Activate, "Activate", ContextID::kGameplay, 0x1000 },
            { actions::Sprint, "Sprint", ContextID::kGameplay, 0x0100 },
            { actions::Sneak, "Sneak", ContextID::kGameplay, 0x0040 },
            { actions::Shout, "Shout", ContextID::kGameplay, 0x0200 },
            { actions::TogglePOV, "Toggle POV", ContextID::kGameplay, 0x0080 },
        };

        constexpr std::array kGameplayCandidates = {
            "Left Attack/Block"sv,
            "Right Attack/Block"sv,
            "Activate"sv,
            "Ready Weapon"sv,
            "Tween Menu"sv,
            "Toggle POV"sv,
            "Jump"sv,
            "Sprint"sv,
            "Shout"sv,
            "Sneak"sv,
            "Favorites"sv,
            "Hotkey1"sv,
            "Hotkey2"sv,
            "Wait"sv,
            "Journal"sv
        };

        constexpr std::array kMenuCandidates = {
            "Accept"sv,
            "Cancel"sv,
            "Up"sv,
            "Down"sv,
            "Left"sv,
            "Right"sv
        };

        constexpr std::array kConsoleCandidates = {
            "PickPrevious"sv,
            "PickNext"sv,
            "NextFocus"sv,
            "PreviousFocus"sv
        };

        constexpr std::array kItemMenuCandidates = {
            "LeftEquip"sv,
            "RightEquip"sv,
            "Item Zoom"sv,
            "XButton"sv,
            "YButton"sv
        };

        constexpr std::array kInventoryCandidates = {
            "ChargeItem"sv
        };

        constexpr std::array kFavoritesCandidates = {
            "Accept"sv,
            "Cancel"sv,
            "Up"sv,
            "Down"sv
        };

        constexpr std::array kMapCandidates = {
            "Cancel"sv,
            "Click"sv,
            "PlayerPosition"sv,
            "LocalMap"sv,
            "Journal"sv,
            "Zoom In"sv,
            "Zoom Out"sv
        };

        constexpr std::array kStatsCandidates = {
            "Rotate"sv
        };

        constexpr std::array kBookCandidates = {
            "PrevPage"sv,
            "NextPage"sv
        };

        constexpr std::array kJournalCandidates = {
            "XButton"sv,
            "YButton"sv,
            "TabSwitch"sv
        };

        constexpr std::array kDebugOverlayCandidates = {
            "NextFocus"sv,
            "PreviousFocus"sv,
            "Up"sv,
            "Down"sv,
            "Left"sv,
            "Right"sv,
            "ToggleMinimize"sv,
            "ToggleMove"sv,
            "LTrigger"sv,
            "RTrigger"sv,
            "B"sv,
            "Y"sv,
            "X"sv
        };

        constexpr std::array kLockpickingCandidates = {
            "Cancel"sv,
            "DebugMode"sv
        };

        constexpr std::array kTfcCandidates = {
            "CameraZUp"sv,
            "CameraZDown"sv,
            "WorldZUp"sv,
            "WorldZDown"sv,
            "LockToZPlane"sv
        };

        constexpr std::array kFavorCandidates = {
            "Cancel"sv
        };

        std::span<const ContextID> ResolveContextSearchOrder(InputContext context)
        {
            static constexpr std::array kGameplay = { ContextID::kGameplay };
            static constexpr std::array kMenu = { ContextID::kMenuMode };
            static constexpr std::array kInventory = { ContextID::kInventory, ContextID::kItemMenu, ContextID::kMenuMode };
            static constexpr std::array kFavorites = { ContextID::kFavorites, ContextID::kMenuMode };
            static constexpr std::array kMap = { ContextID::kMap, ContextID::kMenuMode };
            static constexpr std::array kJournal = { ContextID::kJournal, ContextID::kMenuMode };
            static constexpr std::array kStats = { ContextID::kStats, ContextID::kMenuMode };
            static constexpr std::array kBook = { ContextID::kBook, ContextID::kMenuMode };
            static constexpr std::array kConsole = { ContextID::kConsole };
            static constexpr std::array kItemMenu = { ContextID::kItemMenu, ContextID::kMenuMode };
            static constexpr std::array kDebugText = { ContextID::kDebugText };
            static constexpr std::array kCursor = { ContextID::kCursor };
            static constexpr std::array kDebugOverlay = { ContextID::kDebugOverlay };
            static constexpr std::array kLockpicking = { ContextID::kLockpicking, ContextID::kMenuMode };
            static constexpr std::array kTfc = { ContextID::kTFCMode };
            static constexpr std::array kMapDebug = { ContextID::kMapDebug };
            static constexpr std::array kFavor = { ContextID::kFavor };

            switch (context) {
            case InputContext::Gameplay:
            case InputContext::Combat:
            case InputContext::Sneaking:
            case InputContext::Riding:
            case InputContext::Werewolf:
            case InputContext::VampireLord:
            case InputContext::Death:
            case InputContext::Bleedout:
            case InputContext::Ragdoll:
            case InputContext::KillMove:
                return kGameplay;

            case InputContext::Menu:
            case InputContext::DialogueMenu:
            case InputContext::TweenMenu:
            case InputContext::ContainerMenu:
            case InputContext::BarterMenu:
            case InputContext::TrainingMenu:
            case InputContext::LevelUpMenu:
            case InputContext::RaceSexMenu:
            case InputContext::MessageBoxMenu:
            case InputContext::QuantityMenu:
            case InputContext::GiftMenu:
            case InputContext::CreationsMenu:
                return kMenu;

            case InputContext::InventoryMenu:
            case InputContext::MagicMenu:
                return kInventory;

            case InputContext::FavoritesMenu:
                return kFavorites;

            case InputContext::MapMenu:
            case InputContext::MapMenuContext:
                return kMap;

            case InputContext::JournalMenu:
                return kJournal;

            case InputContext::StatsMenu:
            case InputContext::SkillMenu:
            case InputContext::Stats:
                return kStats;

            case InputContext::BookMenu:
            case InputContext::Book:
                return kBook;

            case InputContext::Console:
                return kConsole;

            case InputContext::ItemMenu:
                return kItemMenu;

            case InputContext::DebugText:
                return kDebugText;

            case InputContext::Cursor:
                return kCursor;

            case InputContext::DebugOverlay:
                return kDebugOverlay;

            case InputContext::Lockpicking:
                return kLockpicking;

            case InputContext::TFCMode:
                return kTfc;

            case InputContext::DebugMapMenu:
                return kMapDebug;

            case InputContext::Favor:
                return kFavor;

            default:
                return kGameplay;
            }
        }

        std::span<const std::string_view> GetFallbackCandidates(ContextID context)
        {
            switch (context) {
            case ContextID::kGameplay:
                return kGameplayCandidates;
            case ContextID::kMenuMode:
                return kMenuCandidates;
            case ContextID::kConsole:
                return kConsoleCandidates;
            case ContextID::kItemMenu:
                return kItemMenuCandidates;
            case ContextID::kInventory:
                return kInventoryCandidates;
            case ContextID::kFavorites:
                return kFavoritesCandidates;
            case ContextID::kMap:
            case ContextID::kMapDebug:
                return kMapCandidates;
            case ContextID::kStats:
                return kStatsCandidates;
            case ContextID::kBook:
                return kBookCandidates;
            case ContextID::kJournal:
                return kJournalCandidates;
            case ContextID::kDebugOverlay:
                return kDebugOverlayCandidates;
            case ContextID::kLockpicking:
                return kLockpickingCandidates;
            case ContextID::kTFCMode:
                return kTfcCandidates;
            case ContextID::kFavor:
                return kFavorCandidates;
            default:
                return {};
            }
        }

        RE::InputEvent* FindInputEventTail(RE::InputEvent* head)
        {
            auto* tail = head;
            while (tail && tail->next) {
                tail = tail->next;
            }
            return tail;
        }

        void DestroyInjectedButtonEvent(RE::ButtonEvent* event)
        {
            if (!event) {
                return;
            }

            event->~ButtonEvent();
            RE::free(event);
        }

    }

    void NativeInputInjector::Reset()
    {
        ClearStagedButtonEvents();
        ReleaseInjectedButtonEvents();
    }

    bool NativeInputInjector::CanHandleAction(std::string_view actionId) const
    {
        return ResolveButtonAction(actionId).has_value();
    }

    std::optional<NativeButtonActionBinding> NativeInputInjector::ResolveButtonAction(std::string_view actionId) const
    {
        for (const auto& binding : kButtonBindings) {
            if (binding.actionId == actionId) {
                return binding;
            }
        }

        return std::nullopt;
    }

    std::uint32_t NativeInputInjector::ResolveMappedGamepadId(const NativeButtonActionBinding& binding) const
    {
        auto* controlMap = RE::ControlMap::GetSingleton();
        if (!controlMap) {
            return binding.fallbackGamepadId;
        }

        const auto mapped = controlMap->GetMappedKey(binding.userEvent, RE::INPUT_DEVICE::kGamepad, binding.context);
        if (mapped != RE::ControlMap::kInvalid && mapped != 0xFF) {
            return mapped;
        }

        return binding.fallbackGamepadId;
    }

    bool NativeInputInjector::PulseButtonAction(std::string_view actionId)
    {
        return QueueButtonAction(actionId, true, 0.0f) &&
            QueueButtonAction(actionId, false, 0.016f);
    }

    bool NativeInputInjector::QueueButtonAction(std::string_view actionId, bool pressed, float heldSeconds)
    {
        if (!CanStageButtonEvents()) {
            return false;
        }

        const auto binding = ResolveButtonAction(actionId);
        if (!binding) {
            return false;
        }

        const auto gamepadId = ResolveMappedGamepadId(*binding);
        if (gamepadId == RE::ControlMap::kInvalid) {
            logger::debug("[DualPad][NativeInjector] No mapped gamepad id for action '{}'", actionId);
            return false;
        }

        const RE::BSFixedString userEvent(binding->userEvent.data());
        if (!StageButtonEvent(gamepadId, userEvent, pressed ? 1.0f : 0.0f, heldSeconds)) {
            return false;
        }

        if (IsDebugLoggingEnabled()) {
            logger::info(
                "[DualPad][NativeInjector] staged action={} id=0x{:04X} value={:.3f} held={:.3f}",
                actionId,
                gamepadId,
                pressed ? 1.0f : 0.0f,
                heldSeconds);
        }

        return true;
    }

    std::optional<RE::BSFixedString> NativeInputInjector::ResolveRawUserEvent(
        std::uint32_t gamepadId,
        InputContext context) const
    {
        auto* controlMap = RE::ControlMap::GetSingleton();
        if (!controlMap) {
            return std::nullopt;
        }

        const auto searchOrder = ResolveContextSearchOrder(context);
        for (const auto nativeContext : searchOrder) {
            const auto userEvent = controlMap->GetUserEventName(
                gamepadId,
                RE::INPUT_DEVICE::kGamepad,
                nativeContext);
            if (!userEvent.empty()) {
                return RE::BSFixedString(userEvent.data());
            }
        }

        for (const auto nativeContext : searchOrder) {
            for (const auto candidate : GetFallbackCandidates(nativeContext)) {
                if (controlMap->GetMappedKey(candidate, RE::INPUT_DEVICE::kGamepad, nativeContext) == gamepadId) {
                    return RE::BSFixedString(candidate.data());
                }
            }
        }

        return std::nullopt;
    }

    bool NativeInputInjector::QueueRawButton(
        std::uint32_t gamepadId,
        InputContext context,
        float value,
        float heldSeconds)
    {
        if (!CanStageButtonEvents()) {
            return false;
        }

        const auto userEvent = ResolveRawUserEvent(gamepadId, context);
        if (!userEvent) {
            return false;
        }

        if (!StageButtonEvent(gamepadId, *userEvent, value, heldSeconds)) {
            return false;
        }

        if (IsDebugLoggingEnabled()) {
            logger::info(
                "[DualPad][NativeInjector] staged raw context={} event={} id=0x{:04X} value={:.3f} held={:.3f}",
                ToString(context),
                userEvent->c_str(),
                gamepadId,
                value,
                heldSeconds);
        }

        return true;
    }

    std::size_t NativeInputInjector::FlushStagedButtonEventsToInputQueue()
    {
        auto* queue = RE::BSInputEventQueue::GetSingleton();
        if (!queue) {
            return 0;
        }

        std::size_t submittedCount = 0;
        std::size_t droppedCount = 0;
        auto& queueHead = detail::GetSEQueueHead(queue);
        auto& queueTail = detail::GetSEQueueTail(queue);

        for (std::size_t i = 0; i < _stagedButtonEventCount; ++i) {
            const auto& staged = _stagedButtonEvents[i];
            if (queue->buttonEventCount >= RE::BSInputEventQueue::MAX_BUTTON_EVENTS) {
                ++droppedCount;
                continue;
            }

            const auto slotIndex = queue->buttonEventCount++;
            auto* buttonEvent = detail::GetSEButtonEventSlot(queue, slotIndex);
            buttonEvent->Init(
                RE::INPUT_DEVICE::kGamepad,
                static_cast<std::int32_t>(staged.idCode),
                staged.value,
                staged.heldSeconds,
                staged.userEvent);
            buttonEvent->device = RE::INPUT_DEVICE::kGamepad;
            buttonEvent->eventType = RE::INPUT_EVENT_TYPE::kButton;
            buttonEvent->next = nullptr;

            if (!queueHead) {
                queueHead = buttonEvent;
            } else if (queueTail) {
                queueTail->next = buttonEvent;
            }
            queueTail = buttonEvent;
            ++submittedCount;
        }

        ClearStagedButtonEvents();

        if (droppedCount != 0) {
            logger::warn(
                "[DualPad][NativeInjector] Poll hook dropped {} staged button events because BSInputEventQueue is full",
                droppedCount);
        }

        if (submittedCount != 0 && IsDebugLoggingEnabled()) {
            logger::info(
                "[DualPad][NativeInjector] Submitted {} staged button events into BSInputEventQueue before PollInputDevices",
                submittedCount);
        }

        return submittedCount;
    }

    std::size_t NativeInputInjector::PrependStagedButtonEventsToInputQueue(
        RE::InputEvent*& head,
        RE::InputEvent*& tail)
    {
        if (_stagedButtonEventCount == 0) {
            return 0;
        }

        auto* queue = RE::BSInputEventQueue::GetSingleton();
        if (!queue) {
            logger::warn("[DualPad][NativeInjector] Independent input queue singleton is unavailable; dropping staged button events");
            ClearStagedButtonEvents();
            return 0;
        }

        auto& queueHead = detail::GetSEQueueHead(queue);
        auto& queueTail = detail::GetSEQueueTail(queue);
        RE::InputEvent* originalHead = head ? head : queueHead;
        RE::InputEvent* originalTail = tail ? tail : queueTail;

        if (originalHead && !originalTail) {
            originalTail = FindInputEventTail(originalHead);
        }

        if (head && queueHead && head != queueHead && IsDebugLoggingEnabled()) {
            logger::warn(
                "[DualPad][NativeInjector] ControlMap head (0x{:X}) differed from input queue singleton head (0x{:X}) before native prepend",
                reinterpret_cast<std::uintptr_t>(head),
                reinterpret_cast<std::uintptr_t>(queueHead));
        }

        RE::InputEvent* stagedHead = nullptr;
        RE::InputEvent* stagedTail = nullptr;
        std::size_t submittedCount = 0;
        std::size_t droppedCount = 0;

        for (std::size_t i = 0; i < _stagedButtonEventCount; ++i) {
            const auto& staged = _stagedButtonEvents[i];
            if (queue->buttonEventCount >= RE::BSInputEventQueue::MAX_BUTTON_EVENTS) {
                ++droppedCount;
                continue;
            }

            const auto slotIndex = queue->buttonEventCount++;
            auto* buttonEvent = detail::GetSEButtonEventSlot(queue, slotIndex);
            buttonEvent->Init(
                RE::INPUT_DEVICE::kGamepad,
                static_cast<std::int32_t>(staged.idCode),
                staged.value,
                staged.heldSeconds,
                staged.userEvent);
            buttonEvent->device = RE::INPUT_DEVICE::kGamepad;
            buttonEvent->eventType = RE::INPUT_EVENT_TYPE::kButton;
            buttonEvent->next = nullptr;

            if (!stagedHead) {
                stagedHead = buttonEvent;
            }
            else {
                stagedTail->next = buttonEvent;
            }
            stagedTail = buttonEvent;
            ++submittedCount;
        }

        ClearStagedButtonEvents();

        if (!stagedHead) {
            if (droppedCount != 0) {
                logger::warn(
                    "[DualPad][NativeInjector] Pre-ControlMap prepend dropped {} staged button events because the input queue singleton cache is full",
                    droppedCount);
            }
            return 0;
        }

        stagedTail->next = originalHead;
        head = stagedHead;
        tail = originalHead ? originalTail : stagedTail;
        queueHead = head;
        queueTail = tail;

        if (droppedCount != 0) {
            logger::warn(
                "[DualPad][NativeInjector] Pre-ControlMap prepend dropped {} staged button events because the input queue singleton cache is full",
                droppedCount);
        }

        if (IsDebugLoggingEnabled()) {
            logger::info(
                "[DualPad][NativeInjector] Prepended {} staged button events to the independent input queue singleton before ControlMap",
                submittedCount);
        }

        return submittedCount;
    }

    std::size_t NativeInputInjector::GetStagedButtonEventCount() const
    {
        return _stagedButtonEventCount;
    }

    void NativeInputInjector::DiscardStagedButtonEvents()
    {
        ClearStagedButtonEvents();
    }

    bool NativeInputInjector::StageButtonEvent(
        std::uint32_t gamepadId,
        const RE::BSFixedString& userEvent,
        float value,
        float heldSeconds)
    {
        const auto isPressedValue = [](float candidate) {
            return candidate > 0.0f;
        };

        for (std::size_t i = _stagedButtonEventCount; i > 0; --i) {
            auto& staged = _stagedButtonEvents[i - 1];
            if (staged.idCode != gamepadId || staged.userEvent != userEvent) {
                continue;
            }

            // Coalesce repeated state updates produced by multiple snapshots drained
            // in the same Poll cycle, but keep explicit press->release transitions.
            if (isPressedValue(staged.value) == isPressedValue(value)) {
                staged.value = value;
                staged.heldSeconds = heldSeconds;
                return true;
            }

            break;
        }

        if (_stagedButtonEventCount >= _stagedButtonEvents.size()) {
            logger::warn(
                "[DualPad][NativeInjector] Staged button buffer is full; dropping gamepad id 0x{:04X}",
                gamepadId);
            return false;
        }

        auto& staged = _stagedButtonEvents[_stagedButtonEventCount++];
        staged.userEvent = userEvent;
        staged.idCode = gamepadId;
        staged.value = value;
        staged.heldSeconds = heldSeconds;
        return true;
    }

    void NativeInputInjector::PrependStagedButtonEvents(RE::InputEvent*& head)
    {
        if (_stagedButtonEventCount == 0) {
            return;
        }

        ReleaseInjectedButtonEvents();
        const auto stagedCount = _stagedButtonEventCount;
        RE::InputEvent* stagedHead = nullptr;
        RE::InputEvent* stagedTail = nullptr;

        for (std::size_t i = 0; i < _stagedButtonEventCount; ++i) {
            const auto& staged = _stagedButtonEvents[i];
            auto* buttonEvent = RE::ButtonEvent::Create(
                RE::INPUT_DEVICE::kGamepad,
                staged.userEvent,
                staged.idCode,
                staged.value,
                staged.heldSeconds);
            if (!buttonEvent) {
                logger::warn(
                    "[DualPad][NativeInjector] Failed to allocate staged button event for id 0x{:04X}",
                    staged.idCode);
                continue;
            }

            if (_injectedButtonEventCount >= _injectedButtonEvents.size()) {
                logger::warn(
                    "[DualPad][NativeInjector] Injected button ownership buffer is full; dropping staged id 0x{:04X}",
                    staged.idCode);
                DestroyInjectedButtonEvent(buttonEvent);
                continue;
            }

            if (!stagedHead) {
                stagedHead = buttonEvent;
            }
            else {
                stagedTail->next = buttonEvent;
            }
            stagedTail = buttonEvent;
            _injectedButtonEvents[_injectedButtonEventCount++] = buttonEvent;
        }

        ClearStagedButtonEvents();

        if (!stagedHead) {
            return;
        }

        stagedTail->next = head;
        head = stagedHead;

        if (IsDebugLoggingEnabled()) {
            logger::info(
                "[DualPad][NativeInjector] Prepended {} staged button events to input chain",
                stagedCount);
        }
    }

    void NativeInputInjector::ReleaseInjectedButtonEvents()
    {
        for (std::size_t i = 0; i < _injectedButtonEventCount; ++i) {
            DestroyInjectedButtonEvent(_injectedButtonEvents[i]);
            _injectedButtonEvents[i] = nullptr;
        }

        _injectedButtonEventCount = 0;
    }

    void NativeInputInjector::ClearStagedButtonEvents()
    {
        for (std::size_t i = 0; i < _stagedButtonEventCount; ++i) {
            _stagedButtonEvents[i] = {};
        }

        _stagedButtonEventCount = 0;
    }

    bool NativeInputInjector::CanStageButtonEvents() const
    {
        return PadEventSnapshotDispatcher::GetSingleton().IsFramePumpEnabled() &&
            NativeInputPreControlMapHook::GetSingleton().CanInject();
    }

    bool NativeInputInjector::IsDebugLoggingEnabled() const
    {
        return RuntimeConfig::GetSingleton().LogNativeInjection();
    }
}
