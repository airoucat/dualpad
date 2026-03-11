#pragma once

#include "input/backend/BackendInterfaces.h"

namespace dualpad::input::backend
{
    class ModEventBackend final : public IModEventBackend
    {
    public:
        void Reset() override {}
        void Dispatch(const FrameActionPlan& plan) override;
    };
}
