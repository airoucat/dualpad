#pragma once

#include <atomic>
#include <cstdint>

namespace RE
{
    class MouseMoveEvent;
    class PlayerControlsData;
    class ThumbstickEvent;
    struct LookHandler;
}

namespace dualpad::input
{
    class GameplayLookTransformHook
    {
    public:
        static GameplayLookTransformHook& GetSingleton();

        bool InstallI2DiagnosticCandidate();
        [[nodiscard]] bool IsInstalled() const noexcept;

    private:
        GameplayLookTransformHook() = default;

        static void ProcessThumbstickHook(
            RE::LookHandler* self,
            RE::ThumbstickEvent* event,
            RE::PlayerControlsData* data);
        static void ProcessMouseMoveHook(
            RE::LookHandler* self,
            RE::MouseMoveEvent* event,
            RE::PlayerControlsData* data);
        static void ScopedGameplayLookTransformHook(void* controls, float* lookVector);
        static bool TransformIsUsingGamepadHook(void* inputDeviceManager);

        std::atomic_bool _attempted{ false };
        std::atomic_bool _installed{ false };
        std::atomic_bool _routeEnabled{ false };
        std::atomic<std::uintptr_t> _originalThumbstick{ 0 };
        std::atomic<std::uintptr_t> _originalMouseMove{ 0 };
        std::atomic<std::uintptr_t> _originalTransform{ 0 };
        std::atomic<std::uintptr_t> _originalQuery{ 0 };
    };
}
