# Keyboard Native Backend Plan

## Status

Keyboard-native reverse-engineering routes now exist in code behind:

```ini
[Injection]
use_upstream_keyboard_hook = true
upstream_keyboard_hook_mode = diobjdata-call
```

It is rollback-safe and disabled by default.

As of the latest tests, it is not a production routing path yet.

The hook installs and the synthetic scancode/control helper path executes, but
gameplay actions such as `Sprint` and `Activate` still do not produce reliable
visible behavior in game. Because of that, production action routing has been
returned to the stable non-keyboard backends for now.

Rollback is one line:

```ini
use_upstream_keyboard_hook = false
```

## Current Experimental Implementations

The keyboard route follows the same high-level philosophy as the gamepad Route B:

- hook upstream inside the native device poll path
- patch one internal call instead of splicing downstream input events by hand
- keep compatibility fallback available

For keyboard specifically, two reverse-engineering implementations now exist:

### `semantic-mid`

- targets `BSWin32KeyboardDevice::Poll` on SE 1.5.97
- verifies the post-event block that starts at `Poll + 0x250`
- patches that mid-function boundary instead of the earlier DirectInput read call
- drains `PadEventSnapshotDispatcher` immediately before synthetic control semantics are merged
- refreshes the physical keyboard snapshot through `BSDirectInputManager::GetDeviceState`
- rebuilds `BSWin32KeyboardDevice::curState` from physical state plus DualPad-owned control keys
- directly calls the native control helper `sub_140C190F0` for synthetic press/release transitions only
- leaves the `MapVirtualKeyA -> ToUnicode -> sub_140C169B0` text path untouched

### `diobjdata-call`

- scans `BSWin32KeyboardDevice::Poll` for the internal call to `sub_140C166D0`
- replaces only that call-site with a trampoline wrapper
- drains `PadEventSnapshotDispatcher` before the original DirectInput read helper returns
- appends synthetic `DIDEVICEOBJECTDATA` transitions directly into the same 10-entry event buffer
- updates `numEvents` without exceeding the engine's hard 10-event limit
- lets the native Poll loop continue updating `prevState/curState` and calling `sub_140C190F0` itself
- still suppresses injection entirely during text-entry contexts

## Why This Route Was Chosen For Experimentation

This route fits the current product decisions:

- the mod still presents as a controller mod
- the gamepad backend stays narrow and analog-focused
- most digital actions can move to keyboard-native control injection
- text-input behavior must not be accidentally triggered by controller buttons

Given those constraints, the current upstream keyboard strategy is:

- patch the keyboard device after `diObjData` has already been consumed into `prevState/curState`
- rebuild the current poll's effective `curState` from physical state plus DualPad-owned control keys
- dispatch only synthetic control transitions through the same native helper Skyrim uses for keyboard controls
- keep the text-generation branch untouched so control-only injection cannot type visible text by accident

## Current Experimental Behavior

The current backend implementation does four things:

1. Resolve supported action IDs to keyboard scancodes using cached `ControlMap` lookups first.
2. Fall back to fixed DIK scan codes only if `ControlMap` does not return a usable mapping.
3. Keep stateful action ownership as desired keyboard-down refcounts per scancode.
4. Suppress unsafe scancodes during text-entry contexts such as `Console` / `DebugText`.

Current text-entry rule:

- when text entry is active, only a narrow non-character-safe scancode set is allowed through
- all other desired key states are forced to release until text entry ends

## Supported Runtime Scope

Current implementation scope is intentionally narrow:

- gameplay digital controls such as `Jump`, `Activate`, `Sprint`, `Sneak`, `Shout`
- menu-style controls such as `Accept`, `Cancel`, `Up`, `Down`
- selected aliases already used by existing bindings:
  - `Dialogue.PreviousOption`
  - `Dialogue.NextOption`
  - `Favorites.PreviousItem`
  - `Favorites.NextItem`
  - `Book.PreviousPage`
  - `Book.NextPage`
  - `Book.Close`
  - `Console.Execute`
  - `Console.HistoryUp`
  - `Console.HistoryDown`

Anything unsupported falls back to the existing compatibility route.

At the moment, all production actions also fall back to the existing stable
routes, because the keyboard-native semantic contract is still not proven.

## Pros And Cons

### Pros

- upstream enough to reuse Skyrim's native keyboard state/update path
- symmetric with the existing gamepad Route B philosophy
- respects player keyboard rebinds via `ControlMap` before falling back
- avoids consumer-side `InputEvent` splicing
- avoids global Windows keyboard simulation APIs such as `SendInput`
- explicitly guards against accidental visible text input
- avoids the 10-entry buffered-event overflow problem seen in the earlier append experiment
- `diobjdata-call` is closer to the native keyboard event source than the mid semantic hook
- `diobjdata-call` preserves the native Poll loop's own `prevState/curState` update and `sub_140C190F0` dispatch ordering

### Cons

- still SE 1.5.97-specific and tied to a verified call-site
- still experimental until the gameplay semantic contract is fully proven
- `semantic-mid` already proved that "hook reached + helper called" is not enough for visible gameplay behavior
- `diobjdata-call` still depends on the exact internal call target and 10-event buffer contract
- now depends on a mid-function stub instead of a simple `write_call<5>` replacement
- text-entry detection is intentionally conservative and currently narrow

## Latest Conclusion

Latest tests confirmed:

- scancode resolution is correct
- the semantic mid-hook is hit
- direct calls into `sub_140C190F0` are happening
- the `diobjdata-call` route is not a dead hook:
  - synthetic `DIDEVICEOBJECTDATA` entries are accepted by `BSWin32KeyboardDevice::Poll`
  - `prevState / curState` are updated correctly for synthetic press/release
  - the produced record reaches `sub_140C11600`, `sub_140C10860`, and `sub_140C15E00`
- `sub_140C11600` resolves the record's `+0x18` field into the correct control-name string
- the two global objects involved in the downstream chain are distinct:
  - `qword_142EC5BD0` is the preprocess-side object
  - `qword_142F257A8` is the dispatch-side owner
- `DispatchGlobal` does not get populated by the helpers already tested:
  - `0xC3A450` is not the transfer/builder step
  - `0xC15DC0` is not the transfer/builder step
  - `0xC15D80` is not the transfer/builder step
- a paired synthetic `Space down + Space up` pulse can now reproduce the same
  `eventCount=2` / `spaceNodes=2` preprocess shape as native-assisted samples
- but that paired synthetic shape still fails in `sub_140C11600`:
  - `lateSeen=true`
  - final `field18=empty`
  - `translatedJumpNodes=0`
- giving the synthetic `Space down` native-like:
  - `timeStamp`
  - `sequence`
  - `appData = -1`
  still does not change the failure outcome
- the new `GetDeviceData` envelope probe narrowed the live boundary:
  - pure synthetic `Jump`:
    - `GETDEVICEDATA WRAPPER BUFFER count=0`
    - `GETDEVICEDATA RETURN BUFFER count=2` only after our injection
    - `wrapperLocalChanged=false`
    - `injectionLocalChanged=true`
  - native-success `Jump`:
    - `GETDEVICEDATA WRAPPER BUFFER` already contains native `Space(0x39)`
    - that native event carries non-zero `timeStamp / sequence`
    - our injection only appends the release-shaped companion event
  - in both cases:
    - `GETDEVICEDATA BEFORE WRAPPER`
    - `GETDEVICEDATA AFTER WRAPPER`
    - `GETDEVICEDATA AFTER INJECTION`
    still show manager `eventCount=0` and no preprocess record

But gameplay actions still do not reliably produce visible behavior.

Therefore three older "good-enough" explanations are now ruled out:

- `managerEventCount=1` vs `managerEventCount=2` is not the root cause
- `spaceNodes=1` vs `spaceNodes=2` is not the root cause
- synthetic `timeStamp / sequence / appData` being too fake is not the root cause

Therefore the current keyboard-native route should be treated as:

- a reverse-engineering scaffold
- a future implementation candidate
- not the active production backend for gameplay actions

Current best interpretation:

- the meaningful remaining divergence is native provenance itself
- the synthetic path can now mimic:
  - event count
  - raw-space chain width
  - release companion shape
  - selected metadata
  but still misses some earlier native-only fact
- that missing fact exists before or at the native `GetDeviceData` result
  generation boundary, not later inside:
  - `sub_140C11600`
  - `sub_140C10860`
  - `sub_140C15E00`

## Confirmed Successes Worth Preserving

These are no longer speculative and should be retained as hard-won findings:

- upstream keyboard injection can enter the native `Poll` path without crashing
- synthetic keyboard state can survive the native `Poll` event loop
- the fixed record buffer carried by the global manager is a real part of the downstream path
- `sub_140C15E00` does receive the produced record chain
- the current remaining blocker is downstream consumer/build logic, not:
  - `ControlMap` lookup
  - keyboard scancode resolution
  - `prevState / curState` mutation
  - `sub_140C11600` control-name resolution
- pure synthetic injection can now reliably produce:
  - `eventCount=2`
  - `spaceNodes=2`
  - a press-shaped `Space` node plus a release-shaped companion node
- native success samples are now proven to differ from synthetic samples
  earlier than preprocess:
  - success samples already have a native `Space(0x39)` event in the wrapper
    output before our injection runs
  - pure synthetic samples do not
- the manager/preprocess record is not created inside the `GetDeviceData`
  wrapper itself; it appears later when `BSWin32KeyboardDevice::Poll`
  consumes the returned local buffer

## Current Investigation Target

The active reverse-engineering target is no longer later than
`sub_140C15E00`.

Current focus:

- the native-only provenance that exists before or at the
  `sub_140C166D0` / `GetDeviceData` return boundary
- what real native `Space(0x39)` samples carry that injected `DIDEVICEOBJECTDATA`
  entries still do not
- whether that provenance is produced:
  - inside the real DirectInput/device read path
  - or in a native-only local commit step before the returned buffer is exposed
    to the rest of `BSWin32KeyboardDevice::Poll`

Current live probe direction in code:

- envelope logging around the keyboard `GetDeviceData` call-site:
  - `GETDEVICEDATA BEFORE WRAPPER`
  - `GETDEVICEDATA AFTER WRAPPER`
  - `GETDEVICEDATA AFTER INJECTION`
  - `GETDEVICEDATA WRAPPER BUFFER`
  - `GETDEVICEDATA RETURN BUFFER`
  - `GETDEVICEDATA WRAPPER LOCAL`
  - `GETDEVICEDATA INJECTION LOCAL`
- runtime probe inside `sub_140C166D0` itself:
  - the live bytes show the main wrapper body is only a thin
    `Acquire -> Poll -> GetDeviceData` chain on the DirectInput device vtable
  - the earlier direct-call scan hits near `+0xDC / +0x120 / +0x157 / +0x19D`
    are adjacent helper functions in the same region, not the main
    provenance branch for returned keyboard events
