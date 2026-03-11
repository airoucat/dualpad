# AutoInputSwitch 1.1.2 (SE 1.5.97) Takeaways

Source reviewed:
- [Hooks.cpp](g:/skyrim_mod/AutoInputSwitch-1.1.2/AutoInputSwitch-1.1.2/src/Hooks.cpp)
- [InputEventHandler.cpp](g:/skyrim_mod/AutoInputSwitch-1.1.2/AutoInputSwitch-1.1.2/src/InputEventHandler.cpp)
- [Extensions.cpp](g:/skyrim_mod/AutoInputSwitch-1.1.2/AutoInputSwitch-1.1.2/src/Extensions.cpp)
- [main.cpp](g:/skyrim_mod/AutoInputSwitch-1.1.2/AutoInputSwitch-1.1.2/src/main.cpp)

## What It Actually Solves

AutoInputSwitch is not a native keyboard/gamepad injection implementation.

It solves a different layer:
- detecting whether the player is currently using keyboard/mouse or gamepad
- forcing Skyrim to answer "using gamepad" or "not using gamepad" consistently
- refreshing menu platform state when the active input family changes
- keeping remap mode usable by delegating `BSPCGamepadDeviceHandler::IsGamepadDeviceEnabled`

Core mechanisms:
- event sink on `BSInputDeviceManager` input events
- switch internal `_usingGamepad` on `kKeyboard / kMouse / kGamepad`
- hook game queries:
  - `BSInputDeviceManager::IsUsingGamepad`
  - `BSInputDeviceManager::GamepadControlsCursor`
- hook `BSPCGamepadDeviceHandler` enable check
- refresh open menus with `RefreshPlatform()`

## What Is Useful To DualPad

These parts are directly useful for the long-term hybrid backend plan:

- platform-state consistency
  - when DualPad mixes gamepad-native and tool/keyboard-style outputs, Skyrim UI must still know which platform to present
- remap-mode handling
  - their `IsGamepadDeviceEnabled` hook shows a practical pattern for not breaking player/menu remap mode
- menu refresh path
  - `menu->RefreshPlatform()` via UI task is a clean way to force icon/layout refresh after input-family changes
- mouse-look handoff pattern
  - their `ComputeMouseLookVector()` shows how they keep mouse-look semantics coherent during family switching
- input-family arbitration
  - the sink-based "last real device family wins" logic is a good reference if DualPad later exposes configurable auto-switch behavior

## What Is Not Useful For The Current Keyboard Investigation

AutoInputSwitch does not help with the current blocked reverse-engineering problem:

- it does not inject synthetic keyboard control records into Skyrim's downstream consumer chain
- it does not solve:
  - `BSWin32KeyboardDevice::Poll`
  - `sub_140C11600`
  - `sub_140C10860`
  - `sub_140C15E00`
  - handler-side filtering/validation
- it does not show how to make synthetic keyboard-origin control records survive gameplay dispatch

So it is not a shortcut for the current `KeyboardNativeBackend` research path.

## Practical Reuse Plan

Recommended reuse for DualPad later:

1. Add a small platform-arbitration layer above backends.
2. Use a hook equivalent to `IsUsingGamepad` / `GamepadControlsCursor` to keep Skyrim UI mode coherent.
3. Reuse the remap-mode idea in `BSPCGamepadDeviceHandler::IsGamepadDeviceEnabled`.
4. Keep this independent from the current keyboard-native dispatch investigation.

## Current Conclusion

AutoInputSwitch 1.1.2 is valuable as:
- a UI/platform-state reference
- a remap-mode compatibility reference
- a future hybrid-backend support reference

It is not valuable as:
- a direct answer to why synthetic keyboard records reach `sub_140C15E00` but still do not trigger gameplay/menu behavior
