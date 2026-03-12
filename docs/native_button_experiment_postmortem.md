# Native Button Experiment Postmortem

## Status

Do not continue the current consumer-side native button experiment as the main implementation.

Keep it only as:

- reverse-engineering notes
- crash reproduction material
- SE 1.5.97 layout verification

Default runtime behavior must stay on the stable XInput compatibility fallback until the upstream native-state backend exists.

## What We Tried

These routes were all tested and should be treated as historical, not as the forward plan:

- arbitrary `BSInputEventQueue` writes from plugin code
- `BSTEventSource<InputEvent*>::PrependEventSink` mutation
- `PlayerControls::ProcessEvent(InputEvent*)` detour
- `MenuControls::ProcessEvent(InputEvent*)` detour
- full `BSInputDeviceManager::PollInputDevices(float)` detour
- `PollInputDevices + 0x4C` `mov rdx, [rdx+380h]` patch
- `PollInputDevices + 0x53` call-site patch
- `drop`, `append-probe`, `append`, `head-prepend`, and `inject` runtime modes
- heap `ButtonEvent` creation
- queue-cache-backed `ButtonEvent` reuse

## What We Confirmed

### Mapping-layer snapshot boundary

The mapping layer currently produces one complete `PadEventBuffer` per HID state delta:

- `PadEventGenerator::Generate()` clears the buffer first
- then emits button events
- then axis events
- then tap/hold events
- then combo events
- then touchpad events

This is deterministic and complete for a single `previous PadState -> current PadState` transition.

It is not a true game-frame atomic list yet:

- snapshots are generated on the HID side
- snapshots are later drained on the main thread
- multiple HID snapshots may still be coalesced before final game injection

So the correct statement is:

- current mapping output is HID-snapshot atomic
- current mapping output is not guaranteed to be game-frame atomic

### ControlMap-side injection findings

From logs and code experiments we confirmed:

- the `head` argument passed into the ControlMap consumer is the real chain being consumed for that call
- `queueHead` is not the whole story
- consumer-side queue/head mutation is extremely sensitive to runtime layout, ownership, and timing
- even after fixing SE 1.5.97 offsets and strides, consumer-side splicing still does not reproduce gameplay/menu semantics reliably

### SE-only layout hazard

One crash root cause was a bad assumption inherited from multi-runtime/CommonLib layout views:

- for this SE 1.5.97-only work, raw queue access had to be corrected to explicit SE offsets
- button cache start and stride needed SE-specific handling

That fixed crashes, but it did not solve the semantic problem.

## Why This Path Is Not The Final Architecture

### It is too late in the pipeline

The current experiment tries to splice `ButtonEvent` objects near or immediately before `ControlMap` consumption.

That is already downstream of the place where Skyrim normally builds and owns pad input semantics.

As a result, we keep fighting hidden engine behavior instead of reusing it.

### Stateful semantics are not trustworthy

The observed product issues were consistent:

- sprint could still interrupt during long hold
- menu direction/page actions did not preserve native hold-repeat behavior
- fast input behavior depended on queue/head mutation details
- source buttons could be blocked while no backend fully owned the resulting action

This is the core architectural failure.

### Mixed per-action backends create dead zones

The current mixed approach lets one physical source button move between:

- native experimental routing
- compatibility state
- compatibility pulse
- plugin actions

That makes blocking and ownership fragile for stateful actions.

For final behavior, one stateful action class must have one owning backend for the entire lifecycle.

## Hard Rules Going Forward

Do not resume these as the main design:

- consumer-side `ButtonEvent` splicing before `ControlMap`
- queue singleton head/tail surgery as the primary gameplay path
- per-action native/compatibility mixing for stateful actions
- new production features built on `use_native_button_injector`

The current `use_native_button_injector` path is now legacy experimental-only.

## Scoped Retry Conditions

As of 2026-03-12, a narrow retry of native `ButtonEvent` injection is allowed,
but only under strict reverse-first conditions.

This retry does not change the architectural conclusion above.

It exists only to answer a smaller question:

- whether there is still one safe, engine-owned injection boundary where a
  minimal `ButtonEvent` experiment can be observed and evaluated correctly

The retry must follow these rules:

- do not resume arbitrary `BSInputEventQueue` writes from unrelated plugin code
- do not treat queue/head surgery as the default forward plan
- do not widen the old `append` / `head-prepend` / `inject` modes into
  production routing
- do not use stateful gameplay actions such as `Sprint` as the first proof case
- do not interpret "node appeared in the chain" as success unless the event is
  also consumed semantically by the game

The retry order is:

1. Use IDA/MCP to recover the real `PollInputDevices -> ControlMap` call path,
   ownership, and queue reset timing.
2. Select at most one or two candidate injection points with concrete evidence.
3. Add observe-only probes first.
4. Add the smallest possible `ButtonEvent` prepend/append experiment only after
   the hook boundary is understood.
5. Validate with simple actions first, such as menu confirm/cancel or a basic
   gameplay pulse, before revisiting held actions.

The retry should be considered failed again if any of the following remains
true after the minimal experiment:

- the hook point is not ownership-safe
- the event can be logged on the chain but not consumed semantically
- behavior still depends on fragile queue/head mutation details
- stateful semantics still require mixed backend ownership