- the next active experiment is therefore a DirectInput shadow-vtable probe on:
  - slot `0x38`
  - slot `0x50`
  - slot `0xC8`
  so the native/synthetic split can be observed at the concrete device-call
  boundary, not only at the game wrapper boundary
- first DirectInput vtable probe results:
  - those slot targets resolve outside the game image, so the interesting
    provenance step is now clearly at the external DirectInput device boundary,
    not another hidden game RVA inside `sub_140C166D0`
  - idle/raw baseline calls return `countOut=0` repeatedly, so broad logging at
    this layer burns budget too early and must be filtered to non-empty returns
  - not every `originalCount=1` sample is useful:
    - some failure samples have a real native event, but it is not `Space(0x39)`
    - at least one confirmed failure sample carried raw scancode `0x01`
    - therefore the current branch is not "native event exists vs does not exist"
      but "native `Space(0x39)` exists vs some other native event exists"
  - narrowed non-empty-return probe now confirms the actual raw DirectInput
    stream:
    - success samples are literal native `0x39` press/release returns from
      DirectInput
    - `Acquire` / `Poll` result codes remain stable (`0x1 / 0x1`) across both
      useful and useless samples
    - failure samples with `originalCount=1` and `nativeFocus=false` are
      literal native `0x01` press/release returns, not malformed `0x39`
      samples
  - this means the current first proven branch is:
    - raw DirectInput returned `Space(0x39)` -> later preprocess can succeed
    - raw DirectInput returned some other key (for example `0x01`) -> that does
      not help synthetic Jump at all

Current live experiment after this result:

- move the synthetic `Jump` injection earlier, from "wrapper returned" to the
  DirectInput `GetDeviceData` return boundary itself
- goal:
  - make the wrapper see a returned raw `0x39` event as if it were present
    before the game-side wrapper continues
  - test whether "wrapper-visible raw `0x39`" is already sufficient, or whether
    there is still some earlier native-only side effect missing

This is now a better candidate than continuing to revisit:

- preprocess chain shape alone
- `sub_140C11600` late-write metadata spoofing
- downstream `sub_140C15E00` handler-family speculation as the primary branch

Additional current test focus:

- compare native-success and pure-synthetic samples at the earliest visible
  boundary where they differ:
  - wrapper output buffer contents
  - keyboard local `diObjData`
  - first `InputLoopProcess[0]` local snapshot
- if the current envelope bounds hold, the next useful hook point is earlier
  than the current call-site patch, not deeper in downstream dispatch

Latest DirectInput return-boundary injection result:

- moving synthetic `0x39` injection from "after wrapper returned" to the
  DirectInput `GetDeviceData` return boundary is **not** sufficient
- pure synthetic samples can now reach the wrapper with:
  - `stageInjected=true`
  - `nativeCountOut=0`
  - `finalCountOut=2`
  - `finalFocusAfter=true`
  - wrapper-local `diObjData` already showing `0x39` press/release
- despite that, preprocess still falls back through the old failure path:
  - `lateSeen=true`
  - `translatedJumpNodes=0`
  - manager record stays `raw-space` / `field18=empty`
- therefore the old working assumption:
  - "if the wrapper sees a raw `Space(0x39)` buffer early enough, synthetic Jump
    should follow the native path"
  is now falsified by live runtime evidence
- the strongest current inference is:
  - the missing factor is outside the returned `DIDEVICEOBJECTDATA` payload
  - and likely lives in native-only local/device state that is established
    earlier than the DirectInput return boundary, or in a hidden keyboard-local
    commit step not reflected by the visible `diObjData` buffer alone

Current live experiment after this new result:

- compare native-success and stage-injected synthetic samples using a broader
  `BSWin32KeyboardDevice` hidden-state snapshot
- log hashes and coarse diffs for the object prefix from object start up to
  `diObjData`, split into:
  - header
  - `prevState`
  - `curState`
  - post-state / pre-`diObjData` tail
- goal:
  - determine whether the missing native provenance is still inside the game's
    keyboard object but outside visible `prevState / curState / diObjData`
  - or whether the remaining divergence must be pushed even earlier/outside the
    game-side keyboard object entirely

Latest coarse hidden-state snapshot result:

- the earlier coarse hidden-state gate was indeed wrong:
  - it treated the keyboard object as one continuous prefix up to `diObjData`
  - but `BSWin32KeyboardDevice` actually places `diObjData` before
    `prevState / curState`
  - so the original hidden hash path could read the wrong bytes and produce
    false conclusions
- this probe has now been corrected to snapshot four real regions separately:
  - header
  - `prevState`
  - `curState`
  - tail
- with that fix in place, the latest live sample on 2026-03-08 around
  `23:58:03` to `23:58:04` shows a much cleaner result:
  - `GETDEVICEDATA HIDDEN BEFORE / AFTER WRAPPER / AFTER INJECTION`
    stay identical within each invocation
  - the only object-level hidden delta currently observed is at
    `InputLoopProcess[0]`, where `prevState` offset `0x1A0` toggles
    `0x0 <-> 0x8000`
  - `header`, `curState`, and tail stay unchanged in those pairs
  - preprocess still fails in the same samples:
    - `lateSeen=true`
    - `translatedJumpNodes=0`
    - final record remains `raw-space` / `field18=empty`
- this now gives a firmer boundary judgment:
  - the missing native provenance is not showing up in the visible
    `BSWin32KeyboardDevice` regions we currently capture at the
    `GetDeviceData` wrapper boundary
  - the observed `prevState[Space]` toggle inside `InputLoopProcess[0]`
    is real, but not sufficient to explain the native-success path
  - so the next likely carrier is outside this visible game-side keyboard
    object state:
    - deeper DirectInput device-side/internal state
    - or another external/native-only carrier not reflected into
      `header / prevState / curState / tail` by the time we probe

Current next experiment after that result:

- keep the game-side keyboard hidden snapshot as-is
- add a new shallow external DirectInput device snapshot around the real
  device vtable targets:
  - snapshot the first `0x60` bytes of the `IDirectInputDevice8A` object
  - snapshot one layer of pointee memory for the pointer slots at
    `+0x08 / +0x10 / +0x18 / +0x20 / +0x28`
  - compare those snapshots across:
    - `DirectInputPoll BEFORE/AFTER`
    - `DirectInputGetDeviceData` before the real external call vs after it
- the goal is to answer a narrower question:
  - does native provenance finally become visible in the external DirectInput
    device-side state, even though it does not appear in the visible
    `BSWin32KeyboardDevice` state at the same boundary
- expected new runtime markers:
  - `DIRECTINPUT POLL DEVICE BEFORE/AFTER`
  - `DIRECTINPUT POLL DEVICE`
  - `DIRECTINPUT GETDEVICEDATA DEVICE BEFORE`
  - `DIRECTINPUT GETDEVICEDATA DEVICE AFTER NATIVE`
  - `DIRECTINPUT GETDEVICEDATA DEVICE`
  - `nativeDeviceChanged=`

Latest shallow DirectInput-device snapshot result:

- the shallow external device snapshot did not produce a split either
- in the 2026-03-09 00:10 sample:
  - failure samples still show:
    - `nativeCountOut=0`
    - `finalCountOut=2`
    - `stageInjected=true`
    - `nativeDeviceChanged=false`
    - later `lateSeen=true` and `translatedJumpNodes=0`
  - success samples still show:
    - `nativeCountOut=1`
    - `finalCountOut=2`
    - `nativeFocusAfter=true`
    - `stageInjected=true`
    - `nativeDeviceChanged=false`
    - later `lateSeen=false` and `translatedJumpNodes=2`
- so the first visible difference is still whether a real native `Space(0x39)`
  exists in the native return buffer
- but that difference is not reflected in:
  - the visible `BSWin32KeyboardDevice` hidden snapshot
  - or the current shallow `IDirectInputDevice8A` object / one-hop pointee
    snapshot
- one additional correction from this run:
  - the current "DirectInput device" target being observed resolves into
    `skse64_1_5_97.dll`, not into the game image and not yet clearly into
    `dinput8.dll`
  - so the next useful narrowing step is not broader device memory hashing,
    but direct call-site probing inside the current external `GetDeviceData`
    / `Poll` target functions themselves

Latest external-target runtime probe result:

- the external target runtime probe is now active and confirms that the
  currently observed `GetDeviceData` / `Poll` functions both live in
  `skse64_1_5_97.dll`
- the new samples still preserve the old success/failure split:
  - success:
    - `nativeCountOut=1`
    - `nativeFocusAfter=true`
    - `stageInjected=true`
    - `lateSeen=false`
    - `translatedJumpNodes=2`
  - failure:
    - `nativeCountOut=0`
    - `finalCountOut=2`
    - `stageInjected=true`
    - `lateSeen=true`
    - `translatedJumpNodes=0`
- however, even at this boundary the shallow device-state summary still shows
  `nativeDeviceChanged=false`, so the useful new information is no longer in
  object-hash deltas, but in the scanned inner call graph itself
- one correction after reviewing the dumped bytes:
  - external `GetDeviceData` target `+0x30` is a false positive from linear
    `E8` scanning, not a real direct call instruction
  - external `Poll` target `+0x4A` is also a false positive for the same
    reason
- current trustworthy external-target call-site candidates are:
  - external `GetDeviceData` target `+0x77`
  - external `GetDeviceData` target `+0x10D`
  - external `GetDeviceData` target `+0x136`
  - external `GetDeviceData` target `+0x146`
  - external `Poll` target `+0xA1`
- current best inference from the byte dump is:
  - external `GetDeviceData` target `+0x136` is the most promising real worker
    call
  - it is preceded by explicit register setup plus two stack arguments, so it
    looks more like a real provider/transform step than the surrounding helper
    calls
  - `+0x146` is more likely a late helper / cookie-check-style tail call than
    the missing upstream carrier
- current routing judgment after this probe:
  - do not keep broadening hidden/object snapshots
  - next experiment should target a typed call-site hook on:
    - `EXTERNAL GETDEVICEDATA TARGET +0x136`
  - with external `Poll +0xA1` kept only as a secondary fallback

Latest external `+0x136` worker-hook result:

- the narrowed worker hook is now validated by live logs:
  - new samples show `capacityIn=10` rather than the old flooded `countBefore=10`
  - this confirms the latest gate is live and only logs non-empty / focus-bearing worker invocations
- `EXTERNAL GETDEVICEDATA TARGET +0x136` is no longer just a plausible worker:
  - it is the first currently observed call-site where real native `DIDEVICEOBJECTDATA`
    records become visible in-process
  - successful samples now show:
    - worker `countAfter=1`
    - `focusAfter=true`
    - worker buffer already contains native `ofs=0x39 data=0x80/0x00`
    - outer `DirectInputGetDeviceData AFTER` then reports `nativeCountOut=1`
      and `nativeFocusAfter=true`
    - later preprocess remains `lateSeen=false` and ends with
      `translatedJumpNodes=2`
