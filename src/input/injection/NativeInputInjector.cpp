#include "pch.h"
#include "input/injection/NativeInputInjector.h"

#include "input/Action.h"
#include "input/PadProfile.h"
#include "input/RuntimeConfig.h"
#include "input/injection/NativeInputPreControlMapHook.h"
#include "input/injection/PadEventSnapshotDispatcher.h"
#include "input/injection/SeInputEventQueueAccess.h"

#include <array>
#include <cmath>
#include <span>

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        using ContextID = RE::ControlMap::InputContextID;

        struct NativeRawButtonBinding
        {
            std::uint32_t padBit{ 0 };
            std::uint32_t gamepadId{ RE::ControlMap::kInvalid };
        };

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

        constexpr float kAxisEpsilon = 0.0001f;
        constexpr float kThumbstickDeadzone = 0.08f;
        constexpr float kThumbstickDeltaEpsilon = 0.01f;

        float ApplyThumbstickDeadzone(float value)
        {
            return std::fabs(value) >= kThumbstickDeadzone ? value : 0.0f;
        }

        std::span<const NativeRawButtonBinding> GetDigitalButtonBindings()
        {
            static const auto bindings = []() {
                const auto& bits = GetPadBits(GetActivePadProfile());
                return std::array{
                    NativeRawButtonBinding{ bits.dpadUp, 0x0001 },
                    NativeRawButtonBinding{ bits.dpadDown, 0x0002 },
                    NativeRawButtonBinding{ bits.dpadLeft, 0x0004 },
                    NativeRawButtonBinding{ bits.dpadRight, 0x0008 },
                    NativeRawButtonBinding{ bits.options, 0x0010 },
                    NativeRawButtonBinding{ bits.create, 0x0020 },
                    NativeRawButtonBinding{ bits.l3, 0x0040 },
                    NativeRawButtonBinding{ bits.r3, 0x0080 },
                    NativeRawButtonBinding{ bits.l1, 0x0100 },
                    NativeRawButtonBinding{ bits.r1, 0x0200 },
                    NativeRawButtonBinding{ bits.cross, 0x1000 },
                    NativeRawButtonBinding{ bits.circle, 0x2000 },
                    NativeRawButtonBinding{ bits.square, 0x4000 },
                    NativeRawButtonBinding{ bits.triangle, 0x8000 },
                };
            }();

            return bindings;
        }

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

        float ComputeReleaseHeldSeconds(const SyntheticButtonState& button)
        {
            if (button.releasedAtUs > button.pressedAtUs && button.pressedAtUs != 0) {
                return static_cast<float>(button.releasedAtUs - button.pressedAtUs) / 1000000.0f;
            }

            return button.heldSeconds;
        }

        bool ShouldQueueThumbstick(
            const SyntheticAxisState& xAxis,
            const SyntheticAxisState& yAxis,
            float lastX,
            float lastY,
            bool wasActive,
            float& filteredX,
            float& filteredY,
            bool& isActive)
        {
            filteredX = ApplyThumbstickDeadzone(xAxis.value);
            filteredY = ApplyThumbstickDeadzone(yAxis.value);
            isActive = std::fabs(filteredX) > kAxisEpsilon || std::fabs(filteredY) > kAxisEpsilon;

            if (!isActive && !wasActive) {
                return false;
            }

            const bool activeChanged = isActive != wasActive;
            const bool deltaChanged =
                std::fabs(filteredX - lastX) > kThumbstickDeltaEpsilon ||
                std::fabs(filteredY - lastY) > kThumbstickDeltaEpsilon;

            return activeChanged || deltaChanged || ((xAxis.changed || yAxis.changed) && isActive);
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
        _submittedDownMask = 0;
        _pendingDigitalReleaseMask = 0;
        _leftTriggerPressedAtUs = 0;
        _rightTriggerPressedAtUs = 0;
        _pendingDigitalReleaseHeldSeconds.fill(0.0f);
        _lastLeftThumbX = 0.0f;
        _lastLeftThumbY = 0.0f;
        _lastRightThumbX = 0.0f;
        _lastRightThumbY = 0.0f;
        _leftThumbActive = false;
        _rightThumbActive = false;
    }

    std::uint32_t NativeInputInjector::SubmitDigitalButtons(const SyntheticPadFrame& frame, std::uint32_t handledButtons)
    {
        if (!IsAvailable()) {
            return 0;
        }

        return SubmitDigitalButtonsInternal(frame, handledButtons);
    }

    void NativeInputInjector::SubmitFrame(const SyntheticPadFrame& frame, std::uint32_t handledButtons)
    {
        if (!IsAvailable()) {
            return;
        }

        (void)SubmitDigitalButtonsInternal(frame, handledButtons);

        const auto& bits = GetPadBits(GetActivePadProfile());
        const auto submitTrigger = [&](std::uint32_t padBit,
                                   std::uint32_t gamepadId,
                                   const SyntheticAxisState& axisState,
                                   std::uint64_t& pressedAtUs) {
            const bool blocked = (handledButtons & padBit) != 0;
            const bool active = (_submittedDownMask & padBit) != 0;
            const float value = std::clamp(axisState.value, 0.0f, 1.0f);

            if (blocked) {
                if (active &&
                    QueueRawButton(gamepadId, frame.context, 0.0f,
                        pressedAtUs != 0 && frame.sourceTimestampUs >= pressedAtUs ?
                            static_cast<float>(frame.sourceTimestampUs - pressedAtUs) / 1000000.0f :
                            0.0f)) {
                    _submittedDownMask &= ~padBit;
                    pressedAtUs = 0;
                }
                return;
            }

            if (value > kAxisEpsilon) {
                if (!active) {
                    pressedAtUs = frame.sourceTimestampUs;
                }

                const auto heldSeconds =
                    pressedAtUs != 0 && frame.sourceTimestampUs >= pressedAtUs ?
                    static_cast<float>(frame.sourceTimestampUs - pressedAtUs) / 1000000.0f :
                    0.0f;

                if (QueueRawButton(gamepadId, frame.context, value, active ? heldSeconds : 0.0f)) {
                    _submittedDownMask |= padBit;
                }
                return;
            }

            if ((axisState.changed || active) && active) {
                const auto heldSeconds =
                    pressedAtUs != 0 && frame.sourceTimestampUs >= pressedAtUs ?
                    static_cast<float>(frame.sourceTimestampUs - pressedAtUs) / 1000000.0f :
                    0.0f;

                if (QueueRawButton(gamepadId, frame.context, 0.0f, heldSeconds)) {
                    _submittedDownMask &= ~padBit;
                    pressedAtUs = 0;
                }
            }
        };

        submitTrigger(bits.l2Button, 0x0009, frame.leftTrigger, _leftTriggerPressedAtUs);
        submitTrigger(bits.r2Button, 0x000A, frame.rightTrigger, _rightTriggerPressedAtUs);

        const auto submitThumbstick = [&](RE::ThumbstickEvent::InputType inputType,
                                      const SyntheticAxisState& xAxis,
                                      const SyntheticAxisState& yAxis,
                                      float& lastX,
                                      float& lastY,
                                      bool& wasActive) {
            float filteredX = 0.0f;
            float filteredY = 0.0f;
            bool isActive = false;
            if (!ShouldQueueThumbstick(
                    xAxis,
                    yAxis,
                    lastX,
                    lastY,
                    wasActive,
                    filteredX,
                    filteredY,
                    isActive)) {
                return;
            }

            if (QueueThumbstick(inputType, filteredX, filteredY)) {
                lastX = filteredX;
                lastY = filteredY;
                wasActive = isActive;
            }
        };

        submitThumbstick(
            RE::ThumbstickEvent::InputType::kLeftThumbstick,
            frame.leftStickX,
            frame.leftStickY,
            _lastLeftThumbX,
            _lastLeftThumbY,
            _leftThumbActive);

        submitThumbstick(
            RE::ThumbstickEvent::InputType::kRightThumbstick,
            frame.rightStickX,
            frame.rightStickY,
            _lastRightThumbX,
            _lastRightThumbY,
            _rightThumbActive);
    }

    std::uint32_t NativeInputInjector::SubmitDigitalButtonsInternal(
        const SyntheticPadFrame& frame,
        std::uint32_t handledButtons)
    {
        std::uint32_t nativeHandledButtons = 0;

        for (const auto& binding : GetDigitalButtonBindings()) {
            const auto bitIndex = static_cast<std::size_t>(std::countr_zero(binding.padBit));
            const auto& button = frame.buttons[bitIndex];
            const bool blocked = (handledButtons & binding.padBit) != 0;
            const bool wasSubmitted = (_submittedDownMask & binding.padBit) != 0;
            const bool hasPendingRelease = (_pendingDigitalReleaseMask & binding.padBit) != 0;
            const bool pulsed = (frame.pulseMask & binding.padBit) != 0;

            if (!blocked && pulsed && !wasSubmitted) {
                if (QueueRawButton(binding.gamepadId, frame.context, 1.0f, 0.0f)) {
                    nativeHandledButtons |= binding.padBit;
                    _submittedDownMask |= binding.padBit;

                    if (QueueRawButton(binding.gamepadId, frame.context, 0.0f, 0.016f)) {
                        _submittedDownMask &= ~binding.padBit;
                        _pendingDigitalReleaseMask &= ~binding.padBit;
                        _pendingDigitalReleaseHeldSeconds[bitIndex] = 0.0f;
                    }
                    else {
                        _pendingDigitalReleaseMask |= binding.padBit;
                        _pendingDigitalReleaseHeldSeconds[bitIndex] = 0.016f;
                    }
                }
                continue;
            }

            if (!blocked && button.down) {
                _pendingDigitalReleaseMask &= ~binding.padBit;
                _pendingDigitalReleaseHeldSeconds[bitIndex] = 0.0f;

                const auto heldSeconds = button.pressed ? 0.0f : button.heldSeconds;
                if (!wasSubmitted) {
                    if (QueueRawButton(binding.gamepadId, frame.context, 1.0f, heldSeconds)) {
                        _submittedDownMask |= binding.padBit;
                        nativeHandledButtons |= binding.padBit;
                    }
                }
                else {
                    nativeHandledButtons |= binding.padBit;
                    (void)QueueRawButton(binding.gamepadId, frame.context, 1.0f, heldSeconds);
                }
                continue;
            }

            if (wasSubmitted || hasPendingRelease) {
                nativeHandledButtons |= binding.padBit;

                const auto releaseHeldSeconds = hasPendingRelease ?
                    _pendingDigitalReleaseHeldSeconds[bitIndex] :
                    ComputeReleaseHeldSeconds(button);

                if (QueueRawButton(binding.gamepadId, frame.context, 0.0f, releaseHeldSeconds)) {
                    _submittedDownMask &= ~binding.padBit;
                    _pendingDigitalReleaseMask &= ~binding.padBit;
                    _pendingDigitalReleaseHeldSeconds[bitIndex] = 0.0f;
                }
                else {
                    _pendingDigitalReleaseMask |= binding.padBit;
                    _pendingDigitalReleaseHeldSeconds[bitIndex] = releaseHeldSeconds;
                }
            }
        }

        return nativeHandledButtons;
    }

    bool NativeInputInjector::IsAvailable() const
    {
        return RE::ControlMap::GetSingleton() != nullptr;
    }

    bool NativeInputInjector::ShouldUseAsMainPath() const
    {
        return RuntimeConfig::GetSingleton().UseNativeFrameInjector();
    }

    bool NativeInputInjector::ShouldUseForButtonActions() const
    {
        const auto& config = RuntimeConfig::GetSingleton();
        return config.UseNativeButtonInjector() || config.UseNativeFrameInjector();
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

    bool NativeInputInjector::QueueThumbstick(
        RE::ThumbstickEvent::InputType inputType,
        float xValue,
        float yValue) const
    {
        auto* queue = RE::BSInputEventQueue::GetSingleton();
        if (!queue) {
            return false;
        }

        if (queue->thumbstickEventCount >= RE::BSInputEventQueue::MAX_THUMBSTICK_EVENTS) {
            logger::warn("[DualPad][NativeInjector] Thumbstick event queue is full; dropping native axis event");
            return false;
        }

        queue->AddThumbstickEvent(inputType, RE::INPUT_DEVICE::kGamepad, xValue, yValue);

        if (IsDebugLoggingEnabled()) {
            logger::info(
                "[DualPad][NativeInjector] thumbstick={} x={:.3f} y={:.3f}",
                inputType == RE::ThumbstickEvent::InputType::kLeftThumbstick ? "Left" : "Right",
                xValue,
                yValue);
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

    std::size_t NativeInputInjector::PrependStagedButtonEventsUsingQueueCache(RE::InputEvent*& head)
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
        RE::InputEvent* stagedHead = nullptr;
        RE::InputEvent* stagedTail = nullptr;
        std::size_t submittedCount = 0;
        std::size_t droppedCount = 0;
        const bool logDebug = IsDebugLoggingEnabled();

        if (logDebug) {
            logger::info(
                "[DualPad][NativeInjector] HeadPrepend begin staged={} head=0x{:X} buttonCount={} queueHead=0x{:X} queueTail=0x{:X}",
                _stagedButtonEventCount,
                reinterpret_cast<std::uintptr_t>(head),
                queue->buttonEventCount,
                reinterpret_cast<std::uintptr_t>(queueHead),
                reinterpret_cast<std::uintptr_t>(queueTail));
        }

        for (std::size_t i = 0; i < _stagedButtonEventCount; ++i) {
            const auto& staged = _stagedButtonEvents[i];
            if (queue->buttonEventCount >= RE::BSInputEventQueue::MAX_BUTTON_EVENTS) {
                ++droppedCount;
                continue;
            }

            const auto slotIndex = queue->buttonEventCount++;
            auto* buttonEvent = detail::GetSEButtonEventSlot(queue, slotIndex);
            if (logDebug) {
                logger::info(
                    "[DualPad][NativeInjector] HeadPrepend slot idx={} slotIndex={} ptr=0x{:X} id=0x{:04X} value={:.3f} held={:.3f} event={}",
                    i,
                    slotIndex,
                    reinterpret_cast<std::uintptr_t>(buttonEvent),
                    staged.idCode,
                    staged.value,
                    staged.heldSeconds,
                    staged.userEvent.empty() ? "" : staged.userEvent.c_str());
            }

            buttonEvent->Init(
                RE::INPUT_DEVICE::kGamepad,
                static_cast<std::int32_t>(staged.idCode),
                staged.value,
                staged.heldSeconds,
                staged.userEvent);
            buttonEvent->device = RE::INPUT_DEVICE::kGamepad;
            buttonEvent->eventType = RE::INPUT_EVENT_TYPE::kButton;
            buttonEvent->next = nullptr;

            if (logDebug) {
                logger::info(
                    "[DualPad][NativeInjector] HeadPrepend initialized idx={} ptr=0x{:X} next=0x{:X}",
                    i,
                    reinterpret_cast<std::uintptr_t>(buttonEvent),
                    reinterpret_cast<std::uintptr_t>(buttonEvent->next));
            }

            if (!stagedHead) {
                stagedHead = buttonEvent;
            } else {
                stagedTail->next = buttonEvent;
            }
            stagedTail = buttonEvent;
            ++submittedCount;
        }

        ClearStagedButtonEvents();

        if (!stagedHead) {
            if (droppedCount != 0) {
                logger::warn(
                    "[DualPad][NativeInjector] Head-prepend dropped {} staged button events because the input queue singleton cache is full",
                    droppedCount);
            }
            return 0;
        }

        stagedTail->next = head;
        head = stagedHead;

        if (logDebug) {
            logger::info(
                "[DualPad][NativeInjector] HeadPrepend linked submitted={} newHead=0x{:X} newTail=0x{:X} tailNext=0x{:X}",
                submittedCount,
                reinterpret_cast<std::uintptr_t>(head),
                reinterpret_cast<std::uintptr_t>(stagedTail),
                reinterpret_cast<std::uintptr_t>(stagedTail ? stagedTail->next : nullptr));
        }

        if (droppedCount != 0) {
            logger::warn(
                "[DualPad][NativeInjector] Head-prepend dropped {} staged button events because the input queue singleton cache is full",
                droppedCount);
        }

        if (IsDebugLoggingEnabled()) {
            logger::info(
                "[DualPad][NativeInjector] Prepended {} staged button events onto the current ControlMap head using queue cache",
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
