#pragma once

#include <cstdint>

namespace dualpad::input
{
    // Installs the compatibility XInput hook used for fallback and upstream pass-through.
    bool InstallXInputIATHook();
    std::uint32_t FillSyntheticXInputState(void* pState);
    std::uint32_t CallOriginalXInputGetState(std::uint32_t userIndex, void* pState);
}