- this means the current first hard boundary has moved forward again:
  - the missing native provenance is not after `+0x136`
  - it is now more likely before `+0x136`, in the same external target,
    or in the path that decides whether `+0x136` returns `0` vs `1`
- failure samples in the same runtime still show the older pattern:
  - outer `nativeCountOut=0`
  - `finalCountOut=2`
  - `stageInjected=true`
  - later preprocess `lateSeen=true`
- current next narrowing step:
  - keep `+0x136` as the confirmed native-materialization worker
  - add a lighter pre-worker probe on `EXTERNAL GETDEVICEDATA TARGET +0x10D`
    to determine whether any visible branch appears immediately before the worker
  - keep external `Poll +0xA1` only as a fallback if `+0x10D` still shows no useful split

Latest `+0x10D` pre-worker follow-up:

- the pre-worker hook now installs successfully on `EXTERNAL GETDEVICEDATA TARGET +0x10D`
- however, current interesting live samples show:
  - install log is present
  - no `ExternalGetDeviceDataPreWorker SNAPSHOT` lines appear alongside either
    successful or failed `DirectInputGetDeviceData AFTER` samples
- current interpretation:
  - `+0x10D` is not providing a useful visible split for the successful / failed
    focus-bearing path
  - it may be a cold branch, a helper only used on some non-interesting path,
    or simply not the step that decides whether `+0x136` emits native events
- current next step is therefore tightened again:
  - stop treating `+0x10D` as the primary narrowing point
  - keep `+0x136` as the confirmed native-materialization worker
  - shift the fallback experiment forward to `EXTERNAL POLL TARGET +0xA1`
    and correlate that inner call with the later `DirectInputGetDeviceData AFTER`
    samples

Latest external `Poll +0xA1` fallback result:

- the external Poll worker hook now installs successfully
- however, current useful live samples still show:
  - no `ExternalPollWorker SNAPSHOT` lines alongside either successful or failed
    outer `DirectInputGetDeviceData AFTER` samples
- current interpretation:
  - `EXTERNAL POLL TARGET +0xA1` is not yet giving a visible split for the hot
    success/failure path
  - this fallback is therefore lower value than expected
- revised next step:
  - stop treating `Poll +0xA1` as the primary narrowing point
  - instead, refine the confirmed `+0x136` worker path one step further by
    distinguishing:
    - failure cases where `+0x136` is called but returns `countAfter=0`
    - failure cases where the path never reaches `+0x136` at all
  - the outer `DirectInputGetDeviceData AFTER` log should now carry a compact
    `ExternalGetDeviceDataWorker SNAPSHOT` summary for exactly that purpose

Latest `+0x136` worker-snapshot result:

- `+0x136` is confirmed to be an offset inside the external `GetDeviceData`
  target that lives in `skse64_1_5_97.dll`, not in `SkyrimSE.exe`
- live install / probe lines now show:
  - external `GetDeviceData` target address itself resolves to
    `skse64_1_5_97.dll`
  - the `+0x136` worker call-site and its original target also both resolve to
    `skse64_1_5_97.dll`
- the new worker snapshot also answers the pending failure question:
  - failure samples do reach `+0x136`
  - but they return `countAfter=0` / `focusAfter=false`
  - success samples reach the same `+0x136` and return `countAfter=1`
  - therefore the current split is not "worker reached vs bypassed", but
    "same worker reached, different native output"

Latest `+0x136` worker-target runtime-probe result:

- the worker body itself is now probed as a standalone target inside
  `skse64_1_5_97.dll`
- current live candidates are:
  - direct call `+0x39`
  - direct call `+0x10E`
  - both resolve to the same inner helper target
  - indirect call `+0x4D` resolves into `ntdll.dll`
  - one later virtual call appears at `+0xB4 [rax+0x50]`
- current success/failure samples still do not differ at the outer worker
  argument shape:
  - `rcx / rdx / r8 / r9` remain stable across both paths in current logs
  - the meaningful observed split remains the worker output itself:
    `countAfter=1` vs `countAfter=0`
- therefore the next narrowing step should move from the worker boundary to the
  two inner direct call-sites at `+0x39` and `+0x10E`, before spending more
  effort on broader outer-envelope logging

Latest worker-owned object finding:

- the current worker byte pattern shows that the later hot-path virtual calls
  are driven by the object passed through `rdxArg` into the confirmed `+0x136`
  worker
- a direct object-level probe attempt identified that object's `slot 0x50`
  target as living in `DINPUT8.dll`, not in Skyrim or `skse64_1_5_97.dll`
- that first shadow-vtable probe on the worker-owned object was not stable and
  caused an immediate crash after installation
- therefore:
  - the `DINPUT8.dll slot 0x50` finding should be kept as a real boundary
  - but the current object-vtable patching method should be treated as unsafe
    and disabled
  - any follow-up on that boundary should use a safer method than direct
    object-level shadow-vtable replacement

Latest worker-owned object snapshot result:

- the safer read-only snapshot around the worker-owned `rdxArg` object is now
  live and stable
- in successful `countAfter=1` samples, the worker-owned object remains shallowly
  identical across the `+0x136` call boundary:
  - identical `objectHash`
  - identical `+0x00 / +0x08 / +0x10 / +0x18 / +0x20 / +0x28`
  - identical linked `+0x08` target hash
  - `workerObjectChanged=false`
- therefore the currently captured shallow object layer is still not the first
  native split
- follow-up logging should keep the read-only method, but explicitly preserve a
  small budget for `countAfter=0` failure samples so successful and failed
  worker-owned object snapshots can be compared directly

Latest nested linked-target snapshot result:

- the worker summary now also carries:
  - `workerObjectHash`
  - `link08`
  - `link08nested`
- current live results show:
  - `workerObjectHash` is identical in both failure (`countAfter=0`) and
    success (`countAfter=1`) samples
  - `link08` address/hash is also identical across both paths
  - `link08nested` can vary across successful samples, but at least one success
    sample still matches the same `link08nested` hash seen in repeated failure
    samples
- therefore:
  - the split is not explained by the current worker-owned object itself
  - and not by the currently captured `rdxArg + 0x08` plus one extra `+0x08`
    nested snapshot layer either
- the remaining useful narrowing target is now deeper than the current
  read-only object fingerprint, or inside the worker's own return-value path
  rather than these shallow object headers

- one more correction from the same run:
  - the added `link08nestedVtable` / `link08nestedSlot50` fields did not yet
    expose a stable success/failure split
  - repeated failure samples stayed at:
    - `link08nestedVtable=0`
    - `link08nestedSlot50=0`
  - but successful samples were mixed:
    - some still matched the same `0/0` shape
    - some produced a non-zero `link08nestedVtable` (for example `0x8000`)
      while `link08nestedSlot50` still remained `0`
- therefore the nested-object vtable interpretation is currently too weak to be
  treated as the real `slot 0x50` boundary
- the next safer read-only step should move back to the worker-owned object
  itself and derive its own `vtable` / `vtable[0x50]` target directly, instead
  of inferring that boundary from the nested `link+0x08->+0x08` object

Latest worker-object `vtable / slot50` result:

- the worker summary now also carries:
  - `workerVtable`
  - `workerSlot50`
- current live results show that repeated failure samples (`countAfter=0`) and
  successful samples (`countAfter=1`) still share the same worker-owned object
  type boundary:
  - identical `workerObjectHash`
  - identical `workerVtable`
  - identical `workerSlot50`
- therefore the previously suspected worker-owned `slot 0x50` boundary is not
  enough by itself to explain `countAfter=0/1`
- the next narrowing step should move away from object type and toward the
  worker's per-call temporary return path, especially the stack-backed buffer
  passed as `stackArg5`

Latest `stackArg5` structured-field result:

- replacing the old raw `0x80` stack hash with structured `QWORD` logging
  produced the first stable per-call split inside the same `+0x136` worker
- failure samples (`countAfter=0`) now consistently show:
  - `stack5FieldMask=0x01`
  - `stack5Q0After=0x7FF600000000`
- successful samples (`countAfter=1`) also show:
  - `stack5FieldMask=0x01`
  - but `stack5Q0After=0x7FF600000001`
- therefore:
  - the raw whole-buffer `stackArg5` hash was only scratch noise
  - but the worker's per-call temporary output is no longer opaque
  - the first stable live split has narrowed to `stackArg5 + 0x00`, very
    likely the low bit of that first `QWORD`
- this does **not** yet prove whether that bit is:
  - a native-event count
  - a success flag
  - or a tagged pointer / status token
- but it is now the narrowest confirmed boundary that differs between repeated
  failure and success samples while the worker object identity stays the same
- the next useful step should compare `stack5Q0Before` vs `stack5Q0After`, and
  then either:
  - statically reverse the write to `stackArg5 + 0x00`
  - or place a new minimal runtime probe on the inner path that produces that
    bit

Latest static read of the `+0x136` worker body (`skse64_1_5_97.dll`, `RVA 0xB9E0`):

- the worker body is now statically decoded far enough to explain the
  `stackArg5` result without any new runtime guessing
- `stackArg5` is not a hidden token object:
  - it is the `numEvents` pointer passed to the underlying call
  - the repeated live value `stack5Q0Before=0x7FF60000000A` is explained by a
    normal 32-bit event-capacity value (`0x0000000A`) sharing an 8-byte slot
    whose upper 32 bits are just unrelated neighboring data
- the worker has two main branches:
  - if `rdi + 0x808 == 0`, it passes the real `numEvents` pointer directly to
    `call [rsi + 0x50]`
  - otherwise it copies `*numEvents` to a stack temp (`[rsp+0x80]`), calls the
    same `call [rsi + 0x50]`, then may post-process cached records and writes a
    final 32-bit count back through the original `numEvents` pointer
- the key writebacks are now understood:
  - `mov dword ptr [rcx], eax` at `RVA 0xBAAE`
  - `mov dword ptr [rax], ecx` at `RVA 0xBB9D`
- therefore the previously observed split:
  - failure: `stack5Q0After=...00000000`
  - success: `stack5Q0After=...00000001`
  is not a mysterious worker-local provenance bit; it is simply the final
  32-bit event count written back by the worker
- this tightens the boundary again:
  - the unresolved split is no longer "some hidden state inside the SKSE
    worker"
  - it is now the return path of the underlying `call [rsi + 0x50]`, i.e. the
    `DINPUT8` device-side `GetDeviceData` implementation or data it consumes

Practical implication:

- continuing to search for a new split inside the SKSE worker itself is now
  low-value
- the next meaningful research step should be one of:
  - move upstream to a `dinput8`/DirectInput proxy
  - or do deeper static reversing on the `DINPUT8` device-side `vtable[0x50]`
    path

Current `dinput8` proxy status:

- a separate research target now exists under:
  - `tools/dinput8_proxy/`
