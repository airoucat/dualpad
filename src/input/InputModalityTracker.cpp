#include "pch.h"
#include "input/InputModalityTracker.h"

#include "input/Action.h"
#include "input/GameplayKbmFactTracker.h"
#include "input/PadProfile.h"
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
                if (const auto* buttonEvent = event.AsButtonEvent()) {
                    return buttonEvent->IsDown();
                }
                return false;

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
                        // Left stick belongs to MoveOwner. Do not let it churn the
                        // coarse gameplay owner; it can still influence the
                        // engine-facing presentation latch separately.
                        return false;
                    }
                }
                return false;

            default:
                return false;
            }
        }

        bool ShouldUpdateGameplayPresentationFromGamepadEvent(const RE::InputEvent& event)
        {
            switch (event.GetEventType()) {
            case RE::INPUT_EVENT_TYPE::kButton:
                if (const auto* buttonEvent = event.AsButtonEvent()) {
                    return buttonEvent->IsDown();
                }
                return false;

            case RE::INPUT_EVENT_TYPE::kThumbstick:
                if (const auto* thumbstickEvent = event.AsThumbstickEvent()) {
                    const bool meaningful =
                        std::fabs(thumbstickEvent->xValue) >= kThumbstickPromotionThreshold ||
                        std::fabs(thumbstickEvent->yValue) >= kThumbstickPromotionThreshold;
                    if (!meaningful) {
                        return false;
                    }

                    const auto thumbstickId =
                        static_cast<RE::ThumbstickEvent::InputType>(thumbstickEvent->idCode);
                    return thumbstickId == RE::ThumbstickEvent::InputType::kLeftThumbstick ||
                        thumbstickId == RE::ThumbstickEvent::InputType::kRightThumbstick;
                }
                return false;

            default:
                return false;
            }
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
            _gameplayMenuEntrySeed.store(initialPresentationOwner, std::memory_order_relaxed);
            _engineGameplayPresentationLatch.store(initialPresentationOwner, std::memory_order_relaxed);

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
            _gameplayMenuEntrySeed.store(initialPresentationOwner, std::memory_order_relaxed);
            _engineGameplayPresentationLatch.store(initialPresentationOwner, std::memory_order_relaxed);
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
        return ResolveIsUsingGamepad();
    }

    bool InputModalityTracker::ResolveIsUsingGamepad() const
    {
        auto& contextManager = ContextManager::GetSingleton();
        const auto authoritativeContext = contextManager.GetCurrentContext();
        const auto authoritativeEpoch = contextManager.GetCurrentEpoch();
        const auto observedContext = _observedContext.load(std::memory_order_relaxed);
        const auto observedEpoch = _observedContextEpoch.load(std::memory_order_relaxed);
        const bool contextReconciled =
            authoritativeContext == observedContext &&
            authoritativeEpoch == observedEpoch;
        const auto context = contextReconciled ? observedContext : authoritativeContext;

        if (IsGameplayDomainContext(context)) {
            const auto& config = RuntimeConfig::GetSingleton();
            if (!config.EnableGameplayOwnership()) {
                if (IsSyntheticGamepadSprintActive()) {
                    return true;
                }

                return _gameplayOwner.load(std::memory_order_relaxed) == GameplayOwner::Gamepad;
            }

            if (IsSyntheticGamepadSprintActive()) {
                return true;
            }

            return _engineGameplayPresentationLatch.load(std::memory_order_relaxed) ==
                PresentationOwner::Gamepad;
        }

        return GetEffectivePresentationOwner(context) == PresentationOwner::Gamepad;
    }

    void InputModalityTracker::SetEngineGameplayPresentationLatch(
        PresentationOwner owner,
        InputContext context,
        std::uint32_t epoch,
        std::string_view reason) const
    {
        if (!IsGameplayDomainContext(context)) {
            return;
        }

        const auto previous =
            _engineGameplayPresentationLatch.exchange(owner, std::memory_order_relaxed);
        if (previous == owner) {
            return;
        }

        logger::info(
            "[DualPad][InputOwner] EngineGameplayPresentationLatch {} -> {} via {} (ctx={}, epoch={})",
            ToString(previous),
            ToString(owner),
            reason,
            dualpad::input::ToString(context),
            epoch);
    }

    bool InputModalityTracker::IsGameplayUsingGamepad() const
    {
        return _gameplayOwner.load(std::memory_order_relaxed) == GameplayOwner::Gamepad;
    }

    bool InputModalityTracker::IsGameplayMenuEntrySeedGamepad() const
    {
        return _gameplayMenuEntrySeed.load(std::memory_order_relaxed) == PresentationOwner::Gamepad;
    }

    void InputModalityTracker::ApplyGameplayMenuInheritance(InputContext context, std::string_view reason)
    {
        const auto inheritedPresentationSeed =
            _gameplayMenuEntrySeed.load(std::memory_order_relaxed);
        const auto inheritedPresentationOwner =
            inheritedPresentationSeed == PresentationOwner::Gamepad ?
            PresentationOwner::Gamepad :
            PresentationOwner::KeyboardMouse;

        SetPresentationOwner(inheritedPresentationOwner, context, reason);
        SetNavigationOwner(
            inheritedPresentationSeed == PresentationOwner::Gamepad ?
            NavigationOwner::Gamepad :
            NavigationOwner::KeyboardMouse);
        SetCursorOwner(
            inheritedPresentationSeed == PresentationOwner::Gamepad ?
            CursorOwner::Gamepad :
            CursorOwner::KeyboardMouse,
            context,
            reason);
        GameplayKbmFactTracker::GetSingleton().Reset();
    }

    void InputModalityTracker::SyncGameplayPresentationFromCoordinator(
        InputContext context,
        std::uint32_t epoch,
        std::string_view reason)
    {
        if (!IsGameplayDomainContext(context)) {
            return;
        }

        const auto state =
            GameplayOwnershipCoordinator::GetSingleton().GetPublishedGameplayPresentationState();
        const auto menuEntryOwner =
            state.menuEntryOwner == GameplayOwnershipCoordinator::ChannelOwner::Gamepad ?
            PresentationOwner::Gamepad :
            PresentationOwner::KeyboardMouse;
        const auto engineOwner =
            state.engineOwner == GameplayOwnershipCoordinator::ChannelOwner::Gamepad ?
            PresentationOwner::Gamepad :
            PresentationOwner::KeyboardMouse;

        SetGameplayMenuEntrySeed(menuEntryOwner, context, reason);
        SetEngineGameplayPresentationLatch(engineOwner, context, epoch, reason);
    }

    void InputModalityTracker::OnAuthoritativeMenuOpen(InputContext context, std::uint32_t epoch)
    {
        if (!IsMenuOwnedContext(context)) {
            return;
        }

        const auto previousContext = _observedContext.load(std::memory_order_relaxed);
        const auto previousEpoch = _observedContextEpoch.load(std::memory_order_relaxed);
        if (previousContext == context && previousEpoch == epoch) {
            return;
        }

        const auto previousWasGameplay = IsGameplayDomainContext(previousContext);

        _observedContext.store(context, std::memory_order_relaxed);
        _observedContextEpoch.store(epoch, std::memory_order_relaxed);
        ResetMouseMoveAccumulator();
        SetPointerIntent(PointerIntent::None);

        if (previousWasGameplay) {
            ApplyGameplayMenuInheritance(context, "authoritative-enter-menu");
        }

        logger::info(
            "[DualPad][InputOwner] Authoritative menu-open sync {} -> {} (presentation={}, navigation={}, cursor={}, gameplay={})",
            dualpad::input::ToString(previousContext),
            dualpad::input::ToString(context),
            ToString(_presentationOwner.load(std::memory_order_relaxed)),
            ToString(_navigationOwner.load(std::memory_order_relaxed)),
            ToString(_cursorOwner.load(std::memory_order_relaxed)),
            ToString(_gameplayOwner.load(std::memory_order_relaxed)));
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
            auto& gameplayOwnership = GameplayOwnershipCoordinator::GetSingleton();
            gameplayOwnership.RefreshPublishedGameplayPresentation(context);
            SyncGameplayPresentationFromCoordinator(context, epoch, "exit-menu");
            GameplayKbmFactTracker::GetSingleton().Reset();
        }

        if (previousWasGameplay && !currentIsGameplay) {
            ApplyGameplayMenuInheritance(context, "enter-menu");
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
        bool shouldPromote = true;
        const bool menuContext = IsMenuContextActive(context);
        if (event.GetEventType() == RE::INPUT_EVENT_TYPE::kButton) {
            const auto* buttonEvent = event.AsButtonEvent();
            if (buttonEvent) {
                const auto keyCode = buttonEvent->GetIDCode();
                const bool consumedSynthetic = ConsumeSyntheticKeyboardEvent(keyCode);
                if (menuContext) {
                    logger::info(
                        "[DualPad][MenuProbe] keyboard-event ctx={} code={} pressed={} down={} held={} up={} syntheticConsumed={}",
                        dualpad::input::ToString(context),
                        keyCode,
                        buttonEvent->IsPressed(),
                        buttonEvent->IsDown(),
                        buttonEvent->IsHeld(),
                        buttonEvent->IsUp(),
                        consumedSynthetic);
                }
                if (consumedSynthetic) {
                    return;
                }

                shouldPromote = buttonEvent->IsDown();
            }
        }

        const bool syntheticWindowActive = IsSyntheticKeyboardWindowActive();
        if (menuContext && event.GetEventType() == RE::INPUT_EVENT_TYPE::kChar) {
            logger::info(
                "[DualPad][MenuProbe] keyboard-event ctx={} type=Char syntheticWindowActive={}",
                dualpad::input::ToString(context),
                syntheticWindowActive);
        }

        if (syntheticWindowActive) {
            if (menuContext) {
                logger::info(
                    "[DualPad][MenuProbe] keyboard-event ctx={} suppressedBySyntheticWindow=true",
                    dualpad::input::ToString(context));
            }
            return;
        }

        if (!shouldPromote) {
            return;
        }

        if (menuContext) {
            logger::info(
                "[DualPad][MenuProbe] keyboard-event ctx={} promotingToKeyboardMouse=true type={}",
                dualpad::input::ToString(context),
                event.GetEventType() == RE::INPUT_EVENT_TYPE::kChar ? "Char" : "Button");
        }

        PromoteToKeyboardMouse(context, event.GetEventType() == RE::INPUT_EVENT_TYPE::kChar ? "keyboard-char" : "keyboard", PointerIntent::None);
    }

    void InputModalityTracker::HandleMouseEvent(const RE::InputEvent& event, InputContext context, const OwnerPolicy& policy)
    {
        bool shouldPromote = true;
        if (IsMenuContextActive(context) && event.GetEventType() == RE::INPUT_EVENT_TYPE::kButton) {
            if (const auto* buttonEvent = event.AsButtonEvent()) {
                shouldPromote = buttonEvent->IsDown();
                logger::info(
                    "[DualPad][MenuProbe] mouse-event ctx={} code={} pressed={} down={} held={} up={} promotingToKeyboardMouse={}",
                    dualpad::input::ToString(context),
                    buttonEvent->GetIDCode(),
                    buttonEvent->IsPressed(),
                    buttonEvent->IsDown(),
                    buttonEvent->IsHeld(),
                    buttonEvent->IsUp(),
                    shouldPromote);
            }
        }

        if (event.GetEventType() == RE::INPUT_EVENT_TYPE::kMouseMove) {
            if (const auto* mouseMoveEvent = event.AsMouseMoveEvent()) {
                HandleMouseMoveEvent(*mouseMoveEvent, context, policy);
            }
            return;
        }

        if (!shouldPromote) {
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
        const bool meaningful = IsMeaningfulGamepadEvent(event);
        bool shouldPromote = meaningful;
        if (IsMenuContextActive(context)) {
            switch (event.GetEventType()) {
            case RE::INPUT_EVENT_TYPE::kButton:
                if (const auto* buttonEvent = event.AsButtonEvent()) {
                    shouldPromote = buttonEvent->IsDown();
                    logger::info(
                        "[DualPad][MenuProbe] gamepad-event ctx={} type=Button code={} pressed={} down={} held={} up={} meaningful={} promotingToGamepad={}",
                        dualpad::input::ToString(context),
                        buttonEvent->GetIDCode(),
                        buttonEvent->IsPressed(),
                        buttonEvent->IsDown(),
                        buttonEvent->IsHeld(),
                        buttonEvent->IsUp(),
                        meaningful,
                        shouldPromote);
                }
                break;
            case RE::INPUT_EVENT_TYPE::kThumbstick:
                if (const auto* thumbstickEvent = event.AsThumbstickEvent()) {
                    logger::info(
                        "[DualPad][MenuProbe] gamepad-event ctx={} type=Thumbstick id={} x={:.3f} y={:.3f} meaningful={} promotingToGamepad={}",
                        dualpad::input::ToString(context),
                        thumbstickEvent->idCode,
                        thumbstickEvent->xValue,
                        thumbstickEvent->yValue,
                        meaningful,
                        meaningful);
                }
                break;
            default:
                logger::info(
                    "[DualPad][MenuProbe] gamepad-event ctx={} type={} meaningful={} promotingToGamepad={}",
                    dualpad::input::ToString(context),
                    static_cast<int>(event.GetEventType()),
                    meaningful,
                    meaningful);
                break;
            }
        }

        if (!shouldPromote) {
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
        auto& gameplayOwnership = GameplayOwnershipCoordinator::GetSingleton();
        const auto epoch = ContextManager::GetSingleton().GetCurrentEpoch();

        switch (event.GetDevice()) {
        case RE::INPUT_DEVICE::kKeyboard:
            if (event.GetEventType() == RE::INPUT_EVENT_TYPE::kButton) {
                const auto* buttonEvent = event.AsButtonEvent();
                if (buttonEvent && ConsumeSyntheticKeyboardEvent(buttonEvent->GetIDCode())) {
                    return;
                }
                if (buttonEvent) {
                    auto& factTracker = GameplayKbmFactTracker::GetSingleton();
                    factTracker.ObserveButtonEvent(*buttonEvent);
                    if (factTracker.IsSprintMappedButton(*buttonEvent)) {
                        // Sprint is a sustained digital action with its own
                        // contributor aggregation. Let it update held facts.
                        // A real key-down still counts as an explicit KBM
                        // presentation hint, but should not churn gameplay owner.
                        if (!IsSyntheticKeyboardWindowActive() && buttonEvent->IsDown()) {
                            gameplayOwnership.RecordGameplayPresentationHint(
                                context,
                                GameplayOwnershipCoordinator::PresentationHint::KeyboardMouseExplicit,
                                "keyboard-sprint");
                            SyncGameplayPresentationFromCoordinator(context, epoch, "keyboard-sprint");
                        }
                        return;
                    }
                    if (!buttonEvent->IsDown()) {
                        return;
                    }
                }
            } else if (event.GetEventType() != RE::INPUT_EVENT_TYPE::kChar) {
                return;
            }

            if (IsSyntheticKeyboardWindowActive()) {
                return;
            }

            SetGameplayOwner(
                GameplayOwner::KeyboardMouse,
                context,
                event.GetEventType() == RE::INPUT_EVENT_TYPE::kChar ? "keyboard-char" : "keyboard");
            gameplayOwnership.RecordGameplayPresentationHint(
                context,
                GameplayOwnershipCoordinator::PresentationHint::KeyboardMouseExplicit,
                event.GetEventType() == RE::INPUT_EVENT_TYPE::kChar ? "keyboard-char" : "keyboard");
            SyncGameplayPresentationFromCoordinator(
                context,
                epoch,
                event.GetEventType() == RE::INPUT_EVENT_TYPE::kChar ? "keyboard-char" : "keyboard");
            return;

        case RE::INPUT_DEVICE::kMouse:
            if (event.GetEventType() == RE::INPUT_EVENT_TYPE::kMouseMove) {
                const auto* mouseMoveEvent = event.AsMouseMoveEvent();
                if (!mouseMoveEvent || (mouseMoveEvent->mouseInputX == 0 && mouseMoveEvent->mouseInputY == 0)) {
                    return;
                }
                GameplayKbmFactTracker::GetSingleton().MarkMouseLookActivity();
                SetGameplayOwner(GameplayOwner::KeyboardMouse, context, "mouse-move");
                gameplayOwnership.RecordGameplayPresentationHint(
                    context,
                    GameplayOwnershipCoordinator::PresentationHint::KeyboardMouseLookOnly,
                    "mouse-move");
                SyncGameplayPresentationFromCoordinator(context, epoch, "mouse-move");
                return;
            }

            if (event.GetEventType() == RE::INPUT_EVENT_TYPE::kButton) {
                if (const auto* buttonEvent = event.AsButtonEvent()) {
                    auto& factTracker = GameplayKbmFactTracker::GetSingleton();
                    factTracker.ObserveButtonEvent(*buttonEvent);
                    if (factTracker.IsSprintMappedButton(*buttonEvent)) {
                        // Same as keyboard sprint: keep sustained held facts.
                        // A real mouse button-down still counts as an explicit
                        // KBM presentation hint, but should not churn gameplay owner.
                        if (buttonEvent->IsDown()) {
                            gameplayOwnership.RecordGameplayPresentationHint(
                                context,
                                GameplayOwnershipCoordinator::PresentationHint::KeyboardMouseExplicit,
                                "mouse-sprint-button");
                            SyncGameplayPresentationFromCoordinator(
                                context,
                                epoch,
                                "mouse-sprint-button");
                        }
                        return;
                    }
                    if (!buttonEvent->IsDown()) {
                        return;
                    }
                }
            }

            SetGameplayOwner(
                GameplayOwner::KeyboardMouse,
                context,
                event.GetEventType() == RE::INPUT_EVENT_TYPE::kButton ? "mouse-button" : "mouse-wheel");
            gameplayOwnership.RecordGameplayPresentationHint(
                context,
                GameplayOwnershipCoordinator::PresentationHint::KeyboardMouseExplicit,
                event.GetEventType() == RE::INPUT_EVENT_TYPE::kButton ? "mouse-button" : "mouse-wheel");
            SyncGameplayPresentationFromCoordinator(
                context,
                epoch,
                event.GetEventType() == RE::INPUT_EVENT_TYPE::kButton ? "mouse-button" : "mouse-wheel");
            return;

        case RE::INPUT_DEVICE::kGamepad:
            {
                const bool shouldPromoteGameplayOwner =
                    ShouldPromoteGameplayOwnerFromGamepadEvent(event);
                const bool shouldUpdateGameplayPresentation =
                    ShouldUpdateGameplayPresentationFromGamepadEvent(event);
                if (!shouldPromoteGameplayOwner && !shouldUpdateGameplayPresentation) {
                    return;
                }

                std::string_view ownerReason = "gamepad";
                std::string_view presentationReason = "gamepad";
                if (event.GetEventType() == RE::INPUT_EVENT_TYPE::kThumbstick) {
                    if (const auto* thumbstickEvent = event.AsThumbstickEvent()) {
                        const auto thumbstickId =
                            static_cast<RE::ThumbstickEvent::InputType>(thumbstickEvent->idCode);
                        if (thumbstickId == RE::ThumbstickEvent::InputType::kLeftThumbstick) {
                            presentationReason = "gamepad-left-thumbstick-presentation";
                        } else {
                            ownerReason = "gamepad-right-thumbstick-owner";
                            presentationReason = "gamepad-right-thumbstick-presentation";
                        }
                    }
                }

                if (shouldPromoteGameplayOwner) {
                    SetGameplayOwner(
                        GameplayOwner::Gamepad,
                        context,
                        ownerReason);
                }
                if (shouldUpdateGameplayPresentation) {
                    gameplayOwnership.RecordGameplayPresentationHint(
                        context,
                        shouldPromoteGameplayOwner ?
                        GameplayOwnershipCoordinator::PresentationHint::GamepadExplicit :
                        GameplayOwnershipCoordinator::PresentationHint::GamepadMoveOnly,
                        presentationReason);
                    SyncGameplayPresentationFromCoordinator(context, epoch, presentationReason);
                }
            }
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

        // Some menus keep pointer/gamepad affordances cached more aggressively
        // than the cursor hook alone. Refresh on real cursor-owner flips so
        // mouse-entered menus can hand control back to gamepad without reopen.
        if (IsMenuContextActive(context)) {
            RefreshMenus();
        }
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

    void InputModalityTracker::SetGameplayMenuEntrySeed(
        PresentationOwner owner,
        InputContext context,
        std::string_view reason)
    {
        const auto previous = _gameplayMenuEntrySeed.exchange(owner, std::memory_order_relaxed);
        if (previous == owner) {
            return;
        }

        logger::info(
            "[DualPad][InputOwner] GameplayMenuEntrySeed {} -> {} via {} (ctx={})",
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
            auto& tracker = GetSingleton();
            return tracker.ResolveIsUsingGamepad();
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
