---
date: 2026-04-08
topic: dualpad-ce3-architecture
focus: dualpad architecture adjustments based on CE3 KBM/gamepad switching findings
---

# Ideation: DualPad CE3 Architecture Adjustments

## Codebase Context

DualPad's current mainline already has two different but still partially entangled chains:

- UI / presentation arbitration centered on `InputModalityTracker`
- gameplay materialization centered on `PadEventSnapshotProcessor -> NativeButtonCommitBackend -> PollCommitCoordinator -> AuthoritativePollState`

The current codebase and architecture docs already recognize that UI owner and gameplay owner should not be the same thing, but the runtime still has notable coupling:

- `InputModalityTracker` still owns too much:
  - platform hooks
  - menu presentation switching
  - cursor/platform policy
  - KBM gameplay fact collection
- gameplay ownership still depends on tracker-owned facts
- `IsUsingGamepad()` remains a cross-layer compatibility seam

The latest CE3 reverse-engineering work sharpened four constraints that are especially relevant to DualPad:

- CE3 keeps a high-resolution internal mode lane:
  - `KBM / generic gamepad / DualSense / fallback`
- CE3 publishes a separate collapsed external lane:
  - effectively `controller-family vs non-controller-family`
- `SetPlatform` and `UsingGamepad` consume the collapsed lane, not the full mode byte
- exact `mode == 1` consumers appear to live below the UI/query layer and behave more like DualSense-specific feature propagation than generic menu platform switching

This suggests DualPad's strongest improvement opportunities are structural, not patch-shaped:

- make the internal vs published state split explicit
- stop routing more meaning through `InputModalityTracker`
- separate presentation/publication concerns from gameplay and DualSense-specific capability concerns

## Ranked Ideas

### 1. Formalize Two Explicit State Lanes
**Description:** Introduce two first-class runtime states: `InternalInputFamilyState` and `PublishedPresentationState`. The internal state keeps high-resolution distinctions such as `KeyboardMouse`, `GenericGamepad`, `DualSense`, and `Fallback`. The published state collapses to the minimum surface needed by UI-facing consumers such as `KeyboardMouse` vs `Controller`.
**Rationale:** This is the closest direct transfer from the CE3 findings. It gives DualPad a clean answer to the current problem where `IsUsingGamepad`, glyph presentation, and future DualSense-specific behavior all compete for the same meaning slot. It also creates a defensible place to keep DS-specific truth without leaking it into generic menu/platform semantics.
**Downsides:** This change touches multiple boundaries at once: `InputModalityTracker`, compatibility hooks, menu refresh, and any code currently treating one boolean as the whole truth. It is conceptually simple but mechanically broad.
**Confidence:** 96%
**Complexity:** High
**Status:** Explored

### 2. Split `InputModalityTracker` into Focused Components
**Description:** Break `InputModalityTracker` into at least three parts: `PresentationArbiter`, `SkyrimCompatibilitySurface`, and `GameplayKbmFactTracker`. The first owns menu/platform/cursor-facing arbitration. The second owns engine-facing hooks like `IsUsingGamepad` and `GamepadControlsCursor`. The third owns gameplay KBM fact gathering only.
**Rationale:** The current tracker has become a structural choke point. As long as UI arbitration, hook compatibility, and gameplay KBM fact collection stay in one class, future changes will keep reintroducing cross-layer regressions. This split would reduce the largest single architecture debt in the project.
**Downsides:** Large migration surface. There will likely be a temporary period where both old and new seams coexist. Some current fixes may need to be rewritten rather than moved verbatim.
**Confidence:** 93%
**Complexity:** High
**Status:** Unexplored