- it builds as a standalone `dinput8.dll` via:
  - `xmake build DualPadDInput8Proxy`
- current proxy structure is still intentionally thin:
  - export `DirectInput8Create`
  - load the real system `dinput8.dll`
  - wrap `IDirectInput8A`
  - wrap keyboard `IDirectInputDevice8A`
  - log `Acquire`, `Poll`, `GetDeviceState`, and `GetDeviceData`
- local build/deploy is now wired for fast iteration:
  - `DualPadDInput8Proxy` builds from the main repo tree
  - the current local test target deploys directly to `G:\g\SkyrimSE\dinput8.dll`
- the proxy is no longer strictly observe-only:
  - a default-off `DualPadDInput8.ini` test mode can rewrite keyboard
    `GetDeviceData` return records at the `dinput8` boundary
  - current research mode preserves original timestamp / sequence / appData from
    the source record when it rewrites the scancode

Latest `dinput8` proxy live-log result:

- the proxy is now confirmed to sit on the real Skyrim keyboard DirectInput
  chain:
  - `DirectInput8Create` succeeded
  - `CreateDevice(GUID_SysKeyboard)` was called and wrapped
  - subsequent keyboard `Acquire`, `Poll`, `GetDeviceState`, and
    `GetDeviceData` calls all hit the proxy log
- live keyboard samples captured directly at the proxy show true native
  `DIDEVICEOBJECTDATA` records before any SKSE-side worker processing:
  - `ofs=0x39 data=0x80` for Space down
  - `ofs=0x39 data=0x0` for Space up
  - non-Space native samples like `ofs=0x1` also appear in the same stream
- a new boundary test has now succeeded:
  - proxy config remapped real `F10` (`ofs=0x44`) records to `Space`
    (`ofs=0x39`) at the `dinput8` `GetDeviceData` return boundary
  - proxy log confirmed injected samples with:
    - `injected=true`
    - `injectedSourceOfs=0x44`
    - returned `ofs=0x39 data=0x80/0x0`
  - Skyrim-side logging for those same injected samples then showed:
    - `PREPROCESS SPACE PROFILE BEFORE ... managerEventCount=2`
    - `PreprocessWriteSummary ... lateSeen=false`
    - `PREPROCESS SPACE PROFILE AFTER ... translatedJumpNodes=2`
    - `sub_140C15E00` receiving translated `Jump`
- this is the first successful proof that a keyboard-event rewrite at the
  `dinput8` return boundary is early enough to reproduce the native-success
  `Space -> Jump` path inside Skyrim
- important nuance:
  - this test rewrote a real native DirectInput keyboard record (`F10`) into
    `Space`
  - it does **not** yet prove that a fully synthetic record created from no
    native keyboard source at all will also succeed
- this is important because it confirms the earlier boundary analysis:
  - the real native keyboard signal is present and observable at the `dinput8`
    layer itself
  - the earlier SKSE-worker boundary (`+0x136`) was indeed too late for source
    creation, but the `dinput8` return boundary is early enough for scancode
    substitution on a real native record
- practical implication:
  - continuing research at the `dinput8` proxy / wrapped `IDirectInputDevice8A`
    layer is justified
  - the best next question is no longer "is this boundary early enough?"
  - it is now:
    - whether full record synthesis at this same boundary can also succeed
    - or whether success still requires an original native DirectInput source
      record to be present in that call

Latest deferred-emit follow-up:

- the earlier `defer_to_empty_call` experiment was initially invalid because the
  proxy still re-emitted from the pending queue in the same `GetDeviceData`
  call
- after fixing that bug, live proxy logs now show true cross-call deferred
  delivery:
  - emitted records report `deferredEmit=true`
  - and also `pendingBefore=1 pendingAfter=0`
  - meaning the `Space(0x39)` record was already queued before the emitting
    `GetDeviceData` call began
- Skyrim-side logs for the first deferred `F10 -> Space` sample still show the
  same success path:
  - `PreprocessWriteSummary ... lateSeen=false`
  - `PREPROCESS SPACE PROFILE AFTER ... translatedJumpNodes=2`
  - `sub_140C15E00` receiving translated `Jump`
- therefore:
  - same-call coexistence with another native record is **not** required
  - the `dinput8` return boundary itself is sufficient even when the rewritten
    `Space` is delayed to a later empty `GetDeviceData` call
- practical latency note:
  - the first deferred `F10` can feel slightly slower because this experiment
    deliberately waits for a later `GetDeviceData` call before emitting
  - once emitted, Skyrim still consumes it as a native-success sample in the
    same downstream processing pass
- remaining limitation:
  - current proxy logging suppresses the swallowed `returned=0` queueing call
  - so this run proves the extra delay exists by design, but does not yet pin
    down the exact wall-clock gap between queue and emit

Latest pure-synthetic `dinput8` boundary result:

- a new proxy mode now synthesizes `DIDEVICEOBJECTDATA` directly from
  `GetAsyncKeyState(VK_F10)` edges at the `dinput8` `GetDeviceData` return
  boundary
- in this mode:
  - no original native keyboard source record is preserved
  - generated records carry:
    - `ofs=0x39`
    - `data=0x80/0x0`
    - `timeStamp=0`
    - `sequence=0`
    - `appData=0`
- live proxy logs now show this pure-synthetic mode emitting:
  - `injected=true`
  - `pureSyntheticMode=true`
  - returned `ofs=0x39 data=0x80/0x0`
- Skyrim-side logs for the same first `F10` sample show:
  - `GETDEVICEDATA WRAPPER BUFFER count=2` with only synthetic-style
    `0x39 down/up`
  - `PreprocessWriteSummary ... lateSeen=false`
  - `PREPROCESS SPACE PROFILE AFTER ... translatedJumpNodes=2`
  - `sub_140C15E00` receiving translated `Jump`
- therefore the key research question is now answered:
  - fully synthetic keyboard records created from scratch at the `dinput8`
    return boundary are sufficient to reproduce the native-success
    `Space -> Jump` path
  - success does **not** require:
    - an original native `F10` record in the same call
    - preserved native `timeStamp`
    - preserved native `sequence`
    - preserved native `appData`

Practical implication:

- the unresolved boundary is no longer inside Skyrim or the SKSE worker path
- the viable formal keyboard-native implementation point is now the `dinput8`
  proxy / wrapped keyboard `IDirectInputDevice8A::GetDeviceData` boundary

Historical notes below are still useful as reverse-engineering history, but any
statement that conflicts with the envelope findings above should be treated as
superseded.

## Alternative Path Summary And Research Boundaries

The current `+0x136` result changed the value of several alternative routes:

- `KeyboardNativeBackend` is still the cleanest in-engine concept if true
  keyboard identity can be recovered, but it is no longer a good open-ended
  product bet
- `SendInput` remains a low-cost experiment path only; it is not expected to
  satisfy the same native provenance contract already missing at the current
  `GetDeviceData` boundary
- `dinput8.dll` / DirectInput proxy remains a plausible intermediate research
  step because it moves the experiment more upstream than the current
  `skse64_1_5_97.dll` worker chain without requiring a driver
- virtual HID keyboard remains the most credible route if true PC-keyboard
  identity for third-party mods becomes a hard requirement

For feature delivery, there is now a separate practical fallback:

- if a feature only needs the gameplay/menu/camera effect normally caused by a
  PC-native event, prefer direct semantic execution over more keyboard
  provenance work
- `TrueDirectionalMovement` and `BFCO` are good references for that style:
  they hook handlers or animation-driven semantics directly instead of trying to
  fabricate new keyboard events
- `MCO/DMCO` assets are still useful as a combat semantic/event vocabulary, but
  they do not provide a reusable keyboard-input implementation

Current stop-loss rule for this research line:

- keep narrowing the native provenance chain while the next experiment clearly
  advances the first visible split
- if a new probe only reaffirms that the same `+0x136` worker returns `0` vs
  `1` without exposing a new inner branch, treat the remaining gap as belonging
  to a deeper external/native source layer and stop treating this path as a
  near-term implementation route

Latest live-log cleanup:

- already-validated preprocess / builder / consumer probes are no longer installed in normal debug runs
- current logs are intentionally narrowed to:
  - `sub_140C15E00`
  - `handler[1] +130/+131`
  - event `record+0x08 / record+0x0C / code`
- the goal is to identify the first handler-side early-return filter without drowning the runtime log in already-proven front-half state dumps

Current focused validation add-on:

- keep one small native-keyboard baseline log alive:
  - keyboard device object
  - keyboard `deviceObject+0x08`
  - first few native `DIDEVICEOBJECTDATA` entries
- this is specifically to confirm whether dispatch-record `record+0x08=2` is native behavior or a synthetic-only mismatch
- current handler-side logging now also records:

Current one-shot template-capture support:

- native `Jump` dumps are armed even when no synthetic `diObjData` entries were appended in that poll
- synthetic `Jump` uses the existing keyboard-native gameplay route, so no temporary
  `Menu.Confirm` reroute is needed
- the one-shot trigger accepts either:
  - translated `field18 = Jump`
  - or the raw keyboard `Space` scancode (`code = 0x39`)
  because synthetic `Jump` currently reaches `sub_140C15E00` in the raw `Space`
  event shape before later dispatch failure
- when a native `Space` event and a synthetic `Space` release coexist in the same
  poll, the one-shot capture now intentionally prefers the native sample so the
  dump does not arm both sides and cancel itself out
- the live debug configuration keeps the older manager-head patch off during this
  wrapper/event template comparison to reduce unrelated noise
  - dispatch-owner raw qword scan
  - parent-handler raw qword scan

Latest `Jump` template-capture result:

- the first successful paired native/synthetic template capture is now available
  for `Jump`
- native `Jump` reaches `sub_140C15E00` as a translated multi-node chain:
  - head node is `Jump` with:
    - `field08 = 0`
    - `field0C = 0`
    - `code = 0x39`
    - `field18 = Jump(+0x2B8)`
    - `value = 1.0`
  - the chain also includes a companion unreadable node and a release-shaped
    `Jump` node
- synthetic `Jump` currently reaches `sub_140C15E00` in a narrower raw-keyboard
  shape:
  - head node is `Space`/`Jump` raw key with:
    - `field08 = 0`
    - `field0C = 0`
    - `code = 0x39`
    - `field18 = empty`
    - `value = 1.0`
  - followed by a release-shaped raw node
- therefore the first concrete native-vs-synthetic divergence is now confirmed
  before parent-handler acceptance:
  - native `Jump` already has the translated descriptor at `event + 0x18`
  - synthetic `Jump` is still a raw `Space`-style event object with
    `event + 0x18 = empty`

Wrapper/arg3 paired dump differences already observed:

- wrapper:
  - native and synthetic both point `wrapper + 0x00` at the same `currentHead`
    shape
  - but they differ at least at:
    - `wrapper + 0x08`
    - `wrapper + 0x28`
    - `wrapper + 0x40`
    - `wrapper + 0x50`
    - `wrapper + 0x58`
