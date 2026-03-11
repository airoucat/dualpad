# Official Upstream XInput Route

## Status

`poll-xinput-call` is now the official runtime route for Skyrim SE 1.5.97.

It replaces the old "experimental Route B" wording.

Current default:

```ini
[Injection]
use_upstream_gamepad_hook = true
upstream_gamepad_hook_mode = poll-xinput-call
```

Rollback remains one line:

```ini
use_upstream_gamepad_hook = false
```

That restores the global compatibility fallback path.

## Chosen Implementation

The current production bridge is:

- keep the existing `SyntheticPadState` producer
- hook `BSWin32GamepadDevice::Poll` only at its internal `XInputGetState` call site
- patch the confirmed SE 1.5.97 call at:
  - `Poll = RVA 0xC1AB40`
  - `XInputGetState` call = `Poll + 0x5D`
- verify the surrounding byte window before patching
- drain snapshots immediately before the native poll reads pad state
- fill one synthetic `XINPUT_STATE`
- let Skyrim continue deriving its own downstream controller semantics

What this deliberately does not do:

- no `ControlMap`-side `ButtonEvent` splicing
- no queue head/tail surgery
- no whole-`Poll` vtable wrapping
- no runtime call-site scanning or probe logging in the shipping path

## Why This Is The Best Current Route

It is the narrowest stable hook we have validated so far.

Compared with the older global IAT compatibility path, it moves synthetic-state handoff to Skyrim's actual gamepad poll timing.

Compared with the abandoned consumer-side native button experiments, it stops rebuilding engine-owned `InputEvent` structures by hand.

Compared with the whole-`Poll` wrapper, it leaves more of Skyrim's native device logic untouched.

## Pros And Cons

### Official route: `poll-xinput-call`

Pros:

- upstream enough to preserve Skyrim's own poll timing
- much narrower than wrapping the full `Poll` method
- avoids consumer-side `ButtonEvent` construction and queue mutation
- rollback is trivial
- validated against the Steam Input comparison baseline as the current best-feeling bridge

Cons:

- still feeds a synthetic `XINPUT_STATE`, so it is not the final planner-owned upstream native-state backend
- locked to verified SE 1.5.97 code bytes
- still carries digital-action ownership limits until `KeyboardNativeBackend` is introduced

### Rejected route: whole-`Poll` wrapper (`poll-vtable`)

Pros:

- stable enough as a reverse-engineering probe
- confirmed the gamepad device object/vtable path was real

Cons:

- gameplay and UI semantics were wrong
- broader hook surface than necessary
- no longer worth carrying as an active runtime mode

### Rejected route: consumer-side native button splicing

Pros:

- none that justify production use

Cons:

- crash-prone
- semantically wrong for hold/repeat behavior
- overly sensitive to queue layout, ownership, and timing

### Fallback only: global XInput IAT hook

Pros:

- simple
- reliable rollback path

Cons:

- timing is less correct than the upstream call-site patch
- more likely to show drift in long-hold or fast-input behavior

## Code-Level Rules

Keep these rules fixed for the official route:

- only `poll-xinput-call` is supported as the upstream mode
- verify the fixed SE 1.5.97 call-site before patching
- if verification fails, log once and fall back cleanly
- keep the compatibility IAT hook as fallback support, not as the primary route
- do not reintroduce Route A or runtime scanner/probe logic into the shipping path

## Remaining Limitations

This is the official current route, not the final architecture target.

The long-term direction still remains:

- context-aware planner above all backends
- narrow analog-focused gamepad backend
- keyboard-native backend for most digital actions
- compatibility fallback only when the native route is unavailable

That next phase does not change the current decision:

- Route B is the runtime path to keep
- Route A stays retired
- consumer-side native button injection stays historical only
