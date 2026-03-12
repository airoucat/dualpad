#pragma once

#include "input/injection/IGameInputInjector.h"
#include "input/InputContext.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace dualpad::input
{
    struct NativeButtonActionBinding
    {
        std::string_view actionId;
        std::string_view userEvent;
        RE::ControlMap::InputContextID context{ RE::UserEvents::INPUT_CONTEXT_ID::kGameplay };
        std::uint32_t fallbackGamepadId{ RE::ControlMap::kInvalid };
    };

    class NativeInputInjector final : public IGameInputInjector
    {
    public:
        static constexpr std::size_t kMaxStagedButtonEvents = 64;

        void Reset() override;
        void SubmitFrame(const SyntheticPadFrame& frame, std::uint32_t handledButtons) override;
        std::uint32_t SubmitDigitalButtons(const SyntheticPadFrame& frame, std::uint32_t handledButtons);

        bool IsAvailable() const;
        bool ShouldUseAsMainPath() const;
        bool ShouldUseForButtonActions() const;
        bool CanHandleAction(std::string_view actionId) const;
        bool CanHandleRawPadButton(std::uint32_t padBit) const;
        bool PulseButtonAction(std::string_view actionId);
        bool QueueButtonAction(std::string_view actionId, bool pressed, float heldSeconds = 0.0f);
        bool PulseRawPadButton(std::uint32_t padBit, InputContext context);
        bool QueueRawPadButton(std::uint32_t padBit, InputContext context, bool pressed, float heldSeconds = 0.0f);
        bool QueueThumbstick( RE::ThumbstickEvent::InputType inputType, float xValue, float yValue) const;
        std::size_t FlushStagedButtonEventsToInputQueue();
        std::size_t PrependStagedButtonEventsToInputQueue(RE::InputEvent*& head, RE::InputEvent*& tail);
        std::size_t PrependStagedButtonEventsUsingQueueCache(RE::InputEvent*& head);
        std::size_t AppendStagedButtonEventsUsingEngineCache(RE::InputEvent*& head);
        std::size_t GetStagedButtonEventCount() const;
        void DiscardStagedButtonEvents();
        void PrependStagedButtonEvents(RE::InputEvent*& head);
        void ReleaseInjectedButtonEvents();

    private:
        struct ButtonLifecycleDescriptor
        {
            RE::ControlMap::InputContextID context{ RE::UserEvents::INPUT_CONTEXT_ID::kGameplay };
            bool deferUntilGameplayGateOpens{ false };
        };

        struct StagedButtonEvent
        {
            RE::BSFixedString userEvent{};
            std::uint32_t idCode{ 0 };
            float value{ 0.0f };
            float heldSeconds{ 0.0f };
            ButtonLifecycleDescriptor lifecycle{};
        };

        struct PendingButtonRelease
        {
            RE::BSFixedString userEvent{};
            std::uint32_t idCode{ 0 };
            float heldSeconds{ 0.0f };
            ButtonLifecycleDescriptor lifecycle{};
            bool ready{ false };
        };

        struct PendingButtonPulse
        {
            RE::BSFixedString userEvent{};
            std::uint32_t idCode{ 0 };
            float releaseHeldSeconds{ 0.0f };
            ButtonLifecycleDescriptor lifecycle{};
        };

        std::uint32_t SubmitDigitalButtonsInternal(const SyntheticPadFrame& frame, std::uint32_t handledButtons);
        std::optional<NativeButtonActionBinding> ResolveButtonAction(std::string_view actionId) const;
        std::uint32_t ResolveMappedGamepadId(const NativeButtonActionBinding& binding) const;
        std::optional<std::uint32_t> ResolveRawGamepadId(std::uint32_t padBit) const;
        std::optional<RE::BSFixedString> ResolveRawUserEvent(std::uint32_t gamepadId, InputContext context) const;
        ButtonLifecycleDescriptor MakeLifecycleDescriptor(
            RE::ControlMap::InputContextID context,
            bool deferUntilGameplayGateOpens) const;
        bool QueueRawButton(std::uint32_t gamepadId, InputContext context, float value, float heldSeconds);
        bool SchedulePendingButtonPulse(
            std::uint32_t gamepadId,
            const RE::BSFixedString& userEvent,
            float releaseHeldSeconds,
            const ButtonLifecycleDescriptor& lifecycle);
        bool SchedulePendingButtonRelease(
            std::uint32_t gamepadId,
            const RE::BSFixedString& userEvent,
            float heldSeconds,
            const ButtonLifecycleDescriptor& lifecycle);
        void PromotePendingButtonPulses(bool gameplayGateOpen);
        void PromotePendingButtonReleases(bool gameplayGateOpen);
        void ArmPendingButtonReleases();
        void ClearPendingButtonPulses();
        void ClearPendingButtonReleases();
        bool StageButtonEvent(
            std::uint32_t gamepadId,
            const RE::BSFixedString& userEvent,
            float value,
            float heldSeconds,
            const ButtonLifecycleDescriptor& lifecycle);
        void ClearStagedButtonEvents();
        bool CanStageButtonEvents() const;
        bool IsGameplayGateOpenForFlush() const;
        bool IsDebugLoggingEnabled() const;

        std::uint32_t _submittedDownMask{ 0 };
        std::uint32_t _pendingDigitalReleaseMask{ 0 };
        std::uint64_t _leftTriggerPressedAtUs{ 0 };
        std::uint64_t _rightTriggerPressedAtUs{ 0 };
        std::array<float, 32> _pendingDigitalReleaseHeldSeconds{};
        float _lastLeftThumbX{ 0.0f };
        float _lastLeftThumbY{ 0.0f };
        float _lastRightThumbX{ 0.0f };
        float _lastRightThumbY{ 0.0f };
        bool _leftThumbActive{ false };
        bool _rightThumbActive{ false };
        std::array<StagedButtonEvent, kMaxStagedButtonEvents> _stagedButtonEvents{};
        std::size_t _stagedButtonEventCount{ 0 };
        std::array<PendingButtonPulse, kMaxStagedButtonEvents> _pendingButtonPulses{};
        std::size_t _pendingButtonPulseCount{ 0 };
        std::array<PendingButtonRelease, kMaxStagedButtonEvents> _pendingButtonReleases{};
        std::size_t _pendingButtonReleaseCount{ 0 };
        std::array<RE::ButtonEvent*, kMaxStagedButtonEvents> _injectedButtonEvents{};
        std::size_t _injectedButtonEventCount{ 0 };
    };
}
