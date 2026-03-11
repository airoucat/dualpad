# Upstream Native-State Reverse Targets

## Purpose

This document starts where the old consumer-side experiment ends.

The planner/backend split is now in place locally, but the final native-state backend still needs one thing that cannot be derived from local source alone:

- a safe upstream hook where Skyrim still owns gamepad-state generation semantics

That hook is the next real reverse-engineering blocker.

## What Is Already Settled

These conclusions should be treated as fixed:

- do not continue `ControlMap`-side `ButtonEvent` splicing as the main route
- do not continue queue head/tail surgery near the ControlMap consumer as the final path
- keep the current `poll-xinput-call` upstream XInput route as the primary SE 1.5.97 runtime bridge
- keep the current XInput compatibility path as fallback only
- keep the new planner/backend split above the future hook site

Code that now exists locally:

- `src/input/backend/FrameActionPlan.h`
- `src/input/backend/ActionBackendPolicy.*`
- `src/input/backend/FrameActionPlanner.*`
- `src/input/backend/VirtualGamepadState.h`
- `src/input/backend/NativeStateBackend.*`

This means the next missing piece is not local architecture. It is the upstream game hook boundary.

## What We Need From Reverse Engineering

We need the point where Skyrim still has native ownership of gamepad-state semantics, before long-hold, repeat, menu-repeat, and control routing are already finalized.

The best candidate class of hook is:

- upstream of `ControlMap`
- upstream of `PlayerControls::ProcessEvent`
- upstream of `MenuControls::ProcessEvent`
- upstream of the already-dispatched `InputEvent` chain

The final desired behavior is:

- plugin builds one `VirtualGamepadState` per frame
- plugin writes or overrides native gamepad state at the right upstream boundary
- Skyrim itself generates `ButtonEvent` / `ThumbstickEvent` behavior from that state

## Questions To Answer In IDA/Ghidra

1. Where does Skyrim SE 1.5.97 gather the native gamepad raw state before `ButtonEvent` construction?
2. Which function owns the canonical per-frame gamepad state object or temporary structure?
3. Which function converts that raw state into button/stick/trigger semantics?
4. Which function boundary is safe to hook without rebuilding the downstream `InputEvent` chain by hand?
5. Can the plugin override a state structure instead of constructing `ButtonEvent` objects?
6. Where are menu repeat and gameplay hold semantics derived?
7. Does the game maintain a separate gamepad-device object with a poll/result buffer that is better to patch than `PollInputDevices`?

## Useful Facts To Carry Into Reversing

### Confirmed unstable or rejected points

These are already known bad or non-final:

- arbitrary `BSInputEventQueue` injection
- `BSTEventSource<InputEvent*>::PrependEventSink`
- `PlayerControls::ProcessEvent`
- `MenuControls::ProcessEvent`
- full `BSInputDeviceManager::PollInputDevices(float)` detour
- `Poll + 0x4C` `mov rdx, [rdx+380h]` patch
- consumer-side `Poll + 0x53` `call sub_140C11600` patch as a production strategy

### Useful relocation and runtime facts

- `RE::BSInputDeviceManager::PollInputDevices(float)` is `RELOCATION_ID(67315, 68617)`
- one observed SE 1.5.97 runtime example:
  - `ExeBase=0x7FF6E2020000`
  - `Poll=0x7FF6E2C350B0`
  - `Poll RVA=0xC150B0`
- earlier reverse work identified the ControlMap consumer call sequence around:
  - `Poll + 0x4C`
  - `Poll + 0x53`

### Confirmed lesson from the old path

Even after fixing SE-only queue layout and button-cache stride, consumer-side injection still failed semantically:

- sprint long-hold semantics were not trustworthy
- menu hold-repeat semantics were not trustworthy
- mixed native/compatibility ownership created dead zones

So the next reverse target must be upstream state ownership, not better queue surgery.

### Confirmed lesson from the upstream timing probes

Two upstream timing routes have now been settled:

- whole-`Poll` wrapper:
  - stable
  - semantically unacceptable
- `Poll` internal `XInputGetState` call-site patch:
  - current official SE 1.5.97 runtime route
  - good enough to keep as the bridge while the long-term backend split is built out

What this means for future reverse-engineering:

- do not spend more time reverse-engineering Route A or the Route B call-site itself
- the remaining blocker is the future planner-owned upstream native-state boundary beyond the current XInput bridge

## Recommended Prompt For GPT / Reverse Assistant

```text
I am reversing Skyrim SE 1.5.97 input handling for an SKSE plugin.

Context:
- I already abandoned consumer-side ButtonEvent splicing near ControlMap.
- I now have a local architecture that builds a per-frame VirtualGamepadState inside the plugin.
- I need the safest upstream hook where I can override or supply native gamepad state before Skyrim derives ButtonEvent/ThumbstickEvent semantics.

Known facts:
- RE::BSInputDeviceManager::PollInputDevices(float) = RELOCATION_ID(67315, 68617)
- One observed runtime example:
  - ExeBase = 0x7FF6E2020000
  - Poll = 0x7FF6E2C350B0
  - Poll RVA = 0xC150B0
- Previous experiments near ControlMap already proved that downstream InputEvent-chain splicing is not suitable as the final design.

I do NOT want:
- ControlMap-side ButtonEvent prepend/append ideas
- PlayerControls::ProcessEvent hooks
- MenuControls::ProcessEvent hooks
- generic hooking advice

I need you to find the upstream gamepad-state path in Skyrim SE 1.5.97.

Please analyze:
1. Which function or object owns native gamepad raw state before InputEvent generation.
2. Which function translates that raw state into button/stick/trigger semantics.
3. Which function boundary is safest to hook if I want to replace the gamepad state for one frame.
4. Whether there is a per-device state buffer, per-frame snapshot structure, or virtual method on a gamepad input device class that is better to hook than PollInputDevices itself.
5. Where menu repeat / hold semantics are derived, and whether they happen before or after InputEvent construction.
6. Whether Skyrim uses a gamepad device object with a Process() method whose output can be safely overridden before ControlMap sees it.

Please return:
- exact functions / RVAs / xrefs
- short pseudocode of the relevant call path
- the concrete object/structure that holds the gamepad state
- the best upstream hook point with evidence
- why that hook point is safer than consumer-side InputEvent splicing

Focus only on Skyrim SE 1.5.97 native gamepad flow.
```

## Narrow Follow-Up Prompt

Use this if the first pass identifies one or two likely gamepad device methods but not enough ownership detail for the next post-Route-B hook:

```text
Please follow xrefs from the Skyrim SE 1.5.97 gamepad input device object and identify:
- the method that polls or copies native gamepad state each frame
- the structure layout that stores button bits, triggers, and sticks
- the first downstream function that consumes that state to derive InputEvent semantics

I need to know the exact point where an SKSE plugin could replace the state for one frame without rebuilding the InputEvent chain manually.
Show evidence from disassembly/decompiler, not guesses.
```