- arg3:
  - native and synthetic currently differ at least at:
    - `arg3 + 0x48` / surrounding qword block in the captured sample

Immediate implication:

- the previously suspected rejection point "translated event routed into the
  wrong family after parent-handler wrapper construction" is still relevant, but
  `Jump` now proves there is also an earlier divergence:
  - native accepted path reaches parent dispatch already translated
  - synthetic rejected path can still arrive as a raw keyboard event object
- the next reverse step should compare:
  - native accepted `Jump` wrapper/context build
  - synthetic raw `Jump` wrapper/context build
  and identify where the raw synthetic path is supposed to be upgraded into the
  translated control-event family but currently is not

Latest raw-structure scan result:

- the `sub_140C15E00` dispatch owner's first `0x60` bytes do not contain a
  direct pointer equal to:
  - `qword_142F25250`
  - `qword_142F50B28`
- the first `0x60` bytes of the observed parent handlers also do not contain a
  direct pointer equal to either of those globals
- therefore the current wrapper/handler path does not appear to embed the two
  known global objects directly in their front-side qword fields
- the relation to:
  - compare-family object graph (`qword_142F25250`)
  - input-manager object graph (`qword_142F50B28`)
  is likely indirect, via:
  - secondary pointed-to objects
  - vtable-driven accessors
  - or later wrapper/context construction

Immediate implication:

- the next reverse target should not assume a simple direct field like
  `handler + X == qword_142F25250`
- the useful next step remains the dispatch wrapper/context path created around
  `sub_140C15E00`, not another shallow qword scan of the same objects

Current manager-head verification add-on:

- a narrow runtime probe now compares:
  - `qword_142F50B28 + 0x380 / +896`
  - the `currentHead` passed into `sub_140C15E00`
- an opt-in test switch can temporarily overwrite that manager-side stored head
  with `currentHead` for one dispatch call:
  - `test_keyboard_manager_head_patch = true`
- this is explicitly a rollback-safe reverse-engineering experiment to verify
  whether the synthetic path is failing because the manager-side stored head is
  stale or null when dispatch begins

Latest manager-head experiment result:

- live runtime confirmed that before every synthetic `sub_140C15E00` call:
  - `qword_142F50B28 + 0x896` / stored head was `0`
  - `currentHead` was the expected translated record head
  - so `match=false` was real, not a logging artifact
- a rollback-safe test then temporarily overwrote:
  - `qword_142F50B28 + 0x896 = currentHead`
  for one dispatch call, and restored it immediately afterward
- this successfully forced:
  - `match=true`
  - the post-dispatch stored head to equal `currentHead`
- but parent/child handler behavior did not change:
  - parent handlers still received the same wrapper `arg2`, not `currentHead`
  - child validators still compared against `Click(+0x2B8)`
  - child validators still returned `false`

What this proves:

- stale/null `qword_142F50B28 + 0x896` is a real divergence between native and
  synthetic dispatch state
- but it is NOT the root cause of the current rejection
- forcing the manager-side stored head to `currentHead` does not alter the
  handler-family routing or the child validate failure
- therefore the remaining blocker is still the wrapper / dispatch-context path
  after `sub_140C15E00`, not the simple manager-head slot itself
  - `handler[1]` validate virtual at `vtable + 0x08`
  - `handler[1]` type-0 process virtual at `vtable + 0x28`
- this is specifically to confirm whether the synthetic record is failing at:
  - the validate virtual
  - or the type-0 processing virtual

Latest focused findings:

- at `sub_140C15E00`, the current synthetic records consistently show:
  - `record+0x08 = 2`
  - `record+0x0C = 0`
  - `code = 0x2` for `Down`
  - `code = 0x1000` for `Accept`
  - `value = 1.0` on press / hold and `0.0` on release
  - steadily increasing `holdTime` during held samples
- `handler[1]` currently shows:
  - `flag130 = 0`
  - `flag131 = 0`
- therefore the currently observed synthetic failure is not explained by:
  - `handler+130`
  - `handler+131`
  - malformed control-name resolution
- native keyboard baseline logging is now present in code, but one fresh runtime capture did not yet include any matching native-keyboard lines, so `record+0x08=2` is still not proven to be either:
  - correct native behavior
  - or a synthetic-only mismatch

New native baseline result:

- real keyboard input was captured through the same upstream hook
- `BSWin32KeyboardDevice` itself currently reports:
  - `deviceObject+0x08 = 0`
  - example native scancodes observed: `0x11`, `0x39`, `0x01`
- therefore the dispatch-stage record field logged as `event +0x08` is not a trivial copy of `keyboardDevice +0x08`
- this also means the earlier hypothesis "keyboard events should obviously use deviceID=1" is not supported by current runtime evidence
- current best interpretation is:
  - the device object's `+0x08`
  - and the dispatch record's `+0x08`
  are different semantics and must be reversed separately

Current next probe:

- stop treating `handler[1] vtable+0x08` (`RVA 0x8A7CA0`) as the final validate target
- instead, treat it as the handler's primary processing function
- inspect `handler[1] + 24` as the sub-handler list and log:
  - list array pointer
  - list count
  - each sub-handler object
  - each sub-handler `vtable + 0x08` validate candidate

Latest correction from reverse notes:

- `sub_140C15E00` calls `handler[1] vtable+0x08`, which is currently `RVA 0x8A7CA0`
- that function is the handler's main processing entry, not the final child validate routine
- inside that function, a later helper walks `handler + 24` and calls each sub-handler's own `vtable+0x08`
- therefore the next reliable reverse targets are the sub-handler validate candidates, not the handler main entry itself

Current best interpretation:

- `RVA 0x8A7CA0` is still useful, but only as the parent dispatcher/owner of the sub-handler list
- the real per-record reject gate is more likely one of the sub-handler validate virtuals
- the next reverse step should therefore focus on:
  - enumerating `handler + 24`
  - logging each sub-handler validate RVA
  - then reversing the first candidate(s) rather than continuing to instrument the parent handler as if it were the final gate

Latest sub-handler enumeration result:

- `handler[1]` current primary function is confirmed as:
  - `RVA 0x8A7CA0`
- `handler[1] + 24` currently exposes 8 child handlers
- latest runtime list:
  - `SubHandler[0] validate RVA 0x8AAB70`
  - `SubHandler[1] validate RVA 0x8AABD0`
  - `SubHandler[2] validate RVA 0x8AABA0`
  - `SubHandler[3] validate RVA 0x8AAE60`
  - `SubHandler[4] validate RVA 0x8AACF0`
  - `SubHandler[5] validate RVA 0x8AAC90`
  - `SubHandler[6] validate RVA 0x8AAEE0`
  - `SubHandler[7] validate RVA 0x8AD7C0`

What this proves:

- the earlier single-object hook on `0x8A7CA0` was aimed at the parent handler entry, not the final validate gate
- the next reverse step should target the child handler validate virtuals listed above
- the runtime event records reaching `sub_140C15E00` remain stable for both:
  - digital-style controls such as `Accept` / `Down` with `field0C = 0`
  - analog-style controls such as `Move` with `field0C = 3`

Immediate reverse recommendation:

- treat `0x8A7CA0` as the owner/dispatcher
- reverse the child validate candidates first, not the parent entry
- use the event `field0C` split (`0` vs `3`) to identify which child handlers are likely digital vs analog gates

Current proof target from latest reverse notes:

- verify whether the dispatch record itself is a real native-style C++ event object
- log for each first-seen record:
  - `event + 0x00` as a vtable candidate
  - `vtable + 0x10` / virtual slot 2 target
  - the return value of that virtual call
  - the compare target from `qword_142F25250 + 696`
- purpose:
  - confirm whether the child validate path is rejecting synthetic events because their event-object source identity does not match the expected global source object

Latest event-object structure result:

- the dispatch record is now confirmed to be a real C++ object-like event record:
  - `event + 0x00` is a stable vtable pointer
  - `vtable + 0x10` resolves to `RVA 0x70A0F0`
- calling that virtual slot succeeds for both:
  - digital records (`field0C = 0`, such as `Accept`, `Down`)
  - analog records (`field0C = 3`, such as `Move`)
- however, the returned source pointer does NOT match the compare target from:
  - `qword_142F25250 + 696`

Observed mismatch examples:

- digital `Accept` record:
  - `EventVfunc[2] returned = 0x7FF6E4F8C2B8`
  - `expected(+696)        = 0x255208E1FB8`
  - `matchExpected         = false`
- analog `Move` record:
  - `EventVfunc[2] returned = 0x7FF6E4F8C568`
  - `expected(+696)        = 0x255208E1FB8`
  - `matchExpected         = false`

What this proves:

- the current synthetic records do NOT fail because they lack a vtable
- the current stronger hypothesis is that the child validate path rejects them because the event-source identity returned by `vtable + 0x10` does not match the expected global source object
- this moves the reverse target from:
  - "is the record malformed?"
  to:
  - "what should `vfunc[2]` return for native-accepted records, and where does that source object come from?"

Latest correction from reverse notes:

- the new working hypothesis is narrower:
  - `RVA 0x70A0F0` may return the address of `event + 0x18`
  - not the final source object value itself
- that means the previous log line:
  - `EventVfunc[2] returned ... matchExpected=false`
  may have compared the field address against the expected source object value
- the current live instrumentation therefore logs both:
  - the pointer returned by `vfunc[2]`
  - the qword stored at `event + 0x18`

Current live validation switch:

- `test_keyboard_event_source_patch = true|false`
- when enabled, the `sub_140C15E00` call-site hook overwrites `event + 0x18` with the expected source object from:
  - `qword_142F25250 + 696`
- this is a reverse-engineering validation only, not a production fix
- rollback remains:
  - `test_keyboard_event_source_patch = false`

Latest falsification from live runtime:

- the `event + 0x18` overwrite test is now proven unsafe in its current form
- when the test was enabled, runtime logs showed valid translated events being rewritten from:
  - `Accept` -> `Click`
  - `Journal` -> `Click`
  - `Left Attack/Block` -> `Click`
- this immediately broke controller button behavior in game
- current best interpretation:
  - `RVA 0x70A0F0` returns the address of `event + 0x18`
  - but `qword_142F25250 + 696` is NOT a drop-in replacement for the value stored in that field
- therefore the present source-patch hypothesis is falsified:
  - do not treat `event + 0x18` as a plain source-object slot
  - do not use `qword_142F25250 + 696` as a direct overwrite value
- live config has been rolled back to:
  - `test_keyboard_event_source_patch = false`

Current safe next probe:

- stop patching dispatch record fields directly
- instead probe the first child validate gate with a single-object shadow-vtable hook
- current target:
  - `SubHandler[0] validate RVA 0x8AAB70`
