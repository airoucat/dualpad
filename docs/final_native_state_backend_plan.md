# Final Native-State Backend Plan

## Goal

Reach a stable architecture where:

- one physical key can map to different actions in different contexts
- stateful actions do not lose hold/repeat semantics
- plugin actions and mod events stay independent from Skyrim control routing
- the injector consumes one complete snapshot update at a time
- fallback compatibility remains available without contaminating the main design

## Current Runtime Bridge

Until the planner-owned upstream native-state backend exists, the official runtime bridge is:

- `BSWin32GamepadDevice::Poll` internal `XInputGetState` call-site patch on SE 1.5.97
- fixed byte verification before patching
- compatibility IAT path retained only as fallback

This bridge is good enough to keep as the current shipping route, but it does not replace the long-term backend split described below.

## Current Snapshot Assessment

### What is already correct

`PadEventGenerator::Generate()` currently emits a complete event list for one HID snapshot transition in a deterministic order:

1. button press/release
2. axis change
3. tap/hold
4. combo
5. touchpad press/slide/release

`TouchpadMapper` and `ComboEvaluator` append into the same `PadEventBuffer`, so the injector receives one complete logical list for that HID sample.

### What is not correct yet

This should not be called a game-frame atomic snapshot yet.

Today it is:

- one atomic event list per HID state transition
- handed off to the injector as one `PadEventSnapshot`

But it is not yet:

- exactly one list per game frame
- guaranteed to be consumed without coalescing before game injection

Therefore the next architecture must treat `PadEventSnapshot` as the authoritative HID-sample unit, then do deterministic main-thread planning from there.

## Final Architecture

### 1. Snapshot ingress

- HID thread reads `PadState`
- mapping layer produces one `PadEventSnapshot`
- snapshot handoff to the main thread stays single-owner and fixed-buffer

### 2. Main-thread state reduction

- `SyntheticStateReducer` remains the place that derives:
  - down
  - pressed
  - released
  - held
  - tap
  - combo
  - axis state
- this reduction step owns lifecycle truth

### 3. Context-aware action planning

Add a planner layer above all output backends:

- input: `PadEventSnapshot` + reduced synthetic frame + current `InputContext`
- output: one `FrameActionPlan`

The planner is where:

- context-specific action choice happens
- forbidden combinations are rejected
- tap/hold/combo semantics are normalized
- source-button blocking is decided only after ownership is known

Initial code skeleton for this layer now exists under `src/input/backend/`:

- `FrameActionPlan.h`
- `ActionBackendPolicy.*`
- `FrameActionPlanner.*`

### 4. Backend split

Use three final backends plus one fallback backend:

- `NativeStateBackend`
  - owns Skyrim control actions that need true pad-state semantics
  - uses an internal `VirtualGamepadState`
  - near-term target scope should stay narrow:
    - left stick
    - right stick
    - left trigger
    - right trigger
  - do not expand it into the default home for most digital actions
- `KeyboardNativeBackend`
  - owns most digital control actions
  - is an implementation tool backend, not a public-facing identity change for the mod
  - should handle non-text control keys only
  - must not intentionally produce visible character input in text-entry UI
- `PluginActionBackend`
  - owns plugin-local actions such as screenshot-style features
- `ModEventBackend`
  - owns reserved mod-facing virtual key or event publication
  - implementation can stay stubbed for now
- `CompatibilityFallbackBackend`
  - stays available only when the native-state path is unavailable
  - not mixed ad hoc with the main stateful path

### 5. Upstream hook requirement

The final native path must hook before Skyrim finalizes or consumes pad input semantics.

Do not use these as the final hook site:

- `PrependEventSink`
- `PlayerControls::ProcessEvent`
- `MenuControls::ProcessEvent`
- `ControlMap` consumer-side chain splicing

The correct target is an upstream gamepad/raw-state generation boundary where the plugin can provide a native-style virtual state before engine semantics are derived.

## Routing Rules

### Stateful gameplay and menu actions

These must not bounce between backends mid-lifecycle:

- sprint
- movement
- look
- menu up/down/left/right
- menu page up/down
- any action that relies on hold/repeat behavior

Once planned, they belong to one backend for the full lifecycle.

### Mixed device output

Mixed device output is acceptable when ownership is clear.

Expected final shape:

- gamepad backend for analog state
- keyboard-native backend for most digital controls

This means one frame may legitimately contain both:

- gamepad analog movement/look/trigger input
- keyboard-native digital actions

That is allowed.

What is not allowed is a single stateful action bouncing between those backends during one hold lifecycle.

### Plugin actions

These stay fully event-driven and bypass game-control injection:

- screenshot
- multi-screenshot
- future plugin-local utilities

### Mod events

These stay separate from game controls and should never require the gameplay backend to fake ownership.

### Text input

Text input is out of scope for the current architecture phase.

Do not add a text-input backend right now.

Hard rule:

- controller buttons must not accidentally type text into visible text-entry UI

If the game is in a text-entry context, keyboard-native character-producing routes should be suppressed rather than guessed.

## Mod Virtual-Key Pool

Local resource verification currently confirms these names in `keyboard_english.txt`:

- `F13`
- `F14`
- `F15`
- `DIK_KANA`
- `DIK_ABNT_C1`
- `DIK_CONVERT`
- `DIK_NOCONVERT`
- `DIK_ABNT_C2`
- `NumPadEqual`
- `PrintSrc`
- `L-Windows`
- `R-Windows`
- `Apps`
- `Sleep`
- `Wake`
- `WebSearch`
- `WebFavorites`
- `WebRefresh`
- `WebStop`
- `WebForward`
- `WebBack`
- `Mail`
- `MediaSelect`

Do not assume `F16`-`F20` are available locally until they are verified in the actual resource tables used by this build.

Final mod-key selection still needs one last validation pass against the shipped `controlmap.txt` codes before release.

## Migration Plan

### Phase 1

- freeze the current consumer-side native button experiment
- keep it disabled by default
- document the dead end and preserve only the reverse-engineering notes

### Phase 2

- introduce a standalone `FrameActionPlan`
- split native-state, plugin, and mod backends
- stop routing stateful actions directly from the current mixed dispatcher
- run the planner in shadow mode first, without changing stable runtime behavior

### Phase 3

- implement `VirtualGamepadState`
- move stateful gameplay/menu ownership to the native-state backend
- keep compatibility as whole-backend fallback only

### Phase 4

- reverse and hook the upstream gamepad state boundary
- feed the virtual state there instead of splicing `ButtonEvent` objects near `ControlMap`

### Phase 5

- shrink `IATHook` and compatibility injection to fallback support
- keep native-state as the primary gameplay/menu path

## Non-Negotiable Rules

- No new production features on top of the current `use_native_button_injector` experiment.
- No consumer-side queue/head mutation as the final design.
- No per-action mixed backend ownership for stateful controls.
- No source-button blocking before the owning backend has committed to the action lifecycle.
