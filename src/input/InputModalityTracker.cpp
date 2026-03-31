#include "pch.h"
#include "input/InputModalityTracker.h"

#include "input/Action.h"
#include "input/backend/NativeButtonCommitBackend.h"
#include "input/RuntimeConfig.h"
#include "input/injection/GameplayOwnershipCoordinator.h"

#include <algorithm>
#include <cmath>

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        constexpr REL::ID kInputManagerProcessEventId{ 67242 };
        constexpr std::ptrdiff_t kInputManagerProcessEventIgnoreKbMouseOffset = 0xBE;
        constexpr REL::ID kInputManagerInitializeId{ 67313 };
        constexpr std::ptrdiff_t kInputManagerInitializeIgnoreKbMouseOffset = 0xF6;
        constexpr REL::ID kIsUsingGamepadId{ 67320 };
        constexpr std::ptrdiff_t kIsUsingGamepadCallOffset = 0xD;
        constexpr REL::ID kGamepadControlsCursorId{ 67321 };
        constexpr std::ptrdiff_t kGamepadControlsCursorCallOffset = 0xD;
        constexpr REL::ID kGamepadHandlerVtblId{ 560029 };
        constexpr std::size_t kGamepadIsEnabledVfuncIndex = 0x8;

        constexpr std::uint8_t kNoOp5[] = {
            0x0F, 0x1F, 0x44, 0x00, 0x00
        };
        constexpr std::uint8_t kNoOp6[] = {
            0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00
        };
        constexpr std::ptrdiff_t kGamepadDelegateOffset = 0x08;
        constexpr std::ptrdiff_t kMenuControlsRemapModeOffset = 0x82;
        constexpr float kThumbstickPromotionThreshold = 0.25f;
        constexpr std::uint64_t kMouseMoveAccumulatorResetMs = 250;
        constexpr std::uint64_t kGameplayMouseLookActiveMs = 200;

        enum GameplayMoveSemanticBit : std::uint32_t
        {
            kMoveForwardBit = 1u << 0,
            kMoveBackBit = 1u << 1,
            kMoveStrafeLeftBit = 1u << 2,
            kMoveStrafeRightBit = 1u << 3
        };

        enum GameplayCombatSemanticBit : std::uint32_t
        {
            kCombatLeftBit = 1u << 0,
            kCombatRightBit = 1u << 1
        };

        enum GameplayDigitalSemanticBit : std::uint32_t
        {
            kDigitalJumpBit = 1u << 0,
            kDigitalActivateBit = 1u << 1,
            kDigitalSprintBit = 1u << 2
        };

        constexpr std::uint32_t kDigitalOwnerRelevantMask =
            kDigitalJumpBit |
            kDigitalActivateBit;

        std::uint64_t GetMonotonicMs()
        {
            return ::GetTickCount64();
        }

        bool IsMenuOwnedContext(InputContext context)
        {
            const auto value = static_cast<std::uint16_t>(context);
            return (value >= 100 && value < 2000) || context == InputContext::Console;
        }

        bool IsGameplayDomainContext(InputContext context)
        {
            return !IsMenuOwnedContext(context);
        }

        void NotifyMenuPresentationChanged(RE::IMenu& menu)
        {
            if (!menu.uiMovie) {
                return;
            }

            RE::GFxValue callback;
            if (!menu.uiMovie->GetVariable(&callback, "_root.DualPad_OnPresentationChanged") ||
                callback.IsUndefined()) {
                return;
            }

            menu.uiMovie->InvokeNoReturn("_root.DualPad_OnPresentationChanged", nullptr, 0);
        }

        bool IsMeaningfulGamepadEvent(const RE::InputEvent& event)
        {
            switch (event.GetEventType()) {
            case RE::INPUT_EVENT_TYPE::kButton:
                return event.AsButtonEvent() != nullptr;
            case RE::INPUT_EVENT_TYPE::kThumbstick:
                if (const auto* thumbstickEvent = event.AsThumbstickEvent()) {
                    return std::fabs(thumbstickEvent->xValue) >= kThumbstickPromotionThreshold ||
                        std::fabs(thumbstickEvent->yValue) >= kThumbstickPromotionThreshold;
                }
                return false;
            default:
                return true;
            }
        }

        bool ShouldPromoteGameplayOwnerFromGamepadEvent(const RE::InputEvent& event)
        {
            switch (event.GetEventType()) {
            case RE::INPUT_EVENT_TYPE::kButton:
                return event.AsButtonEvent() != nullptr;

            case RE::INPUT_EVENT_TYPE::kThumbstick:
                if (const auto* thumbstickEvent = event.AsThumbstickEvent()) {
                    const bool meaningful =
                        std::fabs(thumbstickEvent->xValue) >= kThumbstickPromotionThreshold ||
                        std::fabs(thumbstickEvent->yValue) >= kThumbstickPromotionThreshold;
                    if (!meaningful) {
                        return false;
                    }

                    const auto thumbstickId = static_cast<RE::ThumbstickEvent::InputType>(thumbstickEvent->idCode);
                    if (thumbstickId == RE::ThumbstickEvent::InputType::kRightThumbstick) {
                        return true;
                    }

                    if (thumbstickId == RE::ThumbstickEvent::InputType::kLeftThumbstick) {
                        // Left stick belongs to MoveOwner, not gameplay presentation.
                        // Letting it promote the coarse gameplay owner reintroduces
                        // presentation/mouselook coupling and causes camera glitches.
                        return false;
                    }
                }
                return false;

            default:
                return true;
            }
        }

        std::uint32_t GetGameplayMappedKey(std::string_view eventId, RE::INPUT_DEVICE device)
        {
            const auto* controlMap = RE::ControlMap::GetSingleton();
            if (!controlMap) {
                return RE::ControlMap::kInvalid;
            }

            return controlMap->GetMappedKey(
                eventId,
                device,
                RE::ControlMap::InputContextID::kGameplay);
        }

        bool IsSyntheticGamepadSprintActive()
        {
            auto& backend = backend::NativeButtonCommitBackend::GetSingleton();
            return backend.IsActionDown(actions::Sprint) &&
                backend.HasHeldContributor(actions::Sprint, backend::HeldContributor::Gamepad);
        }

    }

    InputModalityTracker& InputModalityTracker::GetSingleton()
    {
        static InputModalityTracker instance;
        return instance;
    }

    InputModalityTracker::InputModalityTracker() = default;

    void InputModalityTracker::Install()
    {
        if (_installed) {
            return;
        }

        InstallDeviceConnectHook();
        InstallInputManagerHook();
        InstallUsingGamepadHook();
        InstallGamepadCursorHook();
        InstallGamepadDeviceEnabledHook();

        if (auto* inputManager = RE::BSInputDeviceManager::GetSingleton(); inputManager) {
            const auto* gamepadHandler = inputManager->GetGamepadHandler();
            const auto initialPresentationOwner =
                gamepadHandler && gamepadHandler->IsEnabled() ?
                PresentationOwner::Gamepad :
                PresentationOwner::KeyboardMouse;
            _presentationOwner.store(initialPresentationOwner, std::memory_order_relaxed);
            _cursorOwner.store(
                initialPresentationOwner == PresentationOwner::Gamepad ?
                CursorOwner::Gamepad :
                CursorOwner::KeyboardMouse,
                std::memory_order_relaxed);
            _gameplayOwner.store(
                initialPresentationOwner == PresentationOwner::Gamepad ?
                GameplayOwner::Gamepad :
                GameplayOwner::KeyboardMouse,
                std::memory_order_relaxed);

            ReconcileContextState();
            Register();
        }

        _installed = true;
        logger::info("[DualPad][InputOwner] Installed UI input ownership arbiter");
    }

    void InputModalityTracker::Register()
    {
        if (_registered) {
            return;
        }

        auto* inputManager = RE::BSInputDeviceManager::GetSingleton();
        if (!inputManager) {
            return;
        }

        if (const auto* gamepadHandler = inputManager->GetGamepadHandler(); gamepadHandler) {
            const auto initialPresentationOwner =
                gamepadHandler->IsEnabled() ?
                PresentationOwner::Gamepad :
                PresentationOwner::KeyboardMouse;
            _presentationOwner.store(initialPresentationOwner, std::memory_order_relaxed);
            _cursorOwner.store(
                initialPresentationOwner == PresentationOwner::Gamepad ?
                CursorOwner::Gamepad :
                CursorOwner::KeyboardMouse,
                std::memory_order_relaxed);
            _gameplayOwner.store(
                initialPresentationOwner == PresentationOwner::Gamepad ?
                GameplayOwner::Gamepad :
                GameplayOwner::KeyboardMouse,
                std::memory_order_relaxed);
        }

        ReconcileContextState();
        inputManager->PrependEventSink(this);
        _registered = true;
        logger::info("[DualPad][InputOwner] Registered input event sink");
    }

    bool InputModalityTracker::IsInstalled() const
    {
        return _installed;
    }

    bool InputModalityTracker::IsUsingGamepad() const
    {
        const auto context = _observedContext.load(std::memory_order_relaxed);
        if (IsGameplayDomainContext(context)) {
            if (!RuntimeConfig::GetSingleton().EnableGameplayOwnership()) {
                if (IsSyntheticGamepadSprintActive()) {
                    return true;
                }

                return _gameplayOwner.load(std::memory_order_relaxed) == GameplayOwner::Gamepad;
            }

            const auto publishedLookOwner =
                GameplayOwnershipCoordinator::GetSingleton().GetPublishedLookOwner();
            if (IsGameplayMouseLookActive() &&
                publishedLookOwner == GameplayOwnershipCoordinator::ChannelOwner::KeyboardMouse) {
                return false;
            }

            if (IsSyntheticGamepadSprintActive()) {
                return true;
            }

            return _gameplayOwner.load(std::memory_order_relaxed) == GameplayOwner::Gamepad;
        }

        return GetEffectivePresentationOwner(context) == PresentationOwner::Gamepad;
    }

    bool InputModalityTracker::IsGameplayUsingGamepad() const
    {
        return _gameplayOwner.load(std::memory_order_relaxed) == GameplayOwner::Gamepad;
    }

    bool InputModalityTracker::IsGameplayMouseLookActive() const
    {
        const auto lastAtMs = _lastGameplayMouseLookAtMs.load(std::memory_order_relaxed);
        if (lastAtMs == 0) {
            return false;
        }

        const auto nowMs = GetMonotonicMs();
        return nowMs >= lastAtMs && (nowMs - lastAtMs) <= kGameplayMouseLookActiveMs;
    }

    bool InputModalityTracker::IsGameplayKeyboardMoveActive() const
    {
        return _gameplayKeyboardMoveDownMask.load(std::memory_order_relaxed) != 0;
    }

    bool InputModalityTracker::IsGameplayKeyboardMouseCombatActive() const
    {
        return _gameplayKeyboardCombatDownMask.load(std::memory_order_relaxed) != 0 ||
            _gameplayMouseCombatDownMask.load(std::memory_order_relaxed) != 0;
    }

    bool InputModalityTracker::IsGameplayKeyboardMouseDigitalActive() const
    {
        return (_gameplayKeyboardDigitalDownMask.load(std::memory_order_relaxed) & kDigitalOwnerRelevantMask) != 0 ||
            (_gameplayMouseDigitalDownMask.load(std::memory_order_relaxed) & kDigitalOwnerRelevantMask) != 0;
    }

    bool InputModalityTracker::IsGameplayKeyboardMouseSprintActive() const
    {
        return (_gameplayKeyboardDigitalDownMask.load(std::memory_order_relaxed) & kDigitalSprintBit) != 0 ||
            (_gameplayMouseDigitalDownMask.load(std::memory_order_relaxed) & kDigitalSprintBit) != 0;
    }

    void InputModalityTracker::MarkSyntheticKeyboardScancode(
        std::uint8_t scancode,
        std::uint8_t pendingEvents,
        std::uint64_t windowMs)
    {
        if (pendingEvents == 0) {
            return;
        }

        std::scoped_lock lock(_suppressionMutex);
        auto& state = _suppressedKeyboardScancodes[scancode];
        const auto expiresAtMs = GetMonotonicMs() + windowMs;
        const auto accumulated = static_cast<unsigned int>(state.pendingEvents) + pendingEvents;
        state.pendingEvents = static_cast<std::uint8_t>((std::min)(accumulated, 0xFFu));
        state.expiresAtMs = expiresAtMs;

        auto trackedExpiresAt = _syntheticKeyboardWindowExpiresAtMs.load(std::memory_order_relaxed);
        while (trackedExpiresAt < expiresAtMs &&
            !_syntheticKeyboardWindowExpiresAtMs.compare_exchange_weak(
                trackedExpiresAt,
                expiresAtMs,
                std::memory_order_relaxed)) {
        }
    }

    RE::BSEventNotifyControl InputModalityTracker::ProcessEvent(
        RE::InputEvent* const* event,
        RE::BSTEventSource<RE::InputEvent*>* source)
    {
        (void)source;

        ReconcileContextState();

        const auto context = _observedContext.load(std::memory_order_relaxed);
        const auto policy = ResolveOwnerPolicy(ResolveOwnerPolicyKind(context));
        const auto menuContextActive = IsMenuContextActive(context);
        auto* current = event ? *event : nullptr;

        while (current) {
            if (!menuContextActive) {
                HandleGameplayOnlyEvent(*current, context);

                current = current->next;
                continue;
            }

            switch (current->GetDevice()) {
            case RE::INPUT_DEVICE::kKeyboard:
                HandleKeyboardEvent(*current, context, policy);
                break;
            case RE::INPUT_DEVICE::kMouse:
                HandleMouseEvent(*current, context, policy);
                break;
            case RE::INPUT_DEVICE::kGamepad:
                HandleGamepadEvent(*current, context, policy);
                break;
            default:
                break;
            }

            current = current->next;
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    void InputModalityTracker::InstallDeviceConnectHook()
    {
        REL::Relocation<std::uintptr_t> hook{ kInputManagerProcessEventId, kInputManagerProcessEventIgnoreKbMouseOffset };
        REL::safe_write(hook.address(), kNoOp6);
    }

    void InputModalityTracker::InstallInputManagerHook()
    {
        REL::Relocation<std::uintptr_t> hook{ kInputManagerInitializeId, kInputManagerInitializeIgnoreKbMouseOffset };
        REL::safe_write(hook.address(), kNoOp5);
    }

    void InputModalityTracker::InstallUsingGamepadHook()
    {
        REL::Relocation<std::uintptr_t> hook{ kIsUsingGamepadId, kIsUsingGamepadCallOffset };
        SKSE::GetTrampoline().write_call<6>(hook.address(), IsUsingGamepadHook);
    }

    void InputModalityTracker::InstallGamepadCursorHook()
    {
        REL::Relocation<std::uintptr_t> hook{ kGamepadControlsCursorId, kGamepadControlsCursorCallOffset };
        SKSE::GetTrampoline().write_call<6>(hook.address(), IsGamepadCursorHook);
    }

    void InputModalityTracker::InstallGamepadDeviceEnabledHook()
    {
        REL::Relocation<std::uintptr_t> gamepadHandlerVtbl{ kGamepadHandlerVtblId };
        gamepadHandlerVtbl.write_vfunc(kGamepadIsEnabledVfuncIndex, IsGamepadDeviceEnabledHook);
    }

    void InputModalityTracker::ReconcileContextState()
    {
        auto& contextManager = ContextManager::GetSingleton();
        const auto context = contextManager.GetCurrentContext();
        const auto epoch = contextManager.GetCurrentEpoch();
        const auto previousEpoch = _observedContextEpoch.load(std::memory_order_relaxed);
        const auto previousContext = _observedContext.load(std::memory_order_relaxed);

        if (previousEpoch == epoch && previousContext == context) {
            return;
        }

        _observedContext.store(context, std::memory_order_relaxed);
        _observedContextEpoch.store(epoch, std::memory_order_relaxed);
        ResetMouseMoveAccumulator();
        SetPointerIntent(PointerIntent::None);

        const auto currentPresentationOwner = _presentationOwner.load(std::memory_order_relaxed);
        const auto previousWasGameplay = IsGameplayDomainContext(previousContext);
        const auto currentIsGameplay = IsGameplayDomainContext(context);

        if (!previousWasGameplay && currentIsGameplay) {
            SetGameplayOwner(
                currentPresentationOwner == PresentationOwner::Gamepad ?
                GameplayOwner::Gamepad :
                GameplayOwner::KeyboardMouse,
                context,
                "exit-menu");
            ResetGameplayChannelFacts();
        }

        if (previousWasGameplay && !currentIsGameplay) {
            const auto inheritedGameplayOwner = _gameplayOwner.load(std::memory_order_relaxed);
            const auto inheritedPresentationOwner =
                inheritedGameplayOwner == GameplayOwner::Gamepad ?
                PresentationOwner::Gamepad :
                PresentationOwner::KeyboardMouse;
            SetPresentationOwner(inheritedPresentationOwner, context, "enter-menu");
            SetNavigationOwner(
                inheritedGameplayOwner == GameplayOwner::Gamepad ?
                NavigationOwner::Gamepad :
                NavigationOwner::KeyboardMouse);
            SetCursorOwner(
                inheritedGameplayOwner == GameplayOwner::Gamepad ?
                CursorOwner::Gamepad :
                CursorOwner::KeyboardMouse,
                context,
                "enter-menu");
            ResetGameplayChannelFacts();
        }

        if (IsMenuContextActive(context)) {
            SetNavigationOwner(
                GetEffectivePresentationOwner(context) == PresentationOwner::Gamepad ?
                NavigationOwner::Gamepad :
                NavigationOwner::KeyboardMouse);
        }
        else {
            SetNavigationOwner(NavigationOwner::None);
        }

        SetCursorOwner(
            GetEffectiveCursorOwner(context) == CursorOwner::Gamepad ?
            CursorOwner::Gamepad :
            CursorOwner::KeyboardMouse,
            context,
            "context-reset");

        const auto policyKind = ResolveOwnerPolicyKind(context);

        logger::info(
            "[DualPad][InputOwner] Context {} -> {} (presentation={}, navigation={}, cursor={}, gameplay={}, pointer={}, policy={})",
            dualpad::input::ToString(previousContext),
            dualpad::input::ToString(context),
            ToString(_presentationOwner.load(std::memory_order_relaxed)),
            ToString(_navigationOwner.load(std::memory_order_relaxed)),
            ToString(_cursorOwner.load(std::memory_order_relaxed)),
            ToString(_gameplayOwner.load(std::memory_order_relaxed)),
            ToString(_pointerIntent.load(std::memory_order_relaxed)),
            ToString(policyKind));
    }

    void InputModalityTracker::HandleKeyboardEvent(const RE::InputEvent& event, InputContext context, const OwnerPolicy&)
    {
        if (event.GetEventType() == RE::INPUT_EVENT_TYPE::kButton) {
            const auto* buttonEvent = event.AsButtonEvent();
            if (buttonEvent && ConsumeSyntheticKeyboardEvent(buttonEvent->GetIDCode())) {
                return;
            }
        }

        if (IsSyntheticKeyboardWindowActive()) {
            return;
        }

        PromoteToKeyboardMouse(context, event.GetEventType() == RE::INPUT_EVENT_TYPE::kChar ? "keyboard-char" : "keyboard", PointerIntent::None);
    }

    void InputModalityTracker::HandleMouseEvent(const RE::InputEvent& event, InputContext context, const OwnerPolicy& policy)
    {
        if (event.GetEventType() == RE::INPUT_EVENT_TYPE::kMouseMove) {
            if (const auto* mouseMoveEvent = event.AsMouseMoveEvent()) {
                HandleMouseMoveEvent(*mouseMoveEvent, context, policy);
            }
            return;
        }

        PromoteToKeyboardMouse(context, event.GetEventType() == RE::INPUT_EVENT_TYPE::kButton ? "mouse-button" : "mouse-wheel", PointerIntent::PointerActive);
    }

    void InputModalityTracker::HandleMouseMoveEvent(const RE::MouseMoveEvent& event, InputContext context, const OwnerPolicy& policy)
    {
        if (event.mouseInputX == 0 && event.mouseInputY == 0) {
            return;
        }

        const auto nowMs = GetMonotonicMs();
        AccumulateMouseMove(event, nowMs);
        SetPointerIntent(PointerIntent::HoverOnly);

        if (!policy.mouseMoveCanPromote) {
            return;
        }

        if (_presentationOwner.load(std::memory_order_relaxed) == PresentationOwner::Gamepad &&
            IsGamepadLeaseActive()) {
            return;
        }

        if (ShouldPromoteMouseMoveToKeyboardMouse(policy, nowMs)) {
            SetPointerIntent(PointerIntent::PointerActive);
            if (policy.mouseMovePromotionTarget == MouseMovePromotionTarget::PresentationAndCursor) {
                logger::info(
                    "[DualPad][InputOwner] mouse move promoted presentation+cursor (ctx={}, thresholdPx={}, delayMs={})",
                    dualpad::input::ToString(context),
                    policy.mouseMoveThresholdPx,
                    policy.mouseMovePromoteDelayMs);
                PromoteToKeyboardMouse(context, "mouse-move-threshold", PointerIntent::PointerActive);
            }
            else if (policy.mouseMovePromotionTarget == MouseMovePromotionTarget::CursorOnly) {
                ResetMouseMoveAccumulator();
                logger::info(
                    "[DualPad][InputOwner] mouse move promoted cursor only (ctx={}, thresholdPx={}, delayMs={})",
                    dualpad::input::ToString(context),
                    policy.mouseMoveThresholdPx,
                    policy.mouseMovePromoteDelayMs);
                SetCursorOwner(CursorOwner::KeyboardMouse, context, "mouse-move-threshold");
            }
        }
    }

    void InputModalityTracker::HandleGamepadEvent(const RE::InputEvent& event, InputContext context, const OwnerPolicy& policy)
    {
        if (!IsMeaningfulGamepadEvent(event)) {
            return;
        }

        PromoteToGamepad(
            context,
            policy,
            event.GetEventType() == RE::INPUT_EVENT_TYPE::kThumbstick ?
            "gamepad-thumbstick" :
            "gamepad");
    }

    void InputModalityTracker::HandleGameplayOnlyEvent(const RE::InputEvent& event, InputContext context)
    {
        switch (event.GetDevice()) {
        case RE::INPUT_DEVICE::kKeyboard:
            if (event.GetEventType() == RE::INPUT_EVENT_TYPE::kButton) {
                const auto* buttonEvent = event.AsButtonEvent();
                if (buttonEvent && ConsumeSyntheticKeyboardEvent(buttonEvent->GetIDCode())) {
                    return;
                }
                if (buttonEvent) {
                    UpdateGameplayChannelFacts(*buttonEvent);
                    if (IsMappedGameplayKeyboardSprintKey(buttonEvent->GetIDCode())) {
                        // Sprint is a sustained digital action with its own
                        // contributor aggregation. Let it update held facts,
                        // but do not let it churn the coarse gameplay owner.
                        return;
                    }
                }
            }

            if (IsSyntheticKeyboardWindowActive()) {
                return;
            }

            SetGameplayOwner(
                GameplayOwner::KeyboardMouse,
                context,
                event.GetEventType() == RE::INPUT_EVENT_TYPE::kChar ? "keyboard-char" : "keyboard");
            return;

        case RE::INPUT_DEVICE::kMouse:
            if (event.GetEventType() == RE::INPUT_EVENT_TYPE::kMouseMove) {
                const auto* mouseMoveEvent = event.AsMouseMoveEvent();
                if (!mouseMoveEvent || (mouseMoveEvent->mouseInputX == 0 && mouseMoveEvent->mouseInputY == 0)) {
                    return;
                }
                _lastGameplayMouseLookAtMs.store(GetMonotonicMs(), std::memory_order_relaxed);
                SetGameplayOwner(GameplayOwner::KeyboardMouse, context, "mouse-move");
                return;
            }

            if (event.GetEventType() == RE::INPUT_EVENT_TYPE::kButton) {
                if (const auto* buttonEvent = event.AsButtonEvent()) {
                    UpdateGameplayChannelFacts(*buttonEvent);
                    if (IsMappedGameplayMouseSprintButton(buttonEvent->GetIDCode())) {
                        // Same as keyboard sprint: keep sustained held facts,
                        // but do not use Sprint to flip coarse gameplay owner.
                        return;
                    }
                }
            }

            SetGameplayOwner(
                GameplayOwner::KeyboardMouse,
                context,
                event.GetEventType() == RE::INPUT_EVENT_TYPE::kButton ? "mouse-button" : "mouse-wheel");
            return;

        case RE::INPUT_DEVICE::kGamepad:
            if (!ShouldPromoteGameplayOwnerFromGamepadEvent(event)) {
                return;
            }

            SetGameplayOwner(
                GameplayOwner::Gamepad,
                context,
                event.GetEventType() == RE::INPUT_EVENT_TYPE::kThumbstick ? "gamepad-thumbstick-owner" : "gamepad");
            return;

        default:
            return;
        }
    }

    bool InputModalityTracker::ConsumeSyntheticKeyboardEvent(std::uint32_t scancode)
    {
        const auto index = static_cast<std::uint8_t>(scancode & 0xFF);
        std::scoped_lock lock(_suppressionMutex);
        auto& state = _suppressedKeyboardScancodes[index];
        if (state.pendingEvents == 0) {
            return false;
        }

        const auto now = GetMonotonicMs();
        if (state.expiresAtMs != 0 && now > state.expiresAtMs) {
            state = {};
            return false;
        }

        --state.pendingEvents;
        if (state.pendingEvents == 0) {
            state.expiresAtMs = 0;
        }

        return true;
    }

    bool InputModalityTracker::IsSyntheticKeyboardWindowActive() const
    {
        const auto expiresAtMs = _syntheticKeyboardWindowExpiresAtMs.load(std::memory_order_relaxed);
        if (expiresAtMs == 0) {
            return false;
        }

        const auto now = GetMonotonicMs();
        if (now > expiresAtMs) {
            _syntheticKeyboardWindowExpiresAtMs.store(0, std::memory_order_relaxed);
            return false;
        }

        return true;
    }

    bool InputModalityTracker::IsGamepadLeaseActive() const
    {
        const auto expiresAtMs = _gamepadLeaseExpiresAtMs.load(std::memory_order_relaxed);
        if (expiresAtMs == 0) {
            return false;
        }

        const auto now = GetMonotonicMs();
        if (now > expiresAtMs) {
            _gamepadLeaseExpiresAtMs.store(0, std::memory_order_relaxed);
            return false;
        }

        return true;
    }

    bool InputModalityTracker::IsMenuContextActive() const
    {
        return IsMenuContextActive(_observedContext.load(std::memory_order_relaxed));
    }

    bool InputModalityTracker::IsMenuContextActive(InputContext context) const
    {
        return IsMenuOwnedContext(context);
    }

    void InputModalityTracker::ResetGameplayChannelFacts()
    {
        _gameplayKeyboardMoveDownMask.store(0, std::memory_order_relaxed);
        _gameplayKeyboardCombatDownMask.store(0, std::memory_order_relaxed);
        _gameplayMouseCombatDownMask.store(0, std::memory_order_relaxed);
        _gameplayKeyboardDigitalDownMask.store(0, std::memory_order_relaxed);
        _gameplayMouseDigitalDownMask.store(0, std::memory_order_relaxed);
        _lastGameplayMouseLookAtMs.store(0, std::memory_order_relaxed);
    }

    void InputModalityTracker::UpdateGameplayChannelFacts(const RE::ButtonEvent& event)
    {
        const auto idCode = event.GetIDCode();
        const bool down = event.IsPressed();

        if (event.GetDevice() == RE::INPUT_DEVICE::kKeyboard) {
            auto moveMask = _gameplayKeyboardMoveDownMask.load(std::memory_order_relaxed);
            if (IsMappedGameplayKeyboardMoveKey(idCode)) {
                const auto* userEvents = RE::UserEvents::GetSingleton();
                if (userEvents) {
                    if (idCode == GetGameplayMappedKey(userEvents->forward, RE::INPUT_DEVICE::kKeyboard)) {
                        moveMask = down ? (moveMask | kMoveForwardBit) : (moveMask & ~kMoveForwardBit);
                    }
                    if (idCode == GetGameplayMappedKey(userEvents->back, RE::INPUT_DEVICE::kKeyboard)) {
                        moveMask = down ? (moveMask | kMoveBackBit) : (moveMask & ~kMoveBackBit);
                    }
                    if (idCode == GetGameplayMappedKey(userEvents->strafeLeft, RE::INPUT_DEVICE::kKeyboard)) {
                        moveMask = down ? (moveMask | kMoveStrafeLeftBit) : (moveMask & ~kMoveStrafeLeftBit);
                    }
                    if (idCode == GetGameplayMappedKey(userEvents->strafeRight, RE::INPUT_DEVICE::kKeyboard)) {
                        moveMask = down ? (moveMask | kMoveStrafeRightBit) : (moveMask & ~kMoveStrafeRightBit);
                    }
                    _gameplayKeyboardMoveDownMask.store(moveMask, std::memory_order_relaxed);
                }
            }

            auto combatMask = _gameplayKeyboardCombatDownMask.load(std::memory_order_relaxed);
            if (IsMappedGameplayKeyboardCombatKey(idCode)) {
                const auto* userEvents = RE::UserEvents::GetSingleton();
                if (userEvents) {
                    if (idCode == GetGameplayMappedKey(userEvents->leftAttack, RE::INPUT_DEVICE::kKeyboard)) {
                        combatMask = down ? (combatMask | kCombatLeftBit) : (combatMask & ~kCombatLeftBit);
                    }
                    if (idCode == GetGameplayMappedKey(userEvents->rightAttack, RE::INPUT_DEVICE::kKeyboard)) {
                        combatMask = down ? (combatMask | kCombatRightBit) : (combatMask & ~kCombatRightBit);
                    }
                    _gameplayKeyboardCombatDownMask.store(combatMask, std::memory_order_relaxed);
                }
            }

            auto digitalMask = _gameplayKeyboardDigitalDownMask.load(std::memory_order_relaxed);
            if (IsMappedGameplayKeyboardDigitalKey(idCode)) {
                const auto* userEvents = RE::UserEvents::GetSingleton();
                if (userEvents) {
                    if (idCode == GetGameplayMappedKey(userEvents->jump, RE::INPUT_DEVICE::kKeyboard)) {
                        digitalMask = down ? (digitalMask | kDigitalJumpBit) : (digitalMask & ~kDigitalJumpBit);
                    }
                    if (idCode == GetGameplayMappedKey(userEvents->activate, RE::INPUT_DEVICE::kKeyboard)) {
                        digitalMask = down ? (digitalMask | kDigitalActivateBit) : (digitalMask & ~kDigitalActivateBit);
                    }
                    if (idCode == GetGameplayMappedKey(userEvents->sprint, RE::INPUT_DEVICE::kKeyboard)) {
                        const auto previousDigitalMask = digitalMask;
                        digitalMask = down ? (digitalMask | kDigitalSprintBit) : (digitalMask & ~kDigitalSprintBit);
                        if ((previousDigitalMask ^ digitalMask) & kDigitalSprintBit) {
                            logger::info(
                                "[DualPad][SprintProbe] KB sprint fact -> {} (keyboard)",
                                down);
                        }
                    }
                    _gameplayKeyboardDigitalDownMask.store(digitalMask, std::memory_order_relaxed);
                }
            }
            return;
        }

        if (event.GetDevice() == RE::INPUT_DEVICE::kMouse) {
            const auto* userEvents = RE::UserEvents::GetSingleton();
            if (!userEvents) {
                return;
            }

            if (IsMappedGameplayMouseCombatButton(idCode)) {
                auto combatMask = _gameplayMouseCombatDownMask.load(std::memory_order_relaxed);
                if (idCode == GetGameplayMappedKey(userEvents->leftAttack, RE::INPUT_DEVICE::kMouse)) {
                    combatMask = down ? (combatMask | kCombatLeftBit) : (combatMask & ~kCombatLeftBit);
                }
                if (idCode == GetGameplayMappedKey(userEvents->rightAttack, RE::INPUT_DEVICE::kMouse)) {
                    combatMask = down ? (combatMask | kCombatRightBit) : (combatMask & ~kCombatRightBit);
                }
                _gameplayMouseCombatDownMask.store(combatMask, std::memory_order_relaxed);
            }

            if (IsMappedGameplayMouseDigitalButton(idCode)) {
                auto digitalMask = _gameplayMouseDigitalDownMask.load(std::memory_order_relaxed);
                if (idCode == GetGameplayMappedKey(userEvents->jump, RE::INPUT_DEVICE::kMouse)) {
                    digitalMask = down ? (digitalMask | kDigitalJumpBit) : (digitalMask & ~kDigitalJumpBit);
                }
                if (idCode == GetGameplayMappedKey(userEvents->activate, RE::INPUT_DEVICE::kMouse)) {
                    digitalMask = down ? (digitalMask | kDigitalActivateBit) : (digitalMask & ~kDigitalActivateBit);
                }
                if (idCode == GetGameplayMappedKey(userEvents->sprint, RE::INPUT_DEVICE::kMouse)) {
                    const auto previousDigitalMask = digitalMask;
                    digitalMask = down ? (digitalMask | kDigitalSprintBit) : (digitalMask & ~kDigitalSprintBit);
                    if ((previousDigitalMask ^ digitalMask) & kDigitalSprintBit) {
                        logger::info(
                            "[DualPad][SprintProbe] KB sprint fact -> {} (mouse)",
                            down);
                    }
                }
                _gameplayMouseDigitalDownMask.store(digitalMask, std::memory_order_relaxed);
            }
        }
    }

    bool InputModalityTracker::IsMappedGameplayKeyboardMoveKey(std::uint32_t idCode) const
    {
        const auto* userEvents = RE::UserEvents::GetSingleton();
        if (!userEvents) {
            return false;
        }

        return idCode == GetGameplayMappedKey(userEvents->forward, RE::INPUT_DEVICE::kKeyboard) ||
            idCode == GetGameplayMappedKey(userEvents->back, RE::INPUT_DEVICE::kKeyboard) ||
            idCode == GetGameplayMappedKey(userEvents->strafeLeft, RE::INPUT_DEVICE::kKeyboard) ||
            idCode == GetGameplayMappedKey(userEvents->strafeRight, RE::INPUT_DEVICE::kKeyboard);
    }

    bool InputModalityTracker::IsMappedGameplayKeyboardCombatKey(std::uint32_t idCode) const
    {
        const auto* userEvents = RE::UserEvents::GetSingleton();
        if (!userEvents) {
            return false;
        }

        return idCode == GetGameplayMappedKey(userEvents->leftAttack, RE::INPUT_DEVICE::kKeyboard) ||
            idCode == GetGameplayMappedKey(userEvents->rightAttack, RE::INPUT_DEVICE::kKeyboard);
    }

    bool InputModalityTracker::IsMappedGameplayMouseCombatButton(std::uint32_t idCode) const
    {
        const auto* userEvents = RE::UserEvents::GetSingleton();
        if (!userEvents) {
            return false;
        }

        return idCode == GetGameplayMappedKey(userEvents->leftAttack, RE::INPUT_DEVICE::kMouse) ||
            idCode == GetGameplayMappedKey(userEvents->rightAttack, RE::INPUT_DEVICE::kMouse);
    }

    bool InputModalityTracker::IsMappedGameplayKeyboardDigitalKey(std::uint32_t idCode) const
    {
        const auto* userEvents = RE::UserEvents::GetSingleton();
        if (!userEvents) {
            return false;
        }

        return idCode == GetGameplayMappedKey(userEvents->jump, RE::INPUT_DEVICE::kKeyboard) ||
            idCode == GetGameplayMappedKey(userEvents->activate, RE::INPUT_DEVICE::kKeyboard) ||
            idCode == GetGameplayMappedKey(userEvents->sprint, RE::INPUT_DEVICE::kKeyboard);
    }

    bool InputModalityTracker::IsMappedGameplayMouseDigitalButton(std::uint32_t idCode) const
    {
        const auto* userEvents = RE::UserEvents::GetSingleton();
        if (!userEvents) {
            return false;
        }

        return idCode == GetGameplayMappedKey(userEvents->jump, RE::INPUT_DEVICE::kMouse) ||
            idCode == GetGameplayMappedKey(userEvents->activate, RE::INPUT_DEVICE::kMouse) ||
            idCode == GetGameplayMappedKey(userEvents->sprint, RE::INPUT_DEVICE::kMouse);
    }

    bool InputModalityTracker::IsMappedGameplayKeyboardSprintKey(std::uint32_t idCode) const
    {
        const auto* userEvents = RE::UserEvents::GetSingleton();
        if (!userEvents) {
            return false;
        }

        return idCode == GetGameplayMappedKey(userEvents->sprint, RE::INPUT_DEVICE::kKeyboard);
    }

    bool InputModalityTracker::IsMappedGameplayMouseSprintButton(std::uint32_t idCode) const
    {
        const auto* userEvents = RE::UserEvents::GetSingleton();
        if (!userEvents) {
            return false;
        }

        return idCode == GetGameplayMappedKey(userEvents->sprint, RE::INPUT_DEVICE::kMouse);
    }

    void InputModalityTracker::RefreshGamepadLease(std::uint64_t windowMs)
    {
        const auto expiresAtMs = GetMonotonicMs() + windowMs;
        auto trackedExpiresAt = _gamepadLeaseExpiresAtMs.load(std::memory_order_relaxed);
        while (trackedExpiresAt < expiresAtMs &&
            !_gamepadLeaseExpiresAtMs.compare_exchange_weak(
                trackedExpiresAt,
                expiresAtMs,
                std::memory_order_relaxed)) {
        }
    }

    void InputModalityTracker::PromoteToGamepad(InputContext context, const OwnerPolicy& policy, std::string_view reason)
    {
        RefreshGamepadLease(policy.gamepadStickyMs);
        ResetMouseMoveAccumulator();
        SetPointerIntent(PointerIntent::None);
        SetNavigationOwner(IsMenuContextActive(context) ? NavigationOwner::Gamepad : NavigationOwner::None);
        SetCursorOwner(CursorOwner::Gamepad, context, reason);
        SetPresentationOwner(PresentationOwner::Gamepad, context, reason);
    }

    void InputModalityTracker::PromoteToKeyboardMouse(InputContext context, std::string_view reason, PointerIntent pointerIntent)
    {
        _gamepadLeaseExpiresAtMs.store(0, std::memory_order_relaxed);
        ResetMouseMoveAccumulator();
        SetPointerIntent(pointerIntent);
        SetNavigationOwner(IsMenuContextActive(context) ? NavigationOwner::KeyboardMouse : NavigationOwner::None);
        SetCursorOwner(CursorOwner::KeyboardMouse, context, reason);
        SetPresentationOwner(PresentationOwner::KeyboardMouse, context, reason);
    }

    InputModalityTracker::PresentationOwner InputModalityTracker::GetEffectivePresentationOwner(InputContext context) const
    {
        (void)context;
        return _presentationOwner.load(std::memory_order_relaxed);
    }

    InputModalityTracker::CursorOwner InputModalityTracker::GetEffectiveCursorOwner(InputContext context) const
    {
        (void)context;
        return _cursorOwner.load(std::memory_order_relaxed);
    }

    void InputModalityTracker::SetPresentationOwner(PresentationOwner owner, InputContext context, std::string_view reason)
    {
        const auto previous = _presentationOwner.exchange(owner, std::memory_order_relaxed);
        if (previous == owner) {
            return;
        }

        logger::info(
            "[DualPad][InputOwner] Presentation {} -> {} via {} (ctx={}, navigation={}, cursor={}, pointer={})",
            ToString(previous),
            ToString(owner),
            reason,
            dualpad::input::ToString(context),
            ToString(_navigationOwner.load(std::memory_order_relaxed)),
            ToString(_cursorOwner.load(std::memory_order_relaxed)),
            ToString(_pointerIntent.load(std::memory_order_relaxed)));

        RefreshMenus();
    }

    void InputModalityTracker::SetNavigationOwner(NavigationOwner owner)
    {
        _navigationOwner.store(owner, std::memory_order_relaxed);
    }

    void InputModalityTracker::SetCursorOwner(CursorOwner owner, InputContext context, std::string_view reason)
    {
        const auto previous = _cursorOwner.exchange(owner, std::memory_order_relaxed);
        if (previous == owner) {
            return;
        }

        logger::info(
            "[DualPad][InputOwner] Cursor {} -> {} via {} (ctx={}, presentation={})",
            ToString(previous),
            ToString(owner),
            reason,
            dualpad::input::ToString(context),
            ToString(_presentationOwner.load(std::memory_order_relaxed)));
    }

    void InputModalityTracker::SetPointerIntent(PointerIntent intent)
    {
        _pointerIntent.store(intent, std::memory_order_relaxed);
    }

    void InputModalityTracker::SetGameplayOwner(GameplayOwner owner, InputContext context, std::string_view reason)
    {
        const auto previous = _gameplayOwner.exchange(owner, std::memory_order_relaxed);
        if (previous == owner) {
            return;
        }

        logger::info(
            "[DualPad][InputOwner] Gameplay {} -> {} via {} (ctx={})",
            ToString(previous),
            ToString(owner),
            reason,
            dualpad::input::ToString(context));
    }

    void InputModalityTracker::ResetMouseMoveAccumulator()
    {
        std::scoped_lock lock(_mouseMoveMutex);
        _mouseMoveAccumulator = {};
    }

    void InputModalityTracker::AccumulateMouseMove(const RE::MouseMoveEvent& event, std::uint64_t nowMs)
    {
        std::scoped_lock lock(_mouseMoveMutex);
        if (_mouseMoveAccumulator.windowStartMs == 0 ||
            (_mouseMoveAccumulator.lastMoveAtMs != 0 &&
                nowMs - _mouseMoveAccumulator.lastMoveAtMs > kMouseMoveAccumulatorResetMs)) {
            _mouseMoveAccumulator = {};
            _mouseMoveAccumulator.windowStartMs = nowMs;
        }

        _mouseMoveAccumulator.dx += std::abs(event.mouseInputX);
        _mouseMoveAccumulator.dy += std::abs(event.mouseInputY);
        _mouseMoveAccumulator.lastMoveAtMs = nowMs;
    }

    bool InputModalityTracker::ShouldPromoteMouseMoveToKeyboardMouse(const OwnerPolicy& policy, std::uint64_t nowMs) const
    {
        std::scoped_lock lock(_mouseMoveMutex);
        if (_mouseMoveAccumulator.windowStartMs == 0) {
            return false;
        }

        const auto elapsedMs = nowMs - _mouseMoveAccumulator.windowStartMs;
        if (elapsedMs < policy.mouseMovePromoteDelayMs) {
            return false;
        }

        return (_mouseMoveAccumulator.dx + _mouseMoveAccumulator.dy) >= policy.mouseMoveThresholdPx;
    }

    InputModalityTracker::OwnerPolicyKind InputModalityTracker::ResolveOwnerPolicyKind(InputContext context) const
    {
        switch (context) {
        case InputContext::Menu:
        case InputContext::DialogueMenu:
        case InputContext::MessageBoxMenu:
        case InputContext::QuantityMenu:
        case InputContext::GiftMenu:
        case InputContext::LevelUpMenu:
        case InputContext::RaceSexMenu:
        case InputContext::TrainingMenu:
        case InputContext::CreationsMenu:
        case InputContext::Console:
            return OwnerPolicyKind::StrictGamepadSticky;

        case InputContext::MapMenu:
        case InputContext::MapMenuContext:
        case InputContext::Cursor:
        case InputContext::DebugMapMenu:
            return OwnerPolicyKind::PointerFirst;

        case InputContext::InventoryMenu:
        case InputContext::MagicMenu:
        case InputContext::JournalMenu:
        case InputContext::FavoritesMenu:
        case InputContext::TweenMenu:
        case InputContext::ContainerMenu:
        case InputContext::BarterMenu:
        case InputContext::StatsMenu:
        case InputContext::SkillMenu:
        case InputContext::BookMenu:
        case InputContext::ItemMenu:
        case InputContext::Book:
        case InputContext::Lockpicking:
        case InputContext::Favor:
            return OwnerPolicyKind::Neutral;

        default:
            return OwnerPolicyKind::Neutral;
        }
    }

    InputModalityTracker::OwnerPolicy InputModalityTracker::ResolveOwnerPolicy(OwnerPolicyKind kind) const
    {
        switch (kind) {
        case OwnerPolicyKind::StrictGamepadSticky:
            return OwnerPolicy{
                .mouseMoveCanPromote = false,
                .mouseMovePromotionTarget = MouseMovePromotionTarget::None,
                .mouseMoveThresholdPx = 12,
                .mouseMovePromoteDelayMs = 160,
                .gamepadStickyMs = 1800
            };
        case OwnerPolicyKind::PointerFirst:
            return OwnerPolicy{
                .mouseMoveCanPromote = true,
                .mouseMovePromotionTarget = MouseMovePromotionTarget::PresentationAndCursor,
                .mouseMoveThresholdPx = 4,
                .mouseMovePromoteDelayMs = 40,
                .gamepadStickyMs = 250
            };
        case OwnerPolicyKind::Neutral:
        default:
            return OwnerPolicy{
                .mouseMoveCanPromote = true,
                .mouseMovePromotionTarget = MouseMovePromotionTarget::CursorOnly,
                .mouseMoveThresholdPx = 10,
                .mouseMovePromoteDelayMs = 120,
                .gamepadStickyMs = 1500
            };
        }
    }

    std::string_view InputModalityTracker::ToString(PresentationOwner owner)
    {
        switch (owner) {
        case PresentationOwner::Gamepad:
            return "Gamepad";
        case PresentationOwner::KeyboardMouse:
        default:
            return "KeyboardMouse";
        }
    }

    std::string_view InputModalityTracker::ToString(NavigationOwner owner)
    {
        switch (owner) {
        case NavigationOwner::Gamepad:
            return "Gamepad";
        case NavigationOwner::KeyboardMouse:
            return "KeyboardMouse";
        case NavigationOwner::None:
        default:
            return "None";
        }
    }

    std::string_view InputModalityTracker::ToString(CursorOwner owner)
    {
        switch (owner) {
        case CursorOwner::Gamepad:
            return "Gamepad";
        case CursorOwner::KeyboardMouse:
        default:
            return "KeyboardMouse";
        }
    }

    std::string_view InputModalityTracker::ToString(GameplayOwner owner)
    {
        switch (owner) {
        case GameplayOwner::Gamepad:
            return "Gamepad";
        case GameplayOwner::KeyboardMouse:
        default:
            return "KeyboardMouse";
        }
    }

    std::string_view InputModalityTracker::ToString(PointerIntent intent)
    {
        switch (intent) {
        case PointerIntent::HoverOnly:
            return "HoverOnly";
        case PointerIntent::PointerActive:
            return "PointerActive";
        case PointerIntent::None:
        default:
            return "None";
        }
    }

    std::string_view InputModalityTracker::ToString(OwnerPolicyKind kind)
    {
        switch (kind) {
        case OwnerPolicyKind::StrictGamepadSticky:
            return "StrictGamepadSticky";
        case OwnerPolicyKind::PointerFirst:
            return "PointerFirst";
        case OwnerPolicyKind::Neutral:
        default:
            return "Neutral";
        }
    }

    std::string_view InputModalityTracker::ToString(MouseMovePromotionTarget target)
    {
        switch (target) {
        case MouseMovePromotionTarget::CursorOnly:
            return "CursorOnly";
        case MouseMovePromotionTarget::PresentationAndCursor:
            return "PresentationAndCursor";
        case MouseMovePromotionTarget::None:
        default:
            return "None";
        }
    }

    bool InputModalityTracker::IsUsingGamepadHook()
    {
        return GetSingleton().IsUsingGamepad();
    }

    bool InputModalityTracker::IsGamepadCursorHook()
    {
        auto& tracker = GetSingleton();
        return tracker.GetEffectiveCursorOwner(tracker._observedContext.load(std::memory_order_relaxed)) == CursorOwner::Gamepad;
    }

    bool InputModalityTracker::IsGamepadDeviceEnabledHook(RE::BSPCGamepadDeviceHandler* device)
    {
        const auto isEnabled = device != nullptr &&
            *reinterpret_cast<void* const*>(reinterpret_cast<const std::uint8_t*>(device) + kGamepadDelegateOffset) != nullptr;
        if (!isEnabled) {
            return false;
        }

        const auto* playerControls = RE::PlayerControls::GetSingleton();
        const auto playerRemapMode = playerControls && playerControls->data.remapMode;

        const auto* menuControls = RE::MenuControls::GetSingleton();
        const auto menuRemapMode = menuControls &&
            *reinterpret_cast<const bool*>(reinterpret_cast<const std::uint8_t*>(menuControls) + kMenuControlsRemapModeOffset);

        if (playerRemapMode || menuRemapMode) {
            return GetSingleton().IsUsingGamepad();
        }

        return true;
    }

    void InputModalityTracker::RefreshMenus()
    {
        auto& tracker = GetSingleton();
        bool expected = false;
        if (!tracker._refreshQueued.compare_exchange_strong(expected, true, std::memory_order_relaxed)) {
            return;
        }

        if (auto* taskInterface = SKSE::GetTaskInterface(); taskInterface) {
            taskInterface->AddUITask(DoRefreshMenus);
        }
        else {
            tracker._refreshQueued.store(false, std::memory_order_relaxed);
        }
    }

    void InputModalityTracker::DoRefreshMenus()
    {
        auto& tracker = GetSingleton();
        auto* ui = RE::UI::GetSingleton();
        if (ui) {
            for (auto& menu : ui->menuStack) {
                if (menu) {
                    menu->RefreshPlatform();
                    NotifyMenuPresentationChanged(*menu);
                }
            }
        }

        tracker._refreshQueued.store(false, std::memory_order_relaxed);
    }
}
