#pragma once

#include <RE/Skyrim.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string_view>

namespace dualpad::input
{
    class InputModalityTracker final :
        public RE::BSTEventSink<RE::InputEvent*>
    {
    public:
        static InputModalityTracker& GetSingleton();

        void Install();
        void Register();

        bool IsInstalled() const;
        bool IsUsingGamepad() const;
        void MarkSyntheticKeyboardScancode(
            std::uint8_t scancode,
            std::uint8_t pendingEvents = 1,
            std::uint64_t windowMs = 250);

        RE::BSEventNotifyControl ProcessEvent(
            RE::InputEvent* const* event,
            RE::BSTEventSource<RE::InputEvent*>* source) override;

    private:
        struct SuppressedScancodeState
        {
            std::uint8_t pendingEvents{ 0 };
            std::uint64_t expiresAtMs{ 0 };
        };

        InputModalityTracker();

        void InstallDeviceConnectHook();
        void InstallInputManagerHook();
        void InstallUsingGamepadHook();
        void InstallGamepadCursorHook();
        void InstallGamepadDeviceEnabledHook();
        bool ConsumeSyntheticKeyboardEvent(std::uint32_t scancode);
        bool IsSyntheticKeyboardWindowActive() const;
        void SetUsingGamepad(bool usingGamepad, std::string_view reason);

        static bool IsUsingGamepadHook();
        static bool IsGamepadDeviceEnabledHook(RE::BSPCGamepadDeviceHandler* device);
        static void RefreshMenus();
        static void DoRefreshMenus();

        bool _registered{ false };
        bool _installed{ false };
        std::atomic_bool _usingGamepad{ false };
        std::atomic_bool _refreshQueued{ false };
        mutable std::atomic<std::uint64_t> _syntheticKeyboardWindowExpiresAtMs{ 0 };
        std::mutex _suppressionMutex;
        std::array<SuppressedScancodeState, 256> _suppressedKeyboardScancodes{};
    };
}
