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

### Lifecycle-driven native backend

The long-term owner for Skyrim-native digital controls is now the
lifecycle-driven native injection path.

This backend should consume already-normalized action lifecycles such as:

- press -> release
- press -> hold -> release
- minimum-down-window pulse
- toggle plus debounce

It should then emit the matching native event family:

- `ButtonEvent` for discrete controls
- `ThumbstickEvent` or native axis state for analog controls
- special native event helpers only when a control genuinely requires them

Important rule:

- not every native control should be forced through the same `ButtonEvent`
  helper

### Keyboard-native backend

The keyboard-native backend is no longer the planned owner for ordinary
Skyrim-native gameplay controls.

Its bounded long-term role is:

- Mod virtual keys
- compatibility experiments
- research around true PC-keyboard provenance for external mods

Practical product rule:

- do not make core Skyrim gameplay controls depend on `dinput8.dll` proxy
  deployment unless a mod-facing virtual-key contract truly needs it

Reason:

- requiring an extra `dinput8.dll` is not clean enough for the main gameplay
  path when a native engine-owned input route is available

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
- lifecycle-driven native backend emits Skyrim-native discrete controls
- keyboard-native backend emits only bounded mod/provenance keys when needed

So it is valid for one frame to contain:

- gamepad analog movement/look
- native discrete gameplay/menu actions
- keyboard-native mod virtual keys when explicitly enabled

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
- Skyrim-native digital actions move to lifecycle-driven native injection
- keyboard-native stays available for mod-facing virtual keys and bounded
  provenance work

This reduces dependence on both Skyrim's gamepad controlmap ambiguity and the
older keyboard provenance experiments for ordinary gameplay behavior.

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
- discrete Skyrim controls go to lifecycle-driven native injection
- plugin actions stay separate
- mod events stay separate

## Immediate Implementation Guidance

Near-term implementation should assume:

- Route B remains the current bridge for analog gamepad-state injection
- the next digital expansion target is the producer-side native button path
  using engine-owned queue/cache helpers
- lifecycle policy should be decided before backend emission
- keyboard-native work should stay focused on mod virtual keys and bounded
  provenance experiments

Do not broaden the gamepad backend again just because Route B is working, and
do not put new Skyrim-native gameplay ownership back onto the keyboard route.

## Alternative Route Summary

Recent reverse-engineering results changed the practical near-term split:

- `Route B` remains the production-ready path for analog gameplay control feel
- producer-side native button injection is now a viable digital research line
  after engine-cache `Jump` proof
- `KeyboardNativeBackend` remains useful, but as a bounded mod/provenance line
  rather than a shipping dependency for core gameplay
- if a feature only needs to execute the native function/semantic that a PC
  event normally triggers, a direct helper/handler route is acceptable

Current route matrix:

- `Route B / NativeStateBackend`
  - production path
  - owns analog stick / trigger semantics and any action that truly depends on
    native controller-state lifecycle
- lifecycle-driven native button backend
  - preferred destination for discrete Skyrim-native actions such as `Jump`,
    `Activate`, `Sprint`, `Sneak`, `Shout`, and menu buttons
  - must be implemented with explicit lifecycle policy such as `HoldOwner`,
    `MinDownWindow`, or `Toggle`, not as blind same-poll pulses
- `KeyboardNativeBackend`
  - keep for Mod virtual keys, reverse-engineering, and bounded provenance
    experiments
  - do not make new core gameplay features depend on it
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
  - worthwhile for mod-facing virtual-key research
  - still has compatibility, deployment, and maintenance risk
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

2026-03-12 update:

- `engine-cache` native button retry has now proven that producer-side native
  `ButtonEvent` injection can be consumed semantically by the game for `Jump`
- the remaining `Jump` issue is no longer best explained as pulse timing alone;
  current reverse evidence points to the gameplay-root broad allow gate
  (`sub_140706AF0`) being blocked on first press by a transient `FaderMenu`
  state bit, so native button injection now needs gate-aware lifecycle staging
- the first implementation step of that staging is now in-tree:
  gameplay pulse actions enter a logical pulse queue first, and lifecycle
  actions can now declare explicit policy such as `MinDownWindow`
- `Activate` is the first action moved onto explicit lifecycle policy with a
  40 ms minimum-down window; other lifecycle actions still retain their
  previous semantics until migrated deliberately
- this shifts the main digital-control plan away from keyboard-native ownership
  and toward lifecycle-driven native injection
- `KeyboardNativeBackend` remains useful, but primarily for Mod virtual keys
  and provenance-sensitive integrations

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

