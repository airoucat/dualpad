# CE3 KBM / Gamepad Switching Initial Findings

_Reconstructed on 2026-04-09 from the active investigation thread after local docs loss._

## Scope

This note captures the first-priority CE3 investigation line:

- how CE3 arbitrates keyboard/mouse vs gamepad switching
- where UI/platform publication sits
- where DualSense-specific detail starts to diverge from generic controller-family publication

It does **not** try to fully document Sony SDK or haptics integration. Those are downstream lines.

## Highest-Confidence Findings

### 1. CE3 keeps an internal high-resolution runtime mode lane

The strongest current mode mapping is:

- `0` = generic gamepad family
- `1` = DualSense family
- `2` = inert/default non-controller baseline
- `0xFF` = actively promoted KBM aggregate mode

The family split is strong:

- `mode <= 1` = controller family
- `mode > 1` = non-controller / KBM family

This gives CE3 two different questions:

- what exact input family is internally active
- what broad controller-family answer should be published outward

### 2. One central receiver owns simultaneous keyboard, mouse, and gamepad state

The strongest structural lead is the central receiver at `qword_145FE1A50`.

Confirmed child layout:

- `+104` = `BSPCKeyboardDevice`
- `+112` = `BSPCMouseDevice`
- `+120` = `BSPCGamepadDeviceHandler`
- `+128` = optional secondary/debug gamepad handler
- `+136` = `BSPCVirtualKeyboardDevice`
- `+96` = internal runtime mode byte
- `+150/+151` = published gamepad-enable value + dirty lane

Raw Windows keyboard and mouse input feed directly into these child handlers.

### 3. Runtime switching is centralized in a poll/update/flush path

The strongest current switching core is:

- `sub_1422D9A90`
  - receiver poll/update/flush entry
- `sub_1422D9B30`
  - runtime mode recompute
- `sub_1422DD180`
  - queue append
- `sub_1422DD250`
  - queue flush/replay

Current best interpretation:

- child handlers accumulate per-poll activity bits
- settings gates filter whether KBM or gamepad activity can participate
- one central recompute function decides the internal runtime mode
- publication then happens through a queue boundary rather than direct UI toggles

### 4. KBM and gamepad switching are gated by explicit setting/state lanes

Currently identified gates:

- `byte_145F67820`
  - `bKeyboardMouseDisabled:General`
- `byte_145F67800`
  - published mirror of receiver-owned `bGamepadEnable` state

Important distinction:

- `+96`
  - internal runtime mode
- `+150/+151`
  - published gamepad-enable lane

So CE3 does not appear to use one single boolean for everything.

### 5. UI-facing surfaces consume a collapsed controller-family answer

The currently confirmed published/query surfaces are:

- `SetPlatform`
- `UsingGamepad`

Both are now tied to the same collapsed bool:

- `bKeyboardMouseDisabled || mode <= 1`

This is one of the most important findings.

It means:

- UI/platform consumers do **not** consume the full `0 / 1 / 2 / 0xFF` lane
- they consume a collapsed `controller-family vs non-controller-family` publication result

### 6. `DeviceChangeEvent` is real, but it is not the same thing as UI platform publication

Queue node types are now partially named:

- type `1` = `MouseMoveEvent`
- type `2` = `CursorMoveEvent`
- type `4` = `ThumbstickEvent`
- type `5` = `DeviceConnectEvent`
- type `6` = `DeviceChangeEvent`

`sub_1422D9B30` emits type `6` when runtime mode changes.

But the currently confirmed upper consumer chain for `DeviceChangeEvent` is:

- `BSInputEventReceiver` layer
- `sub_1412C5D10`
- `sub_141A5ADF0`

This currently looks more like device/feature-state refresh than direct `SetPlatform` broadcasting.

### 7. Exact `mode == 1` is consumed below the UI/query layer

Several readers treat `mode == 1` as a dedicated DualSense-only signal.

Two lower-layer patterns are now visible:

- a configurable family bucket used by lower controller behavior
- a distinct exact-`mode == 1` lane used for DualSense-only propagation

The strongest current example is:

- `sub_14152CEE0`
  - reads `mode == 1`
  - turns it into a boolean
  - pushes that boolean into long-lived downstream objects
  - triggers virtual refresh behavior on change

This strongly suggests:

- DualSense subtype is intentionally kept below the generic UI/publication layer

### 8. Some low-level event interpretation rules flip by active family

Two small readers make this concrete:

- `sub_1412BCBC0`
- `sub_1412BD710`

They switch accepted subtype behavior depending on whether controller-family publication is active.

This matters because it shows CE3 is not just switching UI display. It is also changing lower event interpretation based on current family.

### 9. The strongest currently confirmed Sony-side clue is in the haptics path, not the switching core

Concrete strings now found:

- `DualSense Controller (User %d) (Haptics)`
- `DualSense Controller (User %d) (Rumble)`
- `...\\Wwise\\SDK\\source\\SoundEngine\\Plugins\\Sinks\\AkMotionSink\\Win32\\AkScePadRumbleSink.cpp`

Current best interpretation:

- Sony-associated integration likely exists
- the clearest evidence currently sits in Wwise/motion/rumble plumbing
- the already-mapped KBM/gamepad switching core still looks more like Bethesda-owned abstraction

## Current Interpretation

At this stage, the strongest design lessons from CE3 are:

- CE3 separates internal high-resolution input truth from outward UI/query publication
- one central receiver computes current family from per-poll source activity plus settings gates
- `SetPlatform` and `UsingGamepad` are published/query surfaces, not the switching core
- exact DualSense detail is consumed below the UI layer
- `DeviceChangeEvent` is not the same thing as `SetPlatform`

The practical split is:

- internal capability lane
  - full mode byte and subtype detail
- UI/publication lane
  - collapsed controller-family boolean
- lower controller/device logic
  - may still read exact mode when subtype matters

## Transfer Implications for DualPad

The biggest transferable lesson is structural:

- do **not** let one boolean carry:
  - internal input truth
  - UI platform publication
  - glyph semantics
  - DualSense-specific feature truth

Instead, the direction suggested by CE3 is:

- keep one high-resolution internal family/capability truth
- publish a separate collapsed UI-facing presentation state
- keep gameplay materialization on its own projection path
- reserve a distinct DualSense-specific propagation lane

This directly motivated the later `Input Kernel + Projection` brainstorms and ideation docs.

## Next Actions

1. Keep `SetPlatform` and `UsingGamepad` classified as collapsed publication surfaces, not core mode truth.
2. Prioritize high-resolution mode consumers that look haptics/trigger or feature-exposure related over more generic input heuristics.
3. Keep the Sony-side line focused on Wwise/motion/rumble rather than revisiting already-understood presentation switching.
4. Treat CE3 as a source of structural policy, not a code-copy target.
