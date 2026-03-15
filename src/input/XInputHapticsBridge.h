#pragma once

namespace dualpad::input
{
    // Installs the XInputSetState import hook used to bridge Skyrim vibration
    // requests into the native DualSense haptics path.
    bool InstallXInputHapticsBridge();
}
