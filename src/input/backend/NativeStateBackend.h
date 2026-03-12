#pragma once

#include "input/backend/BackendInterfaces.h"
#include "input/backend/NativeDigitalLifecycleCoordinator.h"

#include <array>

namespace dualpad::input::backend
{
    class NativeStateBackend final : public INativeStateBackend
    {
    public:
        void Reset() override;
        void BeginFrame(InputContext context) override;
        void ApplyPlan(const FrameActionPlan& plan) override;
        const VirtualGamepadState& GetState() const override { return _state; }
        const VirtualGamepadState& CommitPollState(std::uint64_t pollTimestampUs);
        void SetRawAnalogState(
            float moveX,
            float moveY,
            float lookX,
            float lookY,
            float leftTrigger,
            float rightTrigger);

        [[nodiscard]] InputContext GetLastContext() const { return _lastContext; }
        [[nodiscard]] const DigitalCommitFrame& GetLastCommitFrame() const { return _lastCommitFrame; }

    private:
        NativeDigitalLifecycleCoordinator _digitalCoordinator{};
        VirtualGamepadState _state{};
        DigitalCommitFrame _lastCommitFrame{};
        InputContext _lastContext{ InputContext::Gameplay };
        std::uint64_t _pollIndex{ 0 };
        std::uint32_t _packetNumber{ 0 };
    };
}
