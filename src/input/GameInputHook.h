#pragma once

namespace dualpad::input
{
    void InstallGameInputHook();
    // void InjectNativeGamepadEvent(TriggerCode code, TriggerPhase phase);
    void InstallNativeSubmitter();
}