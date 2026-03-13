#pragma once

#include "input/InputContext.h"
#include "input/mapping/PadEvent.h"

#include <cstdint>
#include <string_view>

namespace dualpad::input
{
    class CompatibilityInputInjector;
    class NativeInputInjector;

    enum class ActionDispatchTarget : std::uint8_t
    {
        None = 0,
        ButtonEvent,
        KeyboardNative,
        CompatibilityPulse,
        CompatibilityState,
        NativePulse,
        NativeState,
        Plugin
    };

    struct ActionDispatchResult
    {
        bool handled{ false };
        ActionDispatchTarget target{ ActionDispatchTarget::None };
    };

    class ActionDispatcher
    {
    public:
        ActionDispatcher(
            CompatibilityInputInjector& compatibilityInjector,
            NativeInputInjector& nativeInjector);

        void DispatchDirectPadEvent(const PadEvent& event) const;
        ActionDispatchResult Dispatch(std::string_view actionId, InputContext context) const;
        ActionDispatchResult DispatchButtonState(
            std::string_view actionId,
            bool down,
            float heldSeconds,
            InputContext context) const;

    private:
        std::uint32_t ResolveCompatibilityPulseBit(std::string_view actionId) const;

        CompatibilityInputInjector& _compatibilityInjector;
        NativeInputInjector& _nativeInjector;
    };

    std::string_view ToString(ActionDispatchTarget target);
}
