#pragma once

#include "input/backend/FrameActionPlan.h"
#include "input/backend/VirtualGamepadState.h"

namespace dualpad::input::backend
{
    class INativeStateBackend
    {
    public:
        virtual ~INativeStateBackend() = default;

        virtual void Reset() = 0;
        virtual void BeginFrame(InputContext context) = 0;
        virtual void ApplyPlan(const FrameActionPlan& plan) = 0;
        virtual const VirtualGamepadState& GetState() const = 0;
    };

    class IPluginActionBackend
    {
    public:
        virtual ~IPluginActionBackend() = default;

        virtual void Reset() = 0;
        virtual void Dispatch(const FrameActionPlan& plan) = 0;
    };

    class IModEventBackend
    {
    public:
        virtual ~IModEventBackend() = default;

        virtual void Reset() = 0;
        virtual void Dispatch(const FrameActionPlan& plan) = 0;
    };
}