- probe goal:
  - log the real second argument received by the child validate function
  - compare it against the `sub_140C15E00` entry head
  - confirm whether the child validate is seeing:
    - the same translated event object
    - a wrapper/pointer-to-pointer
    - or a different transformed structure entirely
- this is intentionally scoped to one runtime sub-handler object to avoid:
  - global code patching
  - unstable trampoline detours
  - more semantic corruption like the failed `event + 0x18` overwrite test

Latest confirmation from the scoped child-validate probe:

- the first child validate gate (`SubHandler[0] validate RVA 0x8AAB70`) now receives the same translated event object that `sub_140C15E00` sees at entry
- runtime proof:
  - `ChildValidateSource BEFORE ... arg2MatchesDispatchHead=true`
  - the logged `arg2` fields exactly match the current `sub_140C15E00` head
- therefore the current failure is no longer explained by:
  - a wrong validate hook signature
  - a wrapper/pointer-to-pointer mismatch
  - the child validate seeing a different transformed object than dispatch entry
- current best interpretation:
  - `0x8AAB70` is a real reject gate
  - it is rejecting the actual translated event object directly
  - the next useful check is whether it rejects because `arg2 + 0x18` (source/control descriptor slot) does not match the expected global compare value

Latest refinement from the same child-validate probe:

- `0x8AAB70` does not see only one event shape
- live logs now show it receiving at least two distinct object forms:
  - translated control events:
    - `Accept` / `Down`
    - `+00 = 0x7FF6E368E8D8`
    - `+08 = 2`
    - `+0C = 0`
    - `+18 = "Accept"` / `"Down"`
    - `+20 = 0x1000` / `0x2`
  - analog translated control events:
    - `Move`
    - `+00 = 0x7FF6E36DAD00`
    - `+08 = 2`
    - `+0C = 3`
    - `+18 = "Move"`
    - `+20 = 0xB`
  - raw keyboard-style event objects:
    - `+00 = 0x7FF6E368E8D8`
    - `+08 = 0`
    - `+0C = 0`
    - `+18 = "empty"`
    - `+20 = 0x38`
    - `+10` points at the translated `Move` record
- all of these currently still return:
  - `ChildValidateSource AFTER result=false`

What this changes:

- the problem is no longer "only translated digital records are rejected"
- the child validate path appears to walk or revisit both:
  - raw keyboard-origin event objects
  - translated control-event objects
- this means the next reverse target must explain:
  - how `0x8AAB70` distinguishes these object families
  - whether `+08/+0C` are family/category fields
  - whether the raw-object `next -> translated-object` linkage is expected native behavior

Latest proven comparison semantics for `0x8AAB70`:

- the child validate check is now effectively confirmed from live runtime:
  - `slot = event->vtable[2](event)`
  - `value = *slot`
  - `expected = *(qword_142F25250 + 696)`
  - validate returns false when `value != expected`
- current instrumentation proved:
  - `vtable[2]` returns the address of `event + 0x18`
  - not the final compare value itself
  - the dereferenced slot value is the same qword already stored at `event + 0x18`
- concrete live examples:
  - `Accept`:
    - `slot = 0x7FF6E4F8C2B8`
    - `*slot = 0x1C5800081B8`
    - `expected = 0x1C5808E1FB8`
    - `match = false`
  - `Move`:
    - `slot = 0x7FF6E4F8C568`
    - `*slot = 0x1C580008318`
    - `expected = 0x1C5808E1FB8`
    - `match = false`

What this now proves:

- the previous ambiguity is gone:
  - `0x8AAB70` is not comparing the returned slot address
  - it is comparing the qword stored in that slot
- the current synthetic failure is therefore specifically:
  - `*(event + 0x18) != *(qword_142F25250 + 696)`
- this does not yet prove what the correct value at `event + 0x18` should be for native-accepted keyboard events
- but it does prove the exact comparison shape that the next reverse step must explain

Latest runtime result from probing all child validates:

- the previous hypothesis:
  - "the first child only handles Click, later children may accept Accept/Down/Move"
  is now falsified by live instrumentation
- all 8 child validate candidates in `handler[1] + 24` are now confirmed to be hit for the same synthetic records:
  - `0x8AAB70`
  - `0x8AABD0`
  - `0x8AABA0`
  - `0x8AAE60`
  - `0x8AACF0`
  - `0x8AAC90`
  - `0x8AAEE0`
  - `0x8AD7C0`
- for translated digital records such as `Accept` and `Down`:
  - every child validate returns `false`
- for translated analog records such as `Move`:
  - every child validate also returns `false`
- the same mismatch is logged for all of them:
  - `slot = event->vtable[2](event)`
  - `value = *slot`
  - `expected = *(qword_142F25250 + 696)`
  - `match = false`

What this proves:

- the reject condition is not specific to only one sub-handler
- the whole child-handler family currently agrees that the synthetic event source/descriptor identity is wrong
- the next reverse target is therefore no longer:
  - "which later child accepts Accept/Down/Move?"
  but:
  - "where does the native path obtain the expected object at `qword_142F25250 + 696`, and why do all child validators compare against it?"

Latest control-slot labeling verification:

- runtime labeling of `qword_142F25250` now confirms these slots:
  - `Move` at `+0x28`
  - `Accept` at `+0x128`
  - `Down` at `+0x140`
  - `Click` at `+0x2B8`
- live comparison logs now show:
  - translated `Accept` records carry `event+0x18 = Accept(+0x128)`
  - translated `Move` records carry `event+0x18 = Move(+0x28)`
  - all child validators still compare against `expected = Click(+0x2B8)`
  - therefore the mismatch is no longer abstract; it is specifically:
    - `Accept(+0x128)` vs `Click(+0x2B8)`
    - `Move(+0x28)` vs `Click(+0x2B8)`
- this strongly suggests the current synthetic path is entering the wrong handler family or missing an earlier native routing step that would switch the expected compare target away from `Click`

Latest `sub_140C11600` field18 verification:

- the newest narrow preprocess probe confirms the event descriptor is created as the shared empty-string object before `sub_140C11600`
- live sample:
  - `emptyDescriptor = 0x1E680009F38 ("empty")`
  - before preprocess:
    - `code=0x1000` had `field18 = emptyDescriptor`
    - `code=0xB` had `field18 = emptyDescriptor`
- after `sub_140C11600`:
  - `code=0x1000` becomes `Accept(+0x128)`
  - `code=0xB` becomes `Move(+0x28)`
- therefore the current failure is not:
  - "sub_140C11600 was not called"
  - "sub_140C11600 failed to translate field18"
- the remaining failure is later in the dispatch/handler family selection, not in preprocess descriptor resolution

Latest `Jump/Space(0x39)` preprocess-condition verification:

- a dedicated `JUMP CONDITION BEFORE/AFTER sub_140C11600` probe was added for raw `Space(0x39)` / Jump candidates
- older samples had suggested the synthetic Jump path could remain:
  - `code = 0x39`
  - `field18 = empty`
  after `sub_140C11600`
- that older conclusion is now superseded by a newer live sample captured on 2026-03-08 at 20:36:02 local time
- in that newer sample the chain is:
  - `InputLoopProcess[0]` / `RVA 0xC1A130` first produces a local `Space(0x39)` press and a manager-side raw-space record
  - `InputLoopProcess[1]`, `[2]`, and `[3]` still leave the manager record as raw-space with `field18 = empty`
  - immediately before `sub_140C11600`, the head and release node are still:
    - `code = 0x39`
    - `field18 = empty`
  - immediately after `sub_140C11600`, those same nodes become:
    - `code = 0x39`
    - `field18 = Jump(+0x2B8)`
  - `sub_140C15E00` then receives the translated `Jump` chain, not a raw `Space`
- concrete log evidence from that run:
  - before preprocess: `FIELD18 BEFORE sub_140C11600` and `RAW SPACE CONDITION BEFORE sub_140C11600`
  - after preprocess: `PreprocessEvent[0] ... field18=Jump`, `RAW SPACE CONDITION AFTER sub_140C11600 ... field18=Jump`
  - dispatch entry: `sub_140C15E00 ENTRY` shows `name=Jump`, `code=0x39`
- therefore the current authoritative conclusion is:
  - for the latest observed synthetic Jump sample, the raw `Space -> Jump` descriptor upgrade does happen across the `sub_140C11600` boundary
  - the old statement "`synthetic Space -> Jump` is definitely not upgraded by `sub_140C11600`" should no longer be treated as reliable
  - the next reverse step should move from "is the upgrade earlier than preprocess?" to "which helper inside or immediately under `sub_140C11600` performs the `Space -> Jump` upgrade, and why does later handler-family routing still fail?"
- a follow-up split probe was then added to distinguish:
  - `RAW SPACE CONDITION BEFORE/AFTER sub_140C11600`
  - `TRANSLATED JUMP CONDITION BEFORE/AFTER sub_140C11600`
- the latest logs still capture `RAW SPACE` samples at this hook site, but at least one fresh sample now also proves the post-preprocess state becomes translated `Jump`
- so the current ambiguity is no longer whether preprocess can ever upgrade `Space(0x39)`
- the current ambiguity is narrower:
  - whether multiple `Space(0x39)` paths exist
  - whether some samples still bypass the successful Jump translation path
  - or whether the earlier empty-after-preprocess result was a logging aperture artifact around the same mutable record chain

Attempted `sub_140C150B0` entry probe:

- a minimal entry probe was attempted to answer the next branch question:
  - whether native translated `Jump` is already present at `sub_140C150B0` entry
  - or whether the upgrade happens inside `sub_140C150B0` before `sub_140C11600`
- live startup logs showed:
  - `sub_140C150B0 entry probe verification failed at 0x...C150B0`
- no `sub_140C150B0 ENTRY RAW SPACE` / `TRANSLATED JUMP` runtime samples were captured
- therefore this branch question remains open:
  - the experiment did not falsify the direction
  - but it also did not produce usable runtime evidence yet

Latest parent-handler routing verification:

- `sub_140C15E00` sees a parent handler array with `count=5`
- runtime parent handlers observed:
  - `Handler[0]` is external / non-game RVA
  - `Handler[1] primaryRva = 0x8A7CA0`
  - `Handler[2] primaryRva = 0x704DE0`
  - `Handler[3]` is external / non-game RVA
  - `Handler[4] primaryRva = 0x5A4EF0`
- live parent-handler probes confirm:
  - `Handler[1]`, `Handler[2]`, `Handler[3]`, and `Handler[4]` are all called for the same synthetic dispatch pass
  - all of them return `false`
- important runtime observation:
  - parent handler `arg2` does **not** equal the dispatch head
  - therefore the parent handlers are not receiving the raw event-chain head directly
  - they receive another wrapper / dispatch context object first

What this proves:

- the previous simplified model:
  - "sub_140C15E00 just passes the event head directly into one handler family"
  is incomplete
