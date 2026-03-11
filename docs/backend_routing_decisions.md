# Backend Routing Decisions

This document records the design decisions that came out of the recent Route B discussion and comparison against Steam Input.

It is intentionally product-oriented:

- what the mod should do
- what we do not want it to do
- which backend owns which class of action

It is not a reverse-engineering notebook.

## Current Runtime Conclusion

The current Route B bridge is:

- patch the internal `XInputGetState` call inside `BSWin32GamepadDevice::Poll`
- fill one synthetic `XINPUT_STATE`
- let Skyrim derive its own native gamepad semantics from that state

This means the current bridge is still a virtual gamepad-state path, not direct `ButtonEvent` construction.

## Product Identity

DualPad remains a controller mod from the player's point of view.

Keyboard-native injection is an internal tool backend, not the primary public-facing interaction model.

The player should not feel like the mod is "typing on the keyboard" during normal play.

## Confirmed Planning Rules

The planner stays above the backend split.

That means all of these still remain valid:

- one physical key can map to different actions in different contexts
- combo bindings remain available
- tap/hold/combo normalization happens before backend routing
- backend choice does not remove context freedom

The backend only answers:

- how the final action is emitted
- which native device semantics should own the lifecycle

## Final Backend Ownership Direction

### Gamepad backend

The gamepad backend should be kept narrow.

For now, treat it as the backend for analog/native pad-state controls only:

- left stick
- right stick
- left trigger
- right trigger

This backend exists to preserve:

- movement analog range
- look analog range
- trigger analog range
- native controller deadzone and sampling behavior

Do not treat it as the long-term home for most digital actions.

### Keyboard-native backend

The keyboard-native backend is still the intended long-term owner for most
digital control actions.

This includes the actions that are easier, more stable, or more product-correct when expressed as native keyboard controls instead of virtual gamepad buttons.

Examples:

- sprint
- activate
- jump
- hotkeys
- menu confirm/cancel if needed
- menu page actions if routed as keyboard-native controls
- plugin-facing helper bindings that should behave like keyboard controls

But the current implementation is not production-ready yet.

Near-term runtime rule:

- keep keyboard-native code available for reverse-engineering and future work
- do not route production gameplay actions through it until Skyrim's internal
  keyboard semantic contract is fully verified

### Plugin backend

Keep plugin-local actions separate:

- screenshot
- multi-screenshot
- future plugin utilities

### Mod backend

Keep mod-facing events separate from both Skyrim controls and plugin-local controls.

## Mixed Output Is Acceptable

Yes, mixed device output is acceptable.

The intended shape is:

- gamepad backend emits analog stick/trigger state
- keyboard-native backend emits digital key controls

So it is valid for one frame to contain:

- gamepad analog movement/look
- keyboard-native digital actions

This is not a design problem by itself.

The real rule is narrower:

- one stateful action lifecycle must have exactly one owning backend

For example:

- `Sprint` must not bounce between gamepad and keyboard mid-hold
- `MenuDown` must not alternate between gamepad repeat and keyboard repeat during one hold

## Text Input Policy

Do not build a text-input backend right now.

The current product goal is control input, not text composition.

This leads to two hard rules:

- DualPad should not intentionally produce character input for text-entry UI right now
- pressing controller buttons while a text-entry field is active must not type visible characters by accident

Practical consequence:

- `KeyboardNativeBackend` is for control-style keys, not for text entry
- if Skyrim is in a text-entry context, keyboard-native character-producing routes should be disabled or suppressed

The mod should prefer "no action" over accidental text injection.

## Player Rebinding Policy

### Game settings changes

If a route depends on virtual gamepad state, Skyrim's own gamepad binding interpretation can affect the result.

If a route depends on keyboard-native controls, Skyrim's keyboard binding interpretation can affect the result.

Therefore the backend split should reduce ambiguity:

- analog gamepad semantics stay on the gamepad backend
- most digital actions move to keyboard-native backend

This reduces dependence on Skyrim's gamepad controlmap for digital gameplay behavior.

### Dynamic `ControlMap` reads

Dynamic `ControlMap` parsing is acceptable if it is cached.

Do not do a full `ControlMap` walk every frame.

Acceptable strategy:

- load once at startup
- refresh on settings/menu transitions that can change bindings
- use lookup tables at runtime

This should not introduce meaningful latency if implemented that way.

