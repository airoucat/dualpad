---
date: 2026-04-08
topic: dualpad-gameplay-input-layer
focus: gameplay input-layer refactor suggestions based on current DualPad architecture and CE3 findings
---

# Ideation: DualPad Gameplay Input Layer Refactor

## Codebase Context

DualPad's gameplay input path is already structurally separate from the UI path on paper, but the current runtime still has meaningful cross-layer leakage.

The main gameplay materialization chain is:

- `PadEventSnapshotProcessor`
- `GameplayOwnershipCoordinator`
- `NativeButtonCommitBackend`
- `PollCommitCoordinator`
- `AxisProjection`
- `UnmanagedDigitalPublisher`
- `AuthoritativePollState`

The strongest current structural observations from the codebase are:

- `PadEventSnapshotProcessor` is already the natural convergence point for:
  - `SyntheticPadFrame`
  - `FrameActionPlan`
  - resync/recovery decisions
  - final poll-state publication
- `GameplayOwnershipCoordinator` is no longer just a gameplay arbiter:
  - it still contains gameplay presentation leases and published presentation state
- gameplay code still reads ambient global state through singletons:
  - `GameplayKbmFactTracker`
  - `InputModalityTracker`
  - runtime config
- `NativeButtonCommitBackend` still contains domain-specific suppression knowledge such as:
  - which digital policies are suppression candidates
  - special Sprint behavior
- `Sprint` is already documented as a different class of problem:
  - not "generic digital ownership"
  - but "single-emitter hold + native keyboard mediation"

The latest CE3 reverse-engineering work sharpened the architectural lesson:

- gameplay/device logic should keep a richer internal state lane
- UI/publication surfaces should consume a collapsed surface
- exact device-family detail, especially DualSense-only state, should live below the UI/platform layer

Applied back to DualPad, the strongest implication is:

- the gameplay injection layer needs its own explicit contract and its own clean seams
- not more tactical conditions threaded through `InputModalityTracker`, ownership hooks, and backends

## Ranked Ideas

### 1. Introduce a First-Class `GameplayInjectionPlan`
**Description:** Add a single explicit contract object between `PadEventSnapshotProcessor` and the materialization backends, for example `GameplayInjectionPlan` or `OwnedGameplayFrame`. It would carry per-channel analog ownership results, digital gate/cancel plans, recovery dispositions, unmanaged publish intent, and any poll-visible final state decisions for the current frame.
**Rationale:** Right now the gameplay path is conceptually linear but operationally fragmented. Ownership, recovery, digital suppression, and publication are spread across multiple modules that each re-read context. A first-class gameplay plan would turn the current implicit choreography into a one-way contract. This is the highest-leverage structural change because it can absorb several smaller debts at once.
**Downsides:** This is a wide rewrite and will touch several files at once. It also creates pressure to define the contract carefully enough that it simplifies rather than becoming another generic bag of fields.
**Confidence:** 95%
**Complexity:** High
**Status:** Unexplored

### 2. Reduce `GameplayOwnershipCoordinator` to a Pure Gameplay Arbiter
**Description:** Remove gameplay-presentation publication and lease logic from `GameplayOwnershipCoordinator`, leaving it responsible only for gameplay channel ownership and gate planning. Anything that exists to influence menu/platform behavior should move out to a separate bridge or be deleted if no longer justified.
**Rationale:** The current coordinator still carries `GetPublishedGameplayPresentationOwner`, `GetPublishedGameplayMenuEntryOwner`, presentation hints, and lease timing. That means the gameplay layer is still partly trying to solve UI/platform concerns. This is exactly the class of leakage that the CE3 findings argue against.
**Downsides:** Some current behavior that feels convenient may turn out to have been relying on this coupling. The migration will need a replacement seam for any genuinely necessary gameplay-to-presentation handoff.
**Confidence:** 93%
**Complexity:** High
**Status:** Unexplored

