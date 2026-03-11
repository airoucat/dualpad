#pragma once

#include "input/backend/BackendInterfaces.h"

namespace dualpad::input::backend
{
    class NativeStateBackend final : public INativeStateBackend
    {
    public:
        void Reset() override;
        void BeginFrame(InputContext context) override;
        void ApplyPlan(const FrameActionPlan& plan) override;
        const VirtualGamepadState& GetState() const override { return _state; }

        [[nodiscard]] InputContext GetLastContext() const { return _lastContext; }

    private:
        VirtualGamepadState _state{};
        InputContext _lastContext{ InputContext::Gameplay };
    };
}
