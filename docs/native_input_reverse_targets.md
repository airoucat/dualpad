# Native Input Reverse Targets

Historical reverse-engineering notes only.

Do not resume this `ControlMap`-side native button path as the main architecture.
Keep it only for crash reproduction, layout verification, and call-site notes while the real upstream native-state backend is being built.

Current experimental native button path:

- Hook target: `PollInputDevices + 0x53`
- SE 1.5.97 RVA:
  - `0xC15103`
- Patched instruction:
  - original: `E8 F8 C4 FF FF` (`call sub_140C11600`)
  - patched: `call PollControlMapCallHook::Thunk`
- Current strategy in code:
  - Keep the real `sub_140C11600` entry untouched
  - Keep the original `mov rdx, [rdx+380h]` untouched
  - Patch only the specific Poll call-site that calls the ControlMap consumer
  - Runtime-selectable modes:
    - `drop`: install the call hook but discard staged native buttons after draining snapshots
    - `append-probe`: keep native button staging, log `head/queueHead/queueTail/buttonEventCount`, then discard without calling `AddButtonEvent`
    - `append`: flush staged native buttons through `BSInputEventQueue::AddButtonEvent/PushOntoInputQueue`, then pass the updated `queueHead` into ControlMap
    - `head-prepend`: prepend heap-allocated `ButtonEvent` nodes only to the current `head` argument passed into `sub_140C11600`, without touching queue singleton fields
    - `inject`: prepend staged native buttons into the independent input queue singleton before ControlMap
  - Use a normal C++ wrapper with the same `(ControlMap*, InputEvent*)` contract
  - `drop` mode lets the wrapper call the original callee with the unmodified head while measuring stability

Latest result from the previous Poll+0x4C patch attempt:

- Replacing the 7-byte `mov rdx, [rdx+380h]` with a helper stub still crashed before helper logging.
- Reverse guidance now indicates that patch point was wrong even if the surrounding block was identified correctly.
- Practical conclusion:
  - keep the `mov` intact
  - patch the following `call sub_140C11600` at `Poll + 0x53` instead

Confirmed ownership/layout after the updated `agents3` reverse notes:

- `+0x380/+0x388` are not `RE::BSInputDeviceManager` fields.
- They belong to the independent input event queue singleton referenced by `qword_142F50B28`.
- In CommonLib terms, that layout matches `RE::BSInputEventQueue::RUNTIME_DATA::queueHead/queueTail`.
- The current implementation should therefore treat `RE::BSInputEventQueue::GetSingleton()` as the typed wrapper for that independent singleton on SE 1.5.97.

Latest probe result from the failed `0xC11600` entry detour experiment:

- The hook can be installed and the game reaches the main menu.
- A probe-only thunk that does not modify the `InputEvent` chain still crashes immediately after the first invocation.
- The first observed register state was:
  - `rcx = ControlMap*`
  - `rdx = independent input queue singleton + 0x20`
  - `r8 = 0`
  - `r9 = non-null transient value`
- This strongly suggests the current `write_branch<5>` detour target is not safely modeled as a simple `void(ControlMap*, InputEvent*)`-style function entry.
- Resolution:
  - switch from entry detour to the Poll call-site patch above

Why more reverse engineering may still be needed:

- If the pre-ControlMap hook still crashes or loses events, we still need harder evidence for:
  - the exact calling convention and full prototype of `sub_140C11600`
  - whether the `head` argument always aliases the queue singleton `queueHead`
  - whether any additional tail/bookkeeping field besides `+0x380/+0x388` must remain in sync
  - whether `ThumbstickEvent` should be injected through the same singleton cache or a sibling path

Observed failure state in the previous experiment:

- A full detour on `BSInputDeviceManager::PollInputDevices(float)` installs cleanly and the game reaches the main menu.
- The game then crashes before any staged native button event is even logged.
- This strongly suggests:
  - the outer `PollInputDevices` entry is too sensitive to detour directly, or
  - the true safe point is the internal ControlMap consume call rather than the Poll prologue

