# Sneak Toggle Anomaly Note

## Symptom

- `Sneak` currently does not toggle cleanly.
- In-game it looks like the toggle starts, the character enters the motion briefly, then immediately returns.

## Current Classification In Code

- [`C:\Users\xuany\.codex\worktrees\237f\dualPad\src\input\backend\NativeActionDescriptor.cpp`](C:\Users\xuany\.codex\worktrees\237f\dualPad\src\input\backend\NativeActionDescriptor.cpp)
- `Game.Sneak -> ActionOutputContract::Toggle`
- `Game.Sneak -> ActionLifecyclePolicy::ToggleOwner`

That means this issue belongs to the **toggle-family** side of gameplay ownership, not the `Sprint` / `HoldOwner` / `SingleEmitterHold` line.

## Current Interpretation

- This is more likely a `ToggleOwner` vs native Skyrim toggle semantics interaction issue.
- It should not be solved by reusing the `Sprint` hold/handoff experiments.
- It also should not be mixed into the current `LookOwner / MoveOwner / CombatOwner` tuning work.

## Deferred Investigation Plan

1. Verify whether `ToggleOwner` currently emits duplicate toggle semantics during mixed-device input.
2. Verify whether native Skyrim `Sneak` expects a different press/release pattern than our current synthetic toggle path.
3. Check whether the issue is caused by native + synthetic toggle both reaching the game, or by synthetic toggle being immediately canceled by a follow-up state transition.
4. Only after that decide whether `Sneak` needs a dedicated toggle-family mediation policy.

## Status

- Recorded for later investigation.
- No runtime logic change is made in this step.
