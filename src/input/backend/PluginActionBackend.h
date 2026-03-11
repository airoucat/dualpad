#pragma once

#include "input/backend/BackendInterfaces.h"

namespace dualpad::input::backend
{
    class PluginActionBackend final : public IPluginActionBackend
    {
    public:
        void Reset() override {}
        void Dispatch(const FrameActionPlan& plan) override;
    };
}
