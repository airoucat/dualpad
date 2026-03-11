#pragma once

#include "input/injection/SyntheticPadFrame.h"

#include <cstdint>

namespace dualpad::input
{
    class IGameInputInjector
    {
    public:
        virtual ~IGameInputInjector() = default;

        virtual void Reset() = 0;
        virtual void SubmitFrame(const SyntheticPadFrame& frame, std::uint32_t handledButtons) = 0;
    };
}
