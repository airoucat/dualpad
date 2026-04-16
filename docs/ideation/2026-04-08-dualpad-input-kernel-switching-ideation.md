---
date: 2026-04-08
topic: dualpad-input-kernel-switching
focus: keyboard/gamepad switching design under input-kernel-and-projection architecture
---

# Ideation: DualPad Input Kernel Switching

## Codebase Context

The newly defined `Input Kernel + Projection` direction already establishes the architectural split:

- one internal high-resolution truth
- a collapsed presentation surface for UI/platform consumers
- a separate gameplay projection
- a reserved device-specific projection for future DualSense-only behavior

The remaining design question is not "whether to switch", but **how keyboard/gamepad switching should work inside that architecture**.

Current repo and CE3 findings together suggest six important constraints:

- `InputModalityTracker` still solves too many different problems at once
- gameplay conflicts are not the same as UI/platform switching conflicts
- CE3 keeps a high-resolution internal mode lane and a separate collapsed published lane
- `SetPlatform` and `UsingGamepad` consume the collapsed lane
- exact device-family detail stays below the UI/query layer
- CE3's strongest runtime switching clue is not ad hoc event toggling, but central recompute from per-poll source activity plus settings/policy gates

So a viable switching design for DualPad must answer these separately:

- how the kernel decides the current internal family
- what signals are strong enough to switch that family
- how UI/platform switching consumes the result
- how gameplay handoff consumes the same internal truth without turning gameplay into another global family switcher

## Ranked Ideas

### 1. Make Switching a Kernel-Owned Family Election
**Description:** Move all keyboard/gamepad switching into the `Input Kernel` as a family-election step that runs each frame from normalized source activity. The kernel publishes a high-resolution `InternalInputFamilyState` such as `KeyboardMouse`, `GenericGamepad`, `DualSense`, and `Fallback`, plus metadata like `dominantSource`, `enteredAt`, and `lastStrongSignalAt`.
**Rationale:** This is the cleanest transfer from the CE3 findings. It prevents switching from being reinterpreted independently by `InputModalityTracker`, gameplay ownership code, and backend-local logic. It also gives every projection a shared stable truth rather than a pile of tactical booleans.
**Downsides:** This is a large conceptual move and requires broad refactoring around current tracker semantics. It only works if the family-election inputs are narrow and explicit.
**Confidence:** 96%
**Complexity:** High
**Status:** Unexplored

### 2. Introduce a Strong / Weak / Auxiliary Signal Taxonomy
**Description:** Classify switching inputs into explicit signal classes. Strong signals can flip the internal family. Weak signals can sustain or influence projections without stealing family ownership. Auxiliary signals never count for switching.
**Rationale:** This directly addresses the most common failure mode in the current design: low-value input such as mouse motion being able to steal too much meaning. It also maps well to the CE3 structure, where not every event that exists at runtime is allowed to become a platform/family switch.
**Downsides:** The taxonomy needs discipline or it turns into another sprawling special-case table. Some signals will still be context-sensitive and need policy mediation.
**Confidence:** 94%
**Complexity:** Medium-High
**Status:** Unexplored

### 3. Treat UI Switching and Gameplay Handoff as Different Consumers of the Same Truth
**Description:** Use the same kernel-owned family truth for both domains, but make the projections consume it differently:
- `Presentation Projection` performs family switching
- `Gameplay Projection` performs per-channel handoff, not a global family flip
In this model, gameplay should not ask "did we switch to keyboard?" but "who currently owns look/move/combat/digital for this frame?"
**Rationale:** This is the most important conceptual correction for DualPad. A lot of current complexity comes from treating gameplay and UI as if they were fighting over the same switch. CE3 strongly suggests the opposite: published UI surfaces are collapsed, while lower layers retain richer semantics.
**Downsides:** It requires being explicit that some current "switching" bugs are actually ownership/handoff bugs, which means not every symptom gets solved in the same subsystem.
**Confidence:** 95%
**Complexity:** High
**Status:** Unexplored

### 4. Put Arbitration Policy in a Context Table, Not in Runtime Branch Forests
**Description:** Define explicit switching policies per context or menu family, such as `controller-sticky`, `pointer-first`, `neutral`, or `gameplay-look-biased`. The kernel produces raw family candidates and signal evidence; projections consult context policy to decide promotion, demotion, sustain, and grace windows.
**Rationale:** This is the most scalable way to keep switching understandable. It allows different menus or gameplay domains to interpret the same signal classes differently without hard-coding behavior into one tracker class. It also preserves a path for future tuning without turning the architecture back into hand-patched logic.
**Downsides:** If the policy matrix grows too freely, it can become a disguised patch list. This only works if the set of policies stays small and named by intent, not by menu accident.
**Confidence:** 88%
**Complexity:** Medium
**Status:** Unexplored

### 5. Publish Only Stable Switching Results Through Dirty/Epoch Semantics
**Description:** Add a publication step between internal family election and external surfaces. `PublishedPresentationState` should only update when the kernel's result crosses a stable switching boundary, and it should expose explicit dirty/epoch semantics so menus, glyphs, and hooks only react to committed changes.
**Rationale:** This matches the CE3 lesson that internal truth and outward publication are different things. It also gives DualPad a direct answer to churn problems: switching can be internally active without immediately forcing all consumers to react.
**Downsides:** This can become over-engineered if too many layers gain their own publish/diff systems. It should stay narrowly focused on outward-facing state surfaces.
**Confidence:** 86%
**Complexity:** Medium
**Status:** Unexplored

### 6. Keep DualSense as a Non-Stealing Subtype
**Description:** Model `DualSense` as a subtype of the internal controller family, with its own reserved feature lane, but explicitly forbid it from changing generic keyboard/gamepad switching semantics by itself. In other words: `DualSense` may change feature exposure, but it should not directly change how `SetPlatform` or generic `UsingGamepad`-style publication behaves.
**Rationale:** This is the cleanest way to absorb the CE3 finding that exact `mode == 1` matters below the UI layer while the published UI layer still collapses to controller-family. It gives DualPad a place to keep DS truth without corrupting the switching model.
**Downsides:** This is easy to agree with architecturally, but it can feel unsatisfying if people expect DS-specific behavior to show up everywhere immediately. The benefit is mostly defensive until DS-specific features are built out.
**Confidence:** 85%
**Complexity:** Medium
**Status:** Unexplored

## Rejection Summary

| # | Idea | Reason Rejected |
|---|------|-----------------|
| 1 | Keep switching logic inside `InputModalityTracker` and just clean it up | Duplicates the old architecture shape and preserves the same choke point. |
| 2 | Let mouse move continue to flip global family directly, just with better thresholds | Too brittle and still confuses pointer noise with family ownership. |
| 3 | Use one global owner for both UI and gameplay switching | Too coarse; gameplay and UI consume the same truth differently. |
| 4 | Copy CE3 `DeviceChangeEvent` semantics literally | Too engine-specific and lower-value than copying the structural separation behind it. |
| 5 | Let DualSense subtype directly affect published UI switching | Violates the strongest CE3 lesson and would pollute generic controller-family semantics. |
| 6 | Replace explicit policies with a generic weighted confidence/scoring engine | Interesting, but too opaque and hard to debug relative to a smaller named policy table. |

## Session Log

- 2026-04-08: Initial ideation - 10 candidate directions considered, 6 survived adversarial filtering.