## Why This Direction Was Chosen

Recent discussion clarified three product facts:

- context freedom is mandatory
- accidental text entry is unacceptable
- analog controller feel is still worth preserving

The narrowest architecture that satisfies those constraints is:

- planner decides context/combo/tap/hold first
- analog actions go to `GamepadNativeBackend`
- digital control actions go to `KeyboardNativeBackend`
- plugin actions stay separate
- mod events stay separate

## Immediate Implementation Guidance

Near-term implementation should assume:

- Route B remains the current bridge for gamepad-state injection
- that bridge is best suited to analog controls first
- the first keyboard-native route should be a call-site hook inside `BSWin32KeyboardDevice::Poll`
- that keyboard route should rebuild `BSWin32KeyboardDevice::curState` from physical state plus DualPad-owned keys inside the same call-site hook
- keyboard-native work should stay focused on non-text control injection

Do not broaden the gamepad backend again just because Route B is working.

The next expansion target should stay on the dedicated keyboard-native control backend, not more digital button ownership inside the gamepad backend.

## Alternative Route Summary

Recent reverse-engineering results changed the practical near-term split:

- `Route B` remains the only production-ready path for native gameplay control feel
- `KeyboardNativeBackend` remains useful, but as a bounded research line rather
  than a shipping dependency
- if a feature only needs to execute the native function/semantic that a PC
  event normally triggers, a direct helper/handler route is acceptable

Current route matrix:

- `Route B / NativeStateBackend`
  - production path
  - owns analog stick / trigger semantics and any action that truly depends on
    native controller-state lifecycle
- `KeyboardNativeBackend`
  - keep for reverse-engineering and future experiments
  - do not make new product features depend on it until the current
    `GetDeviceData` provenance gap is solved
- `PluginActionBackend`
  - preferred for screenshot, quicksave, quickload, wait, and similar plugin
    utility behavior
- `ModEventBackend`
  - preferred when DualPad controls both the sender and the receiving mod-side
    contract
- future `DirectSemanticBackend`
  - suitable for actions where we can directly call game helpers or reuse
    handler/camera semantics instead of fabricating keyboard identity

Routes that are still valid to evaluate but should not be treated as the main
product plan:

- `SendInput`
  - lightweight user-mode proof-of-concept only
  - not trusted as a final answer for Skyrim's keyboard provenance
- `dinput8.dll` / DirectInput proxy
  - worthwhile as a more-upstream research step
  - still has compatibility and maintenance risk
- virtual HID keyboard
  - most likely way to obtain true PC-keyboard identity for other mods
  - significantly higher engineering and deployment cost

Relevant reusable mod patterns:

- `TrueDirectionalMovement` and `BFCO` are useful as examples of
  helper/handler-driven semantics, not as examples of queue-based synthetic
  input production
- `MCO/DMCO` behavior assets are useful as a combat semantic dictionary, not as
  an input backend implementation

## Keyboard-Native Update

Recent `dinput8` proxy experiments changed the practical implementation point
for keyboard-native controls:

- old in-plugin SKSE keyboard worker hooks are no longer the preferred formal
  route
- the preferred route is now the keyboard `IDirectInputDevice8A::GetDeviceData`
  return boundary inside the separate `dinput8.dll` proxy

What is already proven:

- remapped native records succeed there
- deferred synthetic records succeed there
- pure synthetic `DIDEVICEOBJECTDATA` generated from scratch also succeeds there

Therefore the shipping-oriented keyboard-native plan is now:

- keep `DualPad.dll` as the action planner / producer
- use a thin bridge into the `dinput8.dll` proxy
- let the proxy emit keyboard-native `DIDEVICEOBJECTDATA` records for the game

Current unresolved constraint:

- gameplay `Jump` still fails whenever Skyrim is running in gamepad-enabled
  mode, even for a real physical `Space` key
- therefore `dinput8`-side keyboard-native emission is proven early enough, but
  is not yet sufficient to coexist with gameplay under the game's active gamepad
  family

Fallback policy remains unchanged:

- if the proxy consumer is absent, `KeyboardNativeBackend` may still fall back
  to the old in-plugin staging path during transition

The final coexistence conclusion is summarized in:

- [keyboard_native_coexistence_summary_zh.md](/c:/Users/xuany/Documents/dualPad/docs/keyboard_native_coexistence_summary_zh.md)
