#pragma once

namespace dualpad::input
{
    constexpr bool ApplyNativeKbmSemanticPolicy(
        bool remapMode,
        bool& ignoreKeyboardMouse) noexcept
    {
        if (ignoreKeyboardMouse == remapMode) {
            return false;
        }
        ignoreKeyboardMouse = remapMode;
        return true;
    }
}
