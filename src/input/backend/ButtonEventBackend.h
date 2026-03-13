#pragma once

#include "input/backend/ActionLifecycleBackend.h"

namespace dualpad::input::backend
{
    // Reserved primary native-PC-event backend. The main ButtonEvent route will
    // land here, while keeping the lifecycle interface shared with other
    // backends such as keyboard-native helper output.
    class ButtonEventBackend final : public IActionLifecycleBackend
    {
    public:
        static ButtonEventBackend& GetSingleton();

        void Reset() override;
        bool IsRouteActive() const override;
        bool CanHandleAction(std::string_view actionId) const override;
        bool TriggerAction(
            std::string_view actionId,
            ActionOutputContract contract,
            InputContext context) override;
        bool SubmitActionState(
            std::string_view actionId,
            ActionOutputContract contract,
            bool pressed,
            float heldSeconds,
            InputContext context) override;

    private:
        ButtonEventBackend() = default;
    };
}
