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