- the current failure is not:
  - "only one parent handler exists"
  - "only handler[1] runs"
- instead:
  - multiple parent handlers are entered
  - all of them reject on the synthetic path
- and their shared `arg2` is some wrapper object that must be understood before the correct handler-family routing can be fixed

Latest `sub_140C15E00` event field summary verification:

- a narrow runtime summary now confirms the translated event-field triple at the `sub_140C15E00` entry/exit boundary
- observed stable digital records:
  - `Accept`:
    - `field08 = 2`
    - `field0C = 0`
    - `code = 0x1000`
  - `Down`:
    - `field08 = 2`
    - `field0C = 0`
    - `code = 0x2`
  - `Right Attack/Block`:
    - `field08 = 2`
    - `field0C = 0`
    - `code = 0xA`
- observed stable analog records:
  - `Move`:
    - `field08 = 2`
    - `field0C = 3`
    - `code = 0xB`
  - `Look`:
    - `field08 = 2`
    - `field0C = 3`
    - `code = 0xC`
- for these samples, the `sub_140C15E00` entry and exit summaries are identical:
  - `field08 / field0C / code` are not rewritten inside `sub_140C15E00`
  - the current translated record shape is therefore already stable before parent-handler dispatch begins

What this proves:

- the current failure is not caused by `sub_140C15E00` mutating the translated event-family triple
- the effective grouping now looks like:
  - digital controls: `field08 = 2`, `field0C = 0`
  - analog controls: `field08 = 2`, `field0C = 3`
- therefore the next reverse target remains the parent-handler wrapper/dispatch-context path, not another attempt to reinterpret the translated event triple itself

Latest global-object relation verification setup:

- a new runtime-only investigation hook now logs the relationship between:
  - `qword_142F25250`
  - `qword_142F50B28`
- the probe is intentionally attached to real native keyboard activity instead of a detached sleep thread:
  - `BEFORE native keyboard processing`
  - `AFTER native keyboard processing`
- each pass logs:
  - slot addresses
  - pointed object addresses
  - whether both globals point to the same object
  - a compact object-field snapshot
  - `eventCount / queueHead / queueTail`
  - queue contents (if non-null)

Why this matters:

- it directly tests whether the compare-family object (`qword_142F25250`) and the input-manager object (`qword_142F50B28`) are the same runtime object or two different objects
- it also directly shows which queue, if any, receives native keyboard events on the accepted native path

Latest global-object relation verification result:

- live runtime logs now confirm:
  - `qword_142F25250` points to `0x7FF6E4F8BAD0`
  - `qword_142F50B28` points to `0x7FF6E4F8C280`
  - `sameObject=false`
- therefore the compare-family global and the input-manager global are definitively two different objects in the live SE 1.5.97 runtime

Queue observations from the same probe:

- `qword_142F25250`-side object:
  - `eventCount=0`
  - `queueHead=0x50000`
  - `queueTail=0x0`
  - following `queueHead` does not decode into a valid readable event record
- `qword_142F50B28`-side object:
  - before native keyboard processing: usually `eventCount=0`, `queueHead=0`, `queueTail=0`
  - after native keyboard processing: `eventCount` can become `2`, while `queueHead` and `queueTail` still remain `0`

What this proves:

- native keyboard activity is reflected on the `qword_142F50B28` side, not on the `qword_142F25250` side
- the old idea that both globals might be the same object is now falsified
- the old idea that the accepted native keyboard path is simply a linked queue on `qword_142F50B28 + 0x896/+0x904` is also not supported by runtime evidence
- the compare-family object at `qword_142F25250` should be treated as a different routing/descriptor object family, not as the input-manager event container

Immediate implication:

- synthetic keyboard events should not be redirected into a hypothetical `qword_142F25250` queue
- the active native event accounting path still appears to originate from `qword_142F50B28`
- but the accepted downstream compare-family/handler routing still depends on the separate `qword_142F25250` object graph

Latest Click-family state-byte verification:

- a new narrow runtime probe logged `byte_142F4E650` before and after:
  - every parent handler call
  - every child validate call
- this byte stayed `0` throughout the observed synthetic dispatch passes:
  - `ParentHandlerProcess[...] ... clickStateBefore=0 ... clickStateAfter=0`
  - `ChildValidateSource[...] ... clickStateBefore=0 ... clickStateAfter=0`
- the synthetic translated records at the same time were still:
  - `Accept(+0x128)` for digital accept
  - `Down(+0x140)` for digital down
  - `Move(+0x28)` for analog move
- yet all child validators still compared against:
  - `expected = Click(+0x2B8)`
  - and returned `false`

What this proves:

- the latest hypothesis from `agents3.md`:
  - "the synthetic path must have entered the Click-specific branch because the Click-family state byte was set"
  is not supported by runtime evidence
- in the observed live path, the compare against `Click(+0x2B8)` happens while:
  - `field08` on the translated events remains `2`
  - `byte_142F4E650` remains `0`
- therefore the current `Click`-target mismatch is not explained by a simple:
  - `field08 == 1`
  - or
  - `byte_142F4E650 != 0`
  branch at the parent/child handler layer
- the remaining problem is still later/finer-grained:
  - either a wrapper/dispatch-context field selects the compare-family independently
  - or the child-handler family is using another routing token that has not yet been identified

## Immediate Test Focus

When enabled, test these first:

- `Sprint`
- `Activate`
- `Jump`
- menu `Up` / `Down`
- `Accept` / `Cancel`
- `Console` open with text-entry safety

Watch for:

- swallowed key transitions
- held-key interruption
- repeated menu navigation behavior
- any accidental visible character input

Latest current-event global verification (`qword_142F4E648`):

- a new narrow runtime probe compared:
  - parent-handler wrapper head (`*arg2`)
  - `currentHead` seen at `sub_140C15E00`
  - `qword_142F4E648`
- live results now show a consistent sequence:
  - before the first parent handlers run, `qword_142F4E648` can still be `0`
  - once the main internal parent handler (`RVA 0x8A7CA0`) begins its child-validation pass, `qword_142F4E648` becomes equal to the current translated event head
  - all child validators then see:
    - `globalMatchesDispatchHead=true`
    - while `arg2` is still the event object itself, not the global slot address
- therefore:
  - the runtime reverse note "`qword_142F4E648` stores the current event before child validation" is supported
  - but the stronger hypothesis "`qword_142F4E648` is missing or wrong, therefore synthetic validation fails" is falsified

What this proves:

- synthetic rejection is not caused by `qword_142F4E648` failing to track the current event
- the current-event global is populated in time for child validation
- the remaining failure still lies in:
  - wrapper / dispatch-context routing
  - compare-family selection
  - or another later-stage validation token

Latest `sub_140C150B0` entry probe result:

- a direct entry probe was successfully installed at the live runtime input loop address:
  - `0x7FF6E2C350B0`
- the probe no longer crashes after correcting the patch width to 5 bytes
- however, it still produced no runtime `ENTRY RAW SPACE` / `ENTRY TRANSLATED JUMP` logs

Why it produced no event logs:

- the current implementation classifies the event shape by first reading:
  - `qword_142F50B28 + 0x896`
- at `sub_140C150B0` entry this field is still `0`
- therefore the probe returns before it can see either a raw `Space(0x39)` event or a translated `Jump(+0x2B8)` event

What this proves:

- using `qword_142F50B28 + 0x896` as the source of truth is too early at `sub_140C150B0` entry
- the failed observation does **not** falsify the current main-line hypothesis
- it only means the entry probe must look at a different early-stage carrier than `manager+0x896`

Immediate implication:

- do not repeat the same `sub_140C150B0` entry experiment with the same `manager+0x896` dependency
- the next useful step should inspect the early virtual-call loop inside the live runtime `sub_140C150B0` body, or another earlier-stage carrier that exists before `manager+0x896` is populated

Latest early-process probe direction:

- the `sub_140C150B0` entry hook is now reduced to one job:
  - inspect `rcx + 0x60`
  - install shadow-vtable probes on the 4 runtime objects processed by the early
    `call qword ptr [rax+0x10]` loop
- this avoids patching the 3-byte indirect call directly
- the new goal is to identify which early process call first makes manager-side
  state show:
  - raw `Space(0x39)`
  - translated `Jump(+0x2B8)`
  - or the first divergence between native and synthetic Jump paths

First live result from early-process probes:

- 4 runtime objects were identified from the `sub_140C150B0` early virtual-call loop:
  - slot 0 -> `RVA 0xC1A130`
  - slot 1 -> `RVA 0xC1A8C0`
  - slot 2 -> `RVA 0xC198B0`
  - slot 3 -> `RVA 0xC1A6A0`
- the initial version of the probe logged too eagerly and exhausted its budget before
  Jump-specific activity happened
- the only stable structural effect seen in those early idle frames was:
  - slot 2 (`RVA 0xC198B0`) can raise manager `eventCount` from `0` to `1`
  - but without producing a visible Jump/Space candidate in the observed manager-side carrier
- therefore the probe was narrowed so it now logs only when a manager-side
  `Space(0x39)` / `Jump` candidate is actually present

## Current Authoritative Boundary

The SKSE-side keyboard-native reverse path has now answered its main question:

- the stable success boundary is **not** inside `DualPad.dll`'s old
  `BSWin32KeyboardDevice::Poll`/`GetDeviceData` call-site hooks
- the stable success boundary is the wrapped keyboard
  `IDirectInputDevice8A::GetDeviceData` return edge inside the `dinput8.dll`
  proxy

What is now proven by runtime logs:

- remapping a real native `F10 (0x44)` record to synthetic `Space (0x39)` at the
  `dinput8` return boundary succeeds
- deferring that remapped `0x39` to the next empty `GetDeviceData` call still
  succeeds
- a **pure synthetic** `0x39` down/up pair generated from scratch at the same
  `dinput8` boundary also succeeds, even with:
  - `timeStamp = 0`
  - `sequence = 0`
  - `appData = 0`

Implication:

- unresolved provenance was never "missing hidden state after SKSE worker
  return"
- the required "native-enough" boundary is simply earlier than the old SKSE
  worker path
- formal keyboard-native implementation should therefore move to the `dinput8`
  proxy layer

## Formalization Skeleton

Current implementation direction is now:

- keep `KeyboardNativeBackend::PulseAction/QueueAction` as the producer API in
  `DualPad.dll`
- add a thin same-process shared bridge between `DualPad.dll` and the
  `dinput8.dll` proxy
- let the proxy consume keyboard commands and emit synthetic
  `DIDEVICEOBJECTDATA` at `GetDeviceData` return time
- keep the old SKSE call-site route as fallback until the proxy bridge is fully
  validated for all target actions

Bridge rules in the current skeleton:

- producer commands:
  - `Press`
  - `Release`
  - `Pulse`
  - `Reset`
- the bridge uses a named shared mapping plus named mutex, scoped by current
  process id
