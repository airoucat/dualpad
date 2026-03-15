#pragma once

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

    class NativeInputInjector final
    {
    public:
        static constexpr std::size_t kMaxStagedButtonEvents = 64;

        void Reset();
        bool CanHandleAction(std::string_view actionId) const;
        bool PulseButtonAction(std::string_view actionId);
        bool QueueButtonAction(std::string_view actionId, bool pressed, float heldSeconds = 0.0f);
        std::size_t FlushStagedButtonEventsToInputQueue();
        std::size_t PrependStagedButtonEventsToInputQueue(RE::InputEvent*& head, RE::InputEvent*& tail);
        std::size_t GetStagedButtonEventCount() const;
        void DiscardStagedButtonEvents();
        void PrependStagedButtonEvents(RE::InputEvent*& head);
        void ReleaseInjectedButtonEvents();

    private:
        struct StagedButtonEvent
        {
            RE::BSFixedString userEvent{};
            std::uint32_t idCode{ 0 };
            float value{ 0.0f };
            float heldSeconds{ 0.0f };
        };

        std::optional<NativeButtonActionBinding> ResolveButtonAction(std::string_view actionId) const;
        std::uint32_t ResolveMappedGamepadId(const NativeButtonActionBinding& binding) const;
        std::optional<RE::BSFixedString> ResolveRawUserEvent(std::uint32_t gamepadId, InputContext context) const;
        bool QueueRawButton(std::uint32_t gamepadId, InputContext context, float value, float heldSeconds);
        bool StageButtonEvent(
            std::uint32_t gamepadId,
            const RE::BSFixedString& userEvent,
            float value,
            float heldSeconds);
        void ClearStagedButtonEvents();
        bool CanStageButtonEvents() const;
        bool IsDebugLoggingEnabled() const;

        std::array<StagedButtonEvent, kMaxStagedButtonEvents> _stagedButtonEvents{};
        std::size_t _stagedButtonEventCount{ 0 };
        std::array<RE::ButtonEvent*, kMaxStagedButtonEvents> _injectedButtonEvents{};
        std::size_t _injectedButtonEventCount{ 0 };
    };
}
