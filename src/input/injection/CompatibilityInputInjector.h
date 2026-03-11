#pragma once

#include "input/injection/IGameInputInjector.h"

#include <cstdint>
#include <string_view>

namespace dualpad::input
{
    class CompatibilityInputInjector final : public IGameInputInjector
    {
    public:
        void Reset() override;
        void SubmitFrame(const SyntheticPadFrame& frame, std::uint32_t handledButtons) override;
        void PulseButton(std::uint32_t bit, std::string_view reason);
        void SetButtonState(std::uint32_t bit, bool down, std::string_view reason);

    private:
        std::uint32_t _virtualHeldDown{ 0 };
        std::uint32_t _submittedVirtualHeldDown{ 0 };
    };
}