## 2026-03-12 Producer-side Retry Update

The narrow retry has now produced one useful success:

- producer-side `engine-cache` button injection can be consumed semantically by
  Skyrim for `Jump`

This does not resurrect the old consumer-side design.

It changes the retry conclusion in a narrower way:

- heap-created or consumer-spliced `ButtonEvent` chains are still not the
  forward plan
- engine-owned queue/cache production is now a valid digital-input research
  path
- the remaining problem has moved from "can a native button event work" to
  lifecycle timing, gameplay-gate awareness, and ownership policy such as
  delayed release, held-owner, minimum-down-window behavior, and broad-gate
  deferral when gameplay-root allow conditions are transiently closed

Latest implementation note:

- native pulse actions are no longer best modeled as "stage press now, hope the
  gate is open"; they should first enter a logical pulse queue, and only be
  materialized into engine-owned `ButtonEvent` writes when the gameplay broad
  gate allows it
- lifecycle actions above that queue should also stop being implicit
  "source-down equals game-down" behavior; the first explicit policy now in tree
  is `Activate -> MinDownWindow(40 ms)`

Current architectural direction after this proof:

- keep `Route B` for analog/native pad-state ownership
- move Skyrim-native discrete controls toward lifecycle-driven native injection
- keep `KeyboardNativeBackend` mainly for Mod virtual keys and bounded
  provenance experiments

## 2026-03-12 Producer-Facade Update

Further IDA/MCP work shows that the native gamepad button path is already
producer-driven inside Skyrim's own device poll code.

Relevant chain:

- `BSWin32GamepadDevice::Poll` at `0x140C1AB40`
- device-local previous/current state comparison
- digital producer helper `sub_140C190F0`
- axis/trigger producer helper `sub_140C19220`
- engine-owned queue/cache materialization through `sub_140C16900`

This changes the best retry conclusion:

- the strongest native-button direction is no longer "write engine-cache
  `ButtonEvent` objects near `ControlMap`"
- the stronger direction is "feed a synthetic current device state early enough
  that Skyrim's own producer helpers generate the `ButtonEvent`s"

In other words:

- `engine-cache` queue writes proved semantic viability
- but producer-side state diffing now looks like the cleaner and more
  generalizable native-button route

This remains on the `ButtonEvent` line, but it moves the ownership boundary
earlier.

### More-Upstream Plan B Candidates

If the current Route B `XInputGetState` call-site patch is still too narrow,
the next better candidates are:

- patch the gamepad device's current-frame state buffer before the native
  previous/current compare step
- investigate the raw-provider boundary around `sub_140C1B600`, which appears
  to populate a larger gamepad state block before the same producer helpers are
  reached

These are both preferable to new consumer-side `ControlMap` queue surgery,
because they preserve the native producer ordering and state accumulation that
`Jump`/`Sprint`/`Activate`/`Sneak` ultimately need.

Latest reverse detail:

- there are at least two producer-side gamepad poll variants that both feed the
  same native button helpers:
  - `BSWin32GamepadDevice::Poll` around `0x140C1AB40`
  - a `BSPCOrbisGamepadDevice`-style poll path around `0x140C1BD20`
- the lower-level raw provider around `sub_140C1B600` appears to participate in
  selecting or populating the richer Orbis-side device state
- for DualSense-specific work, this makes the Orbis/raw-provider boundary a
  concrete more-upstream Plan B, not just a theoretical alternative to the
  current XInput call-site patch

### More-Upstream Plan C Candidate

Further reverse work exposes a third boundary above the raw-state read itself:

- the provider slot / handle registration layer around `unk_142F6C800`

Relevant evidence:

- `sub_140C1CD70` allocates or reuses one provider slot and assigns an
  internal handle-like ID
- `sub_140C1CFC0` can rebuild one slot with richer metadata/state and then
  re-arm it
- `sub_140C1D640` sends per-slot initialization/configuration reports
- `sub_140C1FA00` writes HID output reports to the provider-owned device handle
- `sub_140C1B280` later drains rich records from that same slot into the
  Orbis-side producer path

This suggests a true Plan C:

- instead of only patching the current read boundary,
- investigate whether DualPad can own, mirror, or populate one native provider
  slot directly and let the existing provider -> Orbis poll -> producer-helper
  chain continue naturally

Status:

- promising but unproven
- likely more invasive than Plan B
- still preferable to new `ControlMap`-side queue surgery if a broader native
  digital route is required

## What Stays Useful

These parts are still valuable and should be preserved:

- HID reader and protocol layer
- `PadEventGenerator` event ordering
- `SyntheticStateReducer` and frame-level button lifecycle reduction
- context-aware binding resolution
- plugin/custom action execution path
- the stable XInput compatibility fallback
- reverse-engineering notes around `PollInputDevices` and the ControlMap call-site

## Recommended Replacement Direction

Replace the current consumer-side experiment with an upstream native-state backend:

- keep context-aware action planning above the backend
- maintain a per-frame `VirtualGamepadState` inside the plugin
- hook earlier, before Skyrim generates or finalizes its own pad input events
- let Skyrim derive long-hold, repeat, and menu semantics from a native-style gamepad state path
- keep plugin actions and mod events on separate backends
- keep XInput compatibility only as global fallback, not as a mixed per-action strategy