### 3. Convert Gameplay Ownership into a One-Way Gate Plan
**Description:** Refactor `GameplayOwnershipCoordinator` so it only computes per-channel ownership plus a gate/cancel plan. `PadEventSnapshotProcessor` applies that plan in one place. Backends stop reading ownership as a global ambient truth.
**Rationale:** This aligns with DualPad's existing pipeline and addresses a specific architecture risk already documented in the repo: owner decision and execution lifecycle are too tightly coupled. It also matches the CE3 pattern of centralized recompute followed by controlled publication rather than ad hoc feedback loops.
**Downsides:** This will reopen validation for digital action suppression, Sprint behavior, and recovery interactions. The payoff is high, but the migration has to be disciplined or it can destabilize current behavior.
**Confidence:** 91%
**Complexity:** High
**Status:** Unexplored

### 4. Promote UI Arbitration into a Formal Policy Table
**Description:** Replace scattered menu/platform heuristics with explicit `PresentationPolicy`, `NavigationPolicy`, and `PointerPolicy` tables keyed by `InputContext` or menu family. Support modes such as `controller-sticky`, `pointer-first`, and `neutral`.
**Rationale:** The repo already has the right instinct in documentation, but the code still leans on a growing set of tactical conditions. CE3's behavior suggests that identical raw input should be interpreted differently depending on current family/context, which supports making policy an explicit design object.
**Downsides:** If this is attempted before state-lane separation and tracker decomposition, it risks becoming a nicer wrapper around the same entanglement.
**Confidence:** 84%
**Complexity:** Medium
**Status:** Unexplored

### 5. Introduce a Dedicated `DualSenseFeatureState`
**Description:** Create a separate state object such as `DualSenseFeatureState` or `DeviceCapabilitySnapshot` that carries DS-specific truth: for example `isDualSenseActive`, `transport`, `hapticsAvailable`, `adaptiveTriggerAvailable`, and future feature flags. This state is consumed only by DS-specific layers, never by `SetPlatform`-style publication paths.
**Rationale:** CE3 now appears to have a distinct exact-`mode == 1` propagation lane. DualPad currently lacks a clean formal equivalent. Adding one would prevent future DS feature work from overloading generic controller-family semantics.
**Downsides:** This is not the most urgent structural change for the current mainline. If introduced too early, it could create pressure to expand into haptics/trigger work before the ownership split is stable.
**Confidence:** 79%
**Complexity:** Medium
**Status:** Unexplored

### 6. Add Explicit Published/Dirty Semantics for Cross-Layer State
**Description:** Introduce explicit published/dirty or epoch-based publication rules for `PublishedPresentationState`, possibly `GameplayKbmFacts`, and future `DualSenseFeatureState`. Consumers only observe published state, not mutable internal state.
**Rationale:** The CE3 findings around published flags and dirty pairs suggest a useful discipline that DualPad currently lacks in a formal way. This would help stabilize refresh timing for menus, glyph updates, and future DS-specific refresh consumers.
**Downsides:** If overdone, this can become a generic event-bus abstraction that hides rather than clarifies ownership. It only pays off if kept narrow and tied to concrete publication seams.
**Confidence:** 82%
**Complexity:** Medium-High
**Status:** Unexplored

## Rejection Summary

| # | Idea | Reason Rejected |
|---|------|-----------------|
| 1 | Keep adding leases and special cases inside `InputModalityTracker` | Too local and reinforces the largest existing structural bottleneck. |
| 2 | Introduce one global `GameplayOwner` for all gameplay input | Not coherent with the existing per-channel pipeline and too coarse for the actual conflict shape. |
| 3 | Recreate CE3 `DeviceChangeEvent` end-to-end in DualPad | Too CE3-specific and lower-value than directly adopting the structural split behind it. |
| 4 | Pivot immediately into haptics/trigger implementation | Belongs to the next problem, not the current architecture bottleneck. |
| 5 | Fix menu issues by adding more menu-by-menu `SetPlatform` patches | Symptom-oriented and unlikely to scale. |
| 6 | Introduce a large unified state/event bus for everything | Too abstract, too expensive, and likely to relocate coupling rather than remove it. |

## Session Log

- 2026-04-08: Initial ideation - 12 candidate directions considered, 6 survived adversarial filtering.
- 2026-04-08: Idea 1 promoted into `docs/brainstorms/2026-04-08-input-kernel-and-projection-architecture-requirements.md` for deeper exploration.