- `DualPad.dll` only uses the bridge when the proxy consumer heartbeat is
  present
- otherwise `KeyboardNativeBackend` falls back to the old in-plugin staging
  arrays
- the proxy keeps its own local latched state and pending synthetic queue
  before appending records to the real `GetDeviceData` output buffer

This means the next work item is no longer reverse engineering. It is product
integration and validation of the bridge-backed `dinput8` path.

## Revised With Route B Active

Later mixed live tests changed the conclusion above. Once the official gamepad
Route B poll path is active in gameplay, keyboard `Jump` fails again even when
the `dinput8` proxy path is made "as native as possible".

What is now explicitly ruled out on the proxy side:

- bridge `Pulse` staging vs legacy pending staging
- same-call `down+up` vs split-call `down` then `up`
- missing `GetDeviceState[0x39]` mirror
- missing `timeStamp/sequence/appData`

This was verified by live runs where:

- `GetDeviceState` already showed `focus[0x39]=0x80`
- `GetDeviceData` still emitted `0x39` down/up
- Skyrim-side preprocess still stayed:
  - `recordKind=raw-space`
  - `translatedJumpNodes=0`
  - `lateSeen=true`

Additional important live observation:

- during gameplay, a **real physical Space key** only succeeds reliably when
  the in-game "gamepad enabled" setting is turned off
- with the in-game "gamepad enabled" setting left on, even a **real physical
  Space key** can fail the same way during gameplay

Implication:

- the remaining blocker is no longer the `dinput8` proxy boundary
- the remaining blocker is a **game-side gate** tied to the game's active
  gamepad family / "gamepad enabled" state, not just to DualPad's Route B hook
- this means the previous wording "while Route B is active" was too narrow; the
  stronger live fact is "while the game is running in gamepad-enabled mode"
- therefore the active research target has moved back into game-side keyboard
  processing, but now with a much narrower question:
  - where does the game first preserve the failing `raw-space/empty` shape under
    gamepad-enabled mode?

Current narrow probe points:

- `AFTER InputLoopProcess[0]`
- `BEFORE sub_140C11600`
- `AFTER sub_140C11600`

Current live result:

- the newly captured **success baseline** ("gamepad disabled" + real physical
  `Space`) shows:
  - `AFTER InputLoopProcess[0]` is still `raw-space + empty`
  - `BEFORE sub_140C11600` is still `raw-space + empty`
  - `AFTER sub_140C11600` becomes translated `Jump`
- the failing baseline ("gamepad enabled" + real physical `Space`) shows:
  - `AFTER InputLoopProcess[0]` is `raw-space + empty`
  - `BEFORE sub_140C11600` is still `raw-space + empty`
  - `AFTER sub_140C11600` remains `raw-space + empty`
- therefore the success/failure split is **not** visible at
  `InputLoopProcess[0]`; the first confirmed visible split is again across the
  `sub_140C11600` boundary

So the current mainline question is no longer "is proxy injection early enough?"
but instead:

- what game-side gate, active while the game is running in gamepad-enabled
  mode, prevents `sub_140C11600` from upgrading native keyboard `Space` to
  translated `Jump`?

## Final Coexistence Finding

The final verified coexistence fix is now documented separately in:

- [keyboard_native_coexistence_summary_zh.md](/c:/Users/xuany/Documents/dualPad/docs/keyboard_native_coexistence_summary_zh.md)
- [keyboard_native_second_family_summary_zh.md](/c:/Users/xuany/Documents/dualPad/docs/keyboard_native_second_family_summary_zh.md)

Short version:

- `dinput8.dll` proxy is the formal keyboard-native emit boundary
- the decisive game-side coexistence gate is `gateObj + 0x121` inside
  `sub_140C11600`
- a scoped `+0x121 -> 0` override during `sub_140C11600` restores
  `Space -> Jump` while preserving gamepad analog input

## Second-Family Revision

Later black-box and static analysis split the remaining keyboard-native actions
into a second family:

- `Activate`
- `Sprint`
- `Sneak`

The decisive live fact is:

- with in-game gamepad enabled, even the **real physical keyboard** keys
  `E`, `L-Ctrl`, and `L-Alt` fail

Therefore the second-family blocker is no longer treated as a proxy/provenance
problem. Current static analysis indicates these keys are being routed through
the generic keyboard/interface path:

- `sub_1408A7CA0`
- `sub_1408A85C0`
- `sub_1408A8650`
- `sub_140EDA8B0`
- `sub_140EC3ED0`

rather than the gameplay action translation path used by `Jump`.

The dedicated Chinese summary for this branch is:

- [keyboard_native_second_family_summary_zh.md](/c:/Users/xuany/Documents/dualPad/docs/keyboard_native_second_family_summary_zh.md)

Current highest-confidence second-family interpretation:

- `Activate / Sprint / Sneak` are not following the `Jump` gameplay translation path
- they are being routed into the generic keyboard / interface key pipeline:
  - `sub_140C190F0`
  - `sub_140C16900`
  - `sub_1408A7CA0`
  - `sub_1408A85C0`
  - `sub_1408A8650`
  - `sub_140EDA8B0`
  - `sub_140EC3ED0`
- black-box validation matches this:
  - with in-game gamepad enabled, even physical `E / L-Ctrl / L-Alt` fail

Latest family-count refinement:

- At the reverse-engineering action level, two high-confidence families are now
  treated as confirmed:
  - `Jump` family:
    - `Game.Jump`
  - second family:
    - `Game.Activate`
    - `Game.Sprint`
    - `Game.Sneak`
- At the owner/tree level, `qword_142F257A8` now has two confirmed registered roots:
  - gameplay root (`sub_140704DE0`), with 13 confirmed handlers:
    - `Movement`
    - `Look`
    - `Sprint`
    - `ReadyWeapon`
    - `AutoMove`
    - `ToggleRun`
    - `Activate`
    - `Jump`
    - `Shout`
    - `AttackBlock`
    - `Run`
    - `Sneak`
    - `TogglePOV`
  - interface/generic root (`sub_1408A7CA0`), with 7 confirmed handlers:
    - `Click`
    - `Direction`
    - `ConsoleOpen`
    - `QuickSaveLoad`
    - `MenuOpen`
    - `Favorites`
    - `Screenshot`
- gameplay root also keeps a confirmed 5-member secondary list:
  - `Activate`
  - `Sprint`
  - `AttackBlock`
  - `Run`
  - `TogglePOV`
- This secondary list does not explain the second-family grouping by itself:
  - `Sneak` is not in the secondary list
  - but still exhibits the same second-family failure shape as
    `Activate / Sprint`

So the second-family investigation is now treated as a game-side mode/routing
problem, not a `dinput8` provenance problem.

Latest static refinement:

- `sub_1408A85C0 -> sub_1408A8650` is now treated as the generic/interface
  keyboard translation tree, not a gameplay action translation path.
- `sub_140C151C0` goes through `BSWin32KeyboardDevice::vfunc+0x30 = sub_140C18BA0`,
  which returns a generic keycode from the keyboard device hash table.
- `sub_140C15180` (full descriptor lookup) currently appears on UI/Scaleform
  style paths, not the second-family gameplay path.
- `BSWin32VirtualKeyboardDevice`/`sub_140524180` currently looks more like a
  mode notification path than the second-family consumer.

Latest object-level refinement:

- `sub_1408A7A90` appears to construct the actual second-family
  interface/generic controls owner rooted at `qword_142F003F8`.
- Its main process method is `sub_1408A7CA0`, which is reached through the
  vtable rather than ordinary code calls.
- `sub_140C15280(qword_142F257A8)` is now treated as a total gamepad-delegate
  gate for this tree: it effectively tests whether
  `BSPCGamepadDeviceHandler` currently has a live delegate/device.
- `sub_1408A85C0` already branches on this gate, so the second-family problem
  is increasingly framed as an interface-tree routing issue driven by active
  gamepad delegate state.

Latest owner/handler refinement:

- `qword_142F257A8` dispatches registered sinks through `sub_1405A5270`, which
  iterates sinks in order and short-circuits on the first `vfunc[1]` returning
  `true`.
- `gameplay root` and `interface root` are both registered on this same owner:
  - gameplay root `vtable[1] = sub_140704DE0`
  - interface root `vtable[1] = sub_1408A7CA0`
- `sub_1407073B0` proves the gameplay tree does contain
  `SprintHandler / ActivateHandler / SneakHandler / JumpHandler`.
- The second-family problem is therefore not "missing gameplay handlers"; it is
  better modeled as "gameplay tree does not consume these events, then
  interface/generic tree still sees and routes them."
- Current gameplay validate slots identified statically:
  - `ActivateHandler` -> `qword_142F25250 + 0x38`
  - `JumpHandler` -> `qword_142F25250 + 0x78`
  - `SprintHandler` -> `qword_142F25250 + 0x88`
  - `SneakHandler` -> `qword_142F25250 + 0x90`
- Current interpretation after decompiling gameplay handler process methods:
  - `Sprint / Activate / Sneak` are not blocked by unusually complex gameplay
    semantics; their handlers mostly operate on straightforward press/release
    values (`event+0x28/+0x2C`) once descriptor validate succeeds.
  - So the dominant second-family blocker remains descriptor/routing mismatch
    before or at gameplay-tree validate, not the handlers' own action logic.
- gameplay root is dispatched through the shared owner just like interface root,
  but its root process `sub_140704DE0` currently returns `0`; therefore if its
  handlers do not consume a second-family event by side effect, the same event
  can continue on into the interface/generic root.
- `sub_140706AF0` is now treated as a broad gameplay-root allow gate, but not
  yet as the single second-family root cause; it depends on wider game state
  than a simple gamepad-enabled boolean.

Latest generic/interface refinement:

- `sub_140EDA8B0`, `sub_140EDA930`, and `sub_140EDA980` are generic/interface
  event-buffer append helpers, not gameplay action builders.
- `sub_140EC3ED0` forwards those generic/interface records to
  `qword_141EC0A78 + 208`.
- This strengthens the current model that `Activate / Sprint / Sneak` are
  being routed into the interface/generic key tree after gameplay validate
  misses, rather than failing because their gameplay handlers are inherently
  more complex than `Jump`.

Latest patch status:

- `Jump` keeps its existing `sub_140C11600` scoped coexistence patch.
- A first second-family coexistence patch is now implemented at gameplay
  validate level instead of owner/platform level.
- The patch is intentionally narrow:
  - `ActivateHandler::validate`
  - `SprintHandler::validate`
  - `SneakHandler::validate`
- Behavior:
  - original validate still runs first
  - only when original validate returns `false`
  - and the current event still matches the raw/transient second-family key
    (`E`, `L-Alt`, `L-Ctrl`)
  - the validate result is overridden to `true`
- Goal:
  - let gameplay root consume second-family keyboard-native events before the
    interface/generic root can take them
  - avoid global platform switching or broad owner-state patches