## 2026-03-12 Producer-Driven Native Button Update

Recent IDA/MCP work tightened the native-button conclusion again.

The key observation is that Skyrim's native gamepad `ButtonEvent` production is
already producer-driven inside the device poll path:

- `BSWin32GamepadDevice::Poll` at `0x140C1AB40` reads current state
- it compares previous/current digital state inside the device object
- digital button changes are dispatched through `sub_140C190F0`
- axis/trigger changes are dispatched through `sub_140C19220`
- those producer helpers finally materialize engine-owned button events through
  `sub_140C16900`

This is better than the current `Poll + 0x53` engine-cache retry because it
keeps Skyrim's own producer-side state accumulation, diff ordering, and native
button semantics intact.

Therefore, if we stay on the native `ButtonEvent` line, the preferred forward
shape is no longer:

- lifecycle plan -> manual queue/cache `ButtonEvent` write near `ControlMap`

It is now:

- lifecycle plan -> virtual current gamepad state -> native gamepad `Poll`
  producer helpers -> engine-owned `ButtonEvent`

Practical conclusion:

- the correct "excellent" version of the native button path is
  producer-driven, not consumer-spliced
- `engine-cache` remains valuable as proof, reverse tooling, and fallback
  experimentation only
- future digital generalization should target the shared producer path used by
  `sub_140C190F0` / `sub_140C19220`

### More-Upstream Plan B

If the current Route B `XInputGetState` call-site patch proves too narrow, the
next more-upstream candidate should still stay above `ControlMap` and still
reuse the producer-side button path.

Current reverse points to two better Plan B candidates:

- pre-diff current-state overlay
  - patch or populate the gamepad device's current-frame state buffer before
    the producer compare step runs
  - let the existing previous/current diff logic in `BSWin32GamepadDevice::Poll`
    call `sub_140C190F0` / `sub_140C19220` itself
- raw-provider hook below the XInput call-site
  - investigate the shared raw-state provider path around `sub_140C1B600`
  - this would be earlier than the current internal `XInputGetState` patch, but
    still feeds the same producer-side button helpers afterward

Additional reverse note:

- there appear to be at least two native producer-side gamepad poll paths on
  SE 1.5.97:
  - `BSWin32GamepadDevice::Poll` around `0x140C1AB40`
  - `BSPCOrbisGamepadDevice::Poll`-style path around `0x140C1BD20`
- both converge on the same producer helpers
  `sub_140C190F0` / `sub_140C19220`
- for DualSense-specific work, the Orbis/raw-provider side is therefore a valid
  more-upstream Plan B, not just an unrelated side path

### More-Upstream Plan C

There is now enough evidence for a third native-button candidate that sits even
earlier than the raw-state read boundary:

- the provider slot / handle registration layer around `unk_142F6C800`

Current reverse markers:

- `sub_140C1CD70` allocates or reuses a provider slot inside the 9144-byte
  provider table and assigns an internal handle-like ID
- `sub_140C1CFC0` can repopulate one provider slot with richer state and
  device metadata, then re-arm it
- `sub_140C1D640` sends initialization/configuration reports for that slot
- `sub_140C1FA00` writes HID output reports to the provider-owned device handle
- `sub_140C1B280` later drains rich per-frame records from the same provider
  slot into the Orbis-side poll path

This suggests a distinct Plan C:

- do not just patch reads from an existing provider
- instead, investigate whether DualPad can own or mirror one native provider
  slot and feed synthetic state into the same provider-managed ring/state block

Why this is interesting:

- it is still upstream of `ControlMap`
- it still converges on the same native producer helpers
- it is earlier than the current `XInputGetState` call-site patch
- it may fit DualSense-specific work better than pretending to be plain XInput

Current caution:

- this is a reverse-backed candidate, not a proven implementation path yet
- it likely has higher complexity than Plan B because it touches slot lifetime,
  provider handles, and HID-side initialization/state ownership

Important rule:

- do not fall back to new `ControlMap`-side queue surgery just because the
  current `engine-cache` retry proved semantic consumption
- if we need a broader native-button route, move earlier into the producer
  state path, not later into the consumer path

- if the proxy consumer is absent, `KeyboardNativeBackend` may still fall back
  to the old in-plugin staging path during transition

The final coexistence conclusion is summarized in:

- [keyboard_native_coexistence_summary_zh.md](/c:/Users/xuany/Documents/dualPad/docs/keyboard_native_coexistence_summary_zh.md)
