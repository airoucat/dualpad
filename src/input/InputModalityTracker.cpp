#include "pch.h"
#include "input/InputModalityTracker.h"

#include <algorithm>
#include <optional>

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

        std::uint64_t GetMonotonicMs()
        {
            return ::GetTickCount64();
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
            _usingGamepad.store(gamepadHandler && gamepadHandler->IsEnabled(), std::memory_order_relaxed);
            Register();
        }

        _installed = true;
        logger::info("[DualPad][InputMode] Installed AutoInputSwitch-style modality tracker");
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
            _usingGamepad.store(gamepadHandler->IsEnabled(), std::memory_order_relaxed);
        }

        inputManager->AddEventSink(this);
        _registered = true;
        logger::info("[DualPad][InputMode] Registered input event sink");
    }

    bool InputModalityTracker::IsInstalled() const
    {
        return _installed;
    }

    bool InputModalityTracker::IsUsingGamepad() const
    {
        return _usingGamepad.load(std::memory_order_relaxed);
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

        auto* current = event ? *event : nullptr;
        std::optional<bool> desiredUsingGamepad;
        std::string_view reason;

        while (current) {
            switch (current->GetDevice()) {
            case RE::INPUT_DEVICE::kKeyboard:
                if (current->GetEventType() == RE::INPUT_EVENT_TYPE::kButton) {
                    const auto* buttonEvent = current->AsButtonEvent();
                    if (buttonEvent && ConsumeSyntheticKeyboardEvent(buttonEvent->GetIDCode())) {
                        current = current->next;
                        continue;
                    }
                }

                if (IsSyntheticKeyboardWindowActive()) {
                    current = current->next;
                    continue;
                }

                desiredUsingGamepad = false;
                reason = "keyboard";
                break;
            case RE::INPUT_DEVICE::kMouse:
                desiredUsingGamepad = false;
                reason = "mouse";
                break;
            case RE::INPUT_DEVICE::kGamepad:
                desiredUsingGamepad = true;
                reason = "gamepad";
                break;
            default:
                break;
            }

            current = current->next;
        }

        if (desiredUsingGamepad.has_value()) {
            SetUsingGamepad(*desiredUsingGamepad, reason);
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
        SKSE::GetTrampoline().write_call<6>(hook.address(), IsUsingGamepadHook);
    }

    void InputModalityTracker::InstallGamepadDeviceEnabledHook()
    {
        REL::Relocation<std::uintptr_t> gamepadHandlerVtbl{ kGamepadHandlerVtblId };
        gamepadHandlerVtbl.write_vfunc(kGamepadIsEnabledVfuncIndex, IsGamepadDeviceEnabledHook);
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

    void InputModalityTracker::SetUsingGamepad(bool usingGamepad, std::string_view reason)
    {
        const auto previous = _usingGamepad.exchange(usingGamepad, std::memory_order_relaxed);
        if (previous == usingGamepad) {
            return;
        }

        logger::info(
            "[DualPad][InputMode] Platform switched to {} via {} input",
            usingGamepad ? "gamepad" : "keyboard/mouse",
            reason);
        RefreshMenus();
    }

    bool InputModalityTracker::IsUsingGamepadHook()
    {
        return GetSingleton().IsUsingGamepad();
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
                }
            }
        }

        tracker._refreshQueued.store(false, std::memory_order_relaxed);
    }
}