What to find in IDA / Ghidra:

1. The full control flow of `BSInputDeviceManager::PollInputDevices(float)`.
2. The exact call site where device `Process()` output becomes a single `InputEvent*` / queue head.
3. The exact full prototype of `sub_140C11600`.
4. The exact singleton type behind `qword_142F50B28`.
5. The exact point where the queue singleton is cleared/reset.
6. Whether `BSTEventSource<InputEvent*>::SendEvent(...)` is called with the same head pointer after ControlMap.

What a good hook point looks like:

- Runs on the main thread once per frame.
- Has stable ownership of the per-frame `InputEvent` chain or queue.
- Occurs after raw device polling and before `ControlMap` / `PlayerControls` consumption.
- Allows injecting a fresh `InputEvent` chain or appending to the queue without mutating an already-dispatched sink chain.

What to report back:

- Function name/address and whether it is inside `PollInputDevices` or a subcall.
- Calling convention and full signature if visible.
- A short pseudocode block of the relevant region.
- The best injection offset with reasoning.
- Any evidence that the queue/list is singleton-owned, cached-array-owned, stack-owned, or heap-owned.

Suggested prompt for GPT / reverse-engineering assistant:

```text
I am reversing Skyrim SE 1.5.97 input handling for an SKSE plugin.

Known facts:
- CommonLibSSE-NG identifies RE::BSInputDeviceManager::PollInputDevices(float) as RELOCATION_ID(67315, 68617).
- I need the safest native input injection point for custom gamepad ButtonEvent/ThumbstickEvent injection.
- Previous attempts failed when injecting at:
  1. BSInputEventQueue from arbitrary plugin code
  2. BSTEventSource<InputEvent*>::PrependEventSink
  3. PlayerControls::ProcessEvent / MenuControls::ProcessEvent

Goal:
Find the exact internal point where Skyrim builds the per-frame InputEvent chain/queue after device polling but before ControlMap / gameplay handlers consume it.

Please analyze BSInputDeviceManager::PollInputDevices(float) and any directly-related subcalls.

I need:
1. High-level pseudocode for PollInputDevices.
2. The call sequence for:
   - device Process() / raw input gathering
   - BSInputEventQueue usage
   - InputEvent chain creation
   - ControlMap consumption
   - event dispatch to sinks/handlers
   - queue clear/reset
3. The safest hook site for injecting my own ButtonEvent/ThumbstickEvent as first-class engine input.
4. Whether it is safer to:
   - append into BSInputEventQueue before a specific internal call, or
   - hook a helper that receives InputEvent* and replace/extend that argument.
5. Concrete evidence for the recommendation:
   - address / function boundaries
   - ownership/lifetime of the InputEvent list
   - whether the hook runs before or after consumers start iterating

Please keep the answer focused on Skyrim SE 1.5.97 native input flow, not generic hooking advice.
```

More targeted follow-up prompt for the current crash:

```text
I already tried a full detour on BSInputDeviceManager::PollInputDevices(float) in Skyrim SE 1.5.97, and the game crashes shortly after entering the main menu, before any custom button event is injected.

Please specifically verify:
1. Whether the outer PollInputDevices entry is safe to detour with a 5-byte branch trampoline.
2. Whether PollInputDevices immediately relies on special prologue/stack/XMM state that makes an outer detour risky.
3. Which internal subcall inside PollInputDevices is a better hook point:
   - after devices finish polling
   - after BSInputEventQueue / InputEvent chain is built
   - before ControlMap consumes InputEvent*
4. The exact offset of that safer internal hook point from the start of PollInputDevices.
5. Whether the safer strategy is:
   - hook an internal helper that receives InputEvent*
   - append to BSInputEventQueue just before a specific internal call
   - hook ControlMap’s input-consume function instead of PollInputDevices entry

Please answer with:
- function addresses / offsets
- pseudocode of the critical block
- why the safer hook point is better than detouring PollInputDevices entry
- any calling convention or lifetime hazards I must preserve in an SKSE plugin
```
