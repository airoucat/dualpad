# DP5-RC20 U1.9 Axis2D / Chord / Primary Path Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 收紧 `Axis2D` 值域、chord timestamp 和 primary path exclusivity 合同，不改变 `input_v2` mainline，不新增 runtime phase。

**Architecture:** 在 `src/input_v2/actions` 中把隐式 Axis2D / chord 语义转成显式小模型，由 `InteractionEngine` 输出确定性的 `ActionValueSnapshot` 和 `ActionPhaseChange`。在 `src/input_v2/gameplay` 中用单一 primary path arbitration helper 收口 gamepad / mouse / keyboard / UI / menu cursor owner 仲裁，避免继续堆叠分散 `if` 分支。

**Tech Stack:** C++23、xmake targets `DualPadInputV2Tests` / `DualPadGameplayProjectionTests` / `DualPadPropertyTests`、Phase 8 CI。

---

### Task 1: Axis2D Contract

**Files:**
- Modify: `src/input_v2/actions/InteractionEngine.h`
- Modify: `src/input_v2/actions/InteractionEngine.cpp`
- Test: `tests/input_v2/InputV2Tests.cpp`
- Test: `tests/input_v2/PropertyTests.cpp`

- [ ] Add focused tests proving two same-action stick axis bindings coalesce into one `Axis2D` value with `x` / `y`, clamped range `[-1.0, 1.0]`, neutral zero, and no duplicate `Axis1D` value.
- [ ] Implement `Axis2D` aggregation by visible bindings for the same action and action value kind.
- [ ] Use `frame.facts.monotonicUs` as the coalesced value timestamp when present; otherwise use the latest component sample timestamp.
- [ ] Keep single trigger axes as `Axis1D`, preserving existing trigger behavior.

### Task 2: Chord Timestamp Contract

**Files:**
- Modify: `src/input_v2/actions/InteractionEngine.h`
- Modify: `src/input_v2/actions/InteractionEngine.cpp`
- Test: `tests/input_v2/InputV2Tests.cpp`
- Test: `tests/input_v2/PropertyTests.cpp`

- [ ] Add tests for `firstEdgeUs`, `lastEdgeUs`, `evaluationUs`, edge-triggered fire, level-held non-refire, and overflow / degraded invalidation.
- [ ] Extend `ActionPhaseChange` with chord timestamp fields and invalidation marker.
- [ ] Emit chord pulse timestamps from the first/last participant edge and frame evaluation time.
- [ ] Clear chord latch and skip chord firing on health degraded frames such as overflow.

### Task 3: Primary Path Exclusivity

**Files:**
- Modify: `src/input_v2/gameplay/GameplayProjectionFrame.h`
- Modify: `src/input_v2/gameplay/GameplayProjectionFrame.cpp`
- Test: `tests/input_v2/GameplayProjectionTests.cpp`

- [ ] Add a primary path arbitration table struct covering `gamepad`, `mouse`, `keyboard`, `ui`, and `menu cursor` inputs.
- [ ] Add tests for keyboard/mouse precedence over gamepad analog, gamepad analog reclaim, menu / UI non-gameplay ownership, and cursor owner coherence.
- [ ] Route `ResolveGameplayProjection` owner decisions through the arbitration helper while preserving existing channel owners, gates, and output plans.

### Task 4: Documentation / Builder / Close-Out

**Files:**
- Modify: `docs/authoritative-baseline/dp5_rc20_contract_zh.md`
- Modify: `.dualpad-builder/feature_list.json`
- Modify: `.dualpad-builder/sprint_plan.json`
- Modify: `.dualpad-builder/progress.md`
- Update GitHub issue: `#8`

- [ ] Document the U1.9 contracts in reviewed Chinese narrative without copying generated tables.
- [ ] Record DP5 / S-DP5 U1.9 state and verification results.
- [ ] Run focused tests, Phase 8 CI, replay diff, JSON checks, reviewed/generated docs consistency, graphify rebuild, and `git diff --check`.
- [ ] Update issue `#8` checklist after verification evidence exists.