### 3. Replace Ambient Singleton Reads with Immutable Frame Inputs
**Description:** Create an explicit immutable input bundle passed through the gameplay pipeline each frame, for example `GameplayFrameInputs { context, contextEpoch, kbmFacts, internalInputFamilyState, runtimeFlags }`. `GameplayOwnershipCoordinator` and the backends should stop querying `InputModalityTracker` and other singleton state directly during decision-making.
**Rationale:** The current gameplay layer is harder to reason about because multiple modules pull in side information independently. A frozen per-frame input snapshot would make ownership decisions deterministic, easier to log, easier to test, and less likely to drift when one singleton changes semantics.
**Downsides:** Some current utility access patterns are convenient and fast to add to; those would need explicit plumbing. This raises short-term verbosity in exchange for long-term clarity.
**Confidence:** 91%
**Complexity:** Medium-High
**Status:** Unexplored

### 4. Make `Sprint` a Dedicated Mediation Subsystem
**Description:** Split `Sprint` fully out of the generic digital ownership family into a dedicated subsystem such as `SprintMediationCoordinator`, built around `SingleEmitterHold + native keyboard mediation`. Treat it as a handler-specific problem with explicit handoff semantics, not as just another gate-aware digital action.
**Rationale:** The repo's own Sprint plan already points this way, and the current code reflects the problem: generic digital suppression explicitly excludes the policies that matter most for Sprint. Continuing to hide Sprint inside broader digital-family rules will keep generating special cases. This is a good candidate for a deliberate carve-out.
**Downsides:** It is a targeted subsystem, which means it intentionally embraces special handling. It will likely require one more round of native input/queue investigation before implementation is safe.
**Confidence:** 90%
**Complexity:** High
**Status:** Unexplored

### 5. Promote Recovery/Resync into a Dedicated Pre-Planning State Machine
**Description:** Extract the current gap/coalesce/cross-context recovery logic into a named stage before gameplay planning, for example `DegradedFrameRecovery` or `InputRecoveryStateMachine`. Its output should be a normalized frame plus explicit recovery dispositions, rather than a set of helper calls interleaved through `PadEventSnapshotProcessor`.
**Rationale:** Input-to-game correctness is not just ownership. Missing presses, cross-context drift, and coalesced snapshots can corrupt the eventual gameplay-visible state even if ownership is correct. The current code already contains strong recovery ideas, but they are still embedded inside the processor. Formalizing that stage would simplify later gameplay refactors.
**Downsides:** This can become too abstract if it is designed before the actual transition cases are enumerated carefully. It should stay tightly scoped to currently observed degraded-delivery modes.
**Confidence:** 86%
**Complexity:** Medium-High
**Status:** Unexplored

### 6. Replace Ad Hoc Digital Suppression with Descriptor-Driven Action Families
**Description:** Introduce an explicit `DigitalActionFamily` classification in the descriptor/routing layer and drive digital gating from that family classification instead of hard-coded checks in `NativeButtonCommitBackend`. Families could include `TransientGameplay`, `SingleEmitterHold`, `RepeatManaged`, and similar categories.
**Rationale:** The current suppression logic is already signaling that the generic approach is breaking down: Sprint and similar actions are explicitly exempted because their semantics differ. Turning this into metadata would move behavior classification closer to descriptors and away from backend-local branching.
**Downsides:** This is less foundational than the top three ideas and should probably follow, not precede, a better gameplay contract. On its own it can become a taxonomy exercise without fixing the larger data-flow problem.
**Confidence:** 80%
**Complexity:** Medium
**Status:** Unexplored

## Rejection Summary

| # | Idea | Reason Rejected |
|---|------|-----------------|
| 1 | Keep improving gameplay behavior by adding more leases to `GameplayOwnershipCoordinator` | Reinforces the same mixed-responsibility design rather than cleaning the seam. |
| 2 | Introduce one global gameplay owner for all input | Too coarse for the actual conflict shape; loses per-channel correctness. |
| 3 | Merge `NativeButtonCommitBackend` and `PollCommitCoordinator` immediately | Too expensive relative to value before the upstream gameplay contract is cleaned up. |
| 4 | Investigate haptics/trigger first | Important, but belongs to a different problem than the current gameplay input bottleneck. |
| 5 | Rebuild the gameplay layer around CE3 `DeviceChangeEvent` concepts | Not grounded enough in DualPad's current injection path and too tied to CE3's internal structure. |
| 6 | Solve the current issues by pushing more gameplay meaning into `InputModalityTracker` | This is the wrong direction; it worsens the existing UI/gameplay entanglement. |

## Session Log

- 2026-04-08: Initial ideation - 11 candidate directions considered, 6 survived adversarial filtering.
