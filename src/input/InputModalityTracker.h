#pragma once

#include <RE/Skyrim.h>

#include "input/InputContext.h"
#include "input/backend/PollCommitCoordinator.h"

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
        void OnAuthoritativeMenuOpen(InputContext context, std::uint32_t epoch);

        bool IsInstalled() const;
        bool IsUsingGamepad() const;
        bool IsGameplayUsingGamepad() const;
        bool IsGameplayMenuEntrySeedGamepad() const;
        void MarkSyntheticKeyboardScancode(
            std::uint8_t scancode,
            std::uint8_t pendingEvents = 1,
            std::uint64_t windowMs = 250);

        RE::BSEventNotifyControl ProcessEvent(
            RE::InputEvent* const* event,
            RE::BSTEventSource<RE::InputEvent*>* source) override;

    private:
        enum class PresentationOwner : std::uint8_t
        {
            Gamepad,
            KeyboardMouse
        };

        enum class NavigationOwner : std::uint8_t
        {
            None,
            Gamepad,
            KeyboardMouse
        };

        enum class CursorOwner : std::uint8_t
        {
            Gamepad,
            KeyboardMouse
        };

        enum class GameplayOwner : std::uint8_t
        {
            Gamepad,
            KeyboardMouse
        };

        enum class PointerIntent : std::uint8_t
        {
            None,
            HoverOnly,
            PointerActive
        };

        enum class OwnerPolicyKind : std::uint8_t
        {
            StrictGamepadSticky,
            Neutral,
            PointerFirst
        };

        enum class MouseMovePromotionTarget : std::uint8_t
        {
            None,
            CursorOnly,
            PresentationAndCursor
        };

        struct SuppressedScancodeState
        {
            std::uint8_t pendingEvents{ 0 };
            std::uint64_t expiresAtMs{ 0 };
        };

        struct MouseMoveAccumulator
        {
            std::int32_t dx{ 0 };
            std::int32_t dy{ 0 };
            std::uint64_t windowStartMs{ 0 };
            std::uint64_t lastMoveAtMs{ 0 };
        };

        struct OwnerPolicy
        {
            bool mouseMoveCanPromote{ false };
            MouseMovePromotionTarget mouseMovePromotionTarget{ MouseMovePromotionTarget::None };
            std::int32_t mouseMoveThresholdPx{ 10 };
            std::uint64_t mouseMovePromoteDelayMs{ 120 };
            std::uint64_t gamepadStickyMs{ 1500 };
        };

        InputModalityTracker();

        void InstallDeviceConnectHook();
        void InstallInputManagerHook();
        void InstallUsingGamepadHook();
        void InstallGamepadCursorHook();
        void InstallGamepadDeviceEnabledHook();
        void ReconcileContextState();
        void HandleKeyboardEvent(const RE::InputEvent& event, InputContext context, const OwnerPolicy& policy);
        void HandleMouseEvent(const RE::InputEvent& event, InputContext context, const OwnerPolicy& policy);
        void HandleMouseMoveEvent(const RE::MouseMoveEvent& event, InputContext context, const OwnerPolicy& policy);
        void HandleGamepadEvent(const RE::InputEvent& event, InputContext context, const OwnerPolicy& policy);
        void HandleGameplayOnlyEvent(const RE::InputEvent& event, InputContext context);
        bool ConsumeSyntheticKeyboardEvent(std::uint32_t scancode);
        bool ResolveIsUsingGamepad() const;
        void ApplyGameplayMenuInheritance(InputContext context, std::string_view reason);
        void SyncGameplayPresentationFromCoordinator(
            InputContext context,
            std::uint32_t epoch,
            std::string_view reason);
        void SetEngineGameplayPresentationLatch(
            PresentationOwner owner,
            InputContext context,
            std::uint32_t epoch,
            std::string_view reason) const;
        bool IsSyntheticKeyboardWindowActive() const;
        bool IsGamepadLeaseActive() const;
        bool IsMenuContextActive() const;
        bool IsMenuContextActive(InputContext context) const;
        void RefreshGamepadLease(std::uint64_t windowMs = 1500);
        void PromoteToGamepad(InputContext context, const OwnerPolicy& policy, std::string_view reason);
        void PromoteToKeyboardMouse(InputContext context, std::string_view reason, PointerIntent pointerIntent);
        PresentationOwner GetEffectivePresentationOwner(InputContext context) const;
        CursorOwner GetEffectiveCursorOwner(InputContext context) const;
        void SetPresentationOwner(PresentationOwner owner, InputContext context, std::string_view reason);
        void SetNavigationOwner(NavigationOwner owner);
        void SetCursorOwner(CursorOwner owner, InputContext context, std::string_view reason);
        void SetGameplayOwner(GameplayOwner owner, InputContext context, std::string_view reason);
        void SetGameplayMenuEntrySeed(PresentationOwner owner, InputContext context, std::string_view reason);
        void SetPointerIntent(PointerIntent intent);
        void ResetMouseMoveAccumulator();
        void AccumulateMouseMove(const RE::MouseMoveEvent& event, std::uint64_t nowMs);
        bool ShouldPromoteMouseMoveToKeyboardMouse(const OwnerPolicy& policy, std::uint64_t nowMs) const;
        OwnerPolicyKind ResolveOwnerPolicyKind(InputContext context) const;
        OwnerPolicy ResolveOwnerPolicy(OwnerPolicyKind kind) const;
        static std::string_view ToString(PresentationOwner owner);
        static std::string_view ToString(NavigationOwner owner);
        static std::string_view ToString(CursorOwner owner);
        static std::string_view ToString(GameplayOwner owner);
        static std::string_view ToString(PointerIntent intent);
        static std::string_view ToString(OwnerPolicyKind kind);
        static std::string_view ToString(MouseMovePromotionTarget target);

        static bool IsUsingGamepadHook();
        static bool IsGamepadCursorHook();
        static bool IsGamepadDeviceEnabledHook(RE::BSPCGamepadDeviceHandler* device);
        static void RefreshMenus();
        static void DoRefreshMenus();

        bool _registered{ false };
        bool _installed{ false };
        std::atomic<PresentationOwner> _presentationOwner{ PresentationOwner::KeyboardMouse };
        std::atomic<NavigationOwner> _navigationOwner{ NavigationOwner::None };
        std::atomic<CursorOwner> _cursorOwner{ CursorOwner::KeyboardMouse };
        std::atomic<GameplayOwner> _gameplayOwner{ GameplayOwner::KeyboardMouse };
        std::atomic<PresentationOwner> _gameplayMenuEntrySeed{ PresentationOwner::KeyboardMouse };
        mutable std::atomic<PresentationOwner> _engineGameplayPresentationLatch{ PresentationOwner::KeyboardMouse };
        std::atomic<PointerIntent> _pointerIntent{ PointerIntent::None };
        std::atomic_bool _refreshQueued{ false };
        std::atomic<InputContext> _observedContext{ InputContext::Gameplay };
        std::atomic<std::uint32_t> _observedContextEpoch{ 0 };
        mutable std::atomic<std::uint64_t> _syntheticKeyboardWindowExpiresAtMs{ 0 };
        mutable std::atomic<std::uint64_t> _gamepadLeaseExpiresAtMs{ 0 };
        std::mutex _suppressionMutex;
        mutable std::mutex _mouseMoveMutex;
        std::array<SuppressedScancodeState, 256> _suppressedKeyboardScancodes{};
        MouseMoveAccumulator _mouseMoveAccumulator{};
    };
}
