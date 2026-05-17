# DualPad 重构计划拆分总览

本目录用于收口 `dualpad_rearchitecture_plan_zh.md` 的拆分计划包。

它当前是设计输入与评审材料，不是默认 workflow current truth，也不是可以越过 `.dualpad-builder/` 直接执行的主线 authority。

目标不是保留一份“大方向正确”的蓝图，而是把它拆成一组可以逐 slice 评审、逐 slice 校对依赖、逐 slice 准备后续 builder promotion 的计划说明。

## 目录结构

- `dualpad_rearchitecture_plan_zh.md`
  - 原始总计划导入副本，仅供历史背景与旧口径对照，不作为执行合同
- `README_zh.md`
  - 本文件；说明拆分顺序、依赖关系和阅读入口
- `01_slice_phase0_freeze_and_replay_barrier_zh.md`
  - Phase 0：冻结当前行为，建立 replay / diff / golden trace barrier
- `02_slice_phase1_catalog_and_manifest_compiler_zh.md`
  - Phase 1：落地 `ContextCatalog + ActionManifest + LegacyIniImporter + ManifestValidator`
- `03_slice_phase2_menu_instance_truth_zh.md`
  - Phase 2：重建菜单实例真相源、`UiMenuObserver`、`MenuInstanceRegistry`、`ContextResolver`
- `04_slice_phase3_presentation_split_zh.md`
  - Phase 3：拆分 `InputModalityTracker`，落地 `SourceEvidenceCollector + PresentationProjection + SkyrimCompatibilitySurface`
- `05_slice_phase4_action_graph_and_interaction_engine_zh.md`
  - Phase 4：把 trigger 语义迁移到 `Action Graph + InteractionEngine`
- `06_slice_phase5_gameplay_projection_zh.md`
  - Phase 5：用 `GameplayProjection` 取代 coordinator feedback loop
- `07_slice_phase6_prompt_projection_zh.md`
  - Phase 6：用 `PromptProjection / PromptService` 取代 glyph reverse lookup
- `08_slice_phase7_ingress_and_resync_zh.md`
  - Phase 7：重做 dispatcher / coalescing / resync
- `09_slice_phase8_cutover_cleanup_and_ci_zh.md`
  - Phase 8 入口页：说明 `09a / 09b` 的分段顺序、共享约束与 handoff gate
- `09a_slice_phase8_runtime_closeout_zh.md`
  - Phase 8A：runtime closeout、public surface swap、legacy deletion / shim shrink
- `09b_slice_phase8_governance_closeout_zh.md`
  - Phase 8B：docgen provenance、reviewed docs 去重、测试 target 收口与默认 CI 接线

## 固定执行顺序

仅在某个 slice 已同步进入 `.dualpad-builder/feature_list.json`、`.dualpad-builder/sprint_plan.json` 或明确被提升为当前 Sprint 之后，下面的顺序才会转化为默认执行顺序；在 promotion 发生前，它只表示本计划包内部的阅读与依赖顺序。

1. `01_slice_phase0_freeze_and_replay_barrier_zh.md`
2. `02_slice_phase1_catalog_and_manifest_compiler_zh.md`
3. `03_slice_phase2_menu_instance_truth_zh.md`
4. `04_slice_phase3_presentation_split_zh.md`
5. `05_slice_phase4_action_graph_and_interaction_engine_zh.md`
6. `06_slice_phase5_gameplay_projection_zh.md`
7. `07_slice_phase6_prompt_projection_zh.md`
8. `08_slice_phase7_ingress_and_resync_zh.md`
9. `09_slice_phase8_cutover_cleanup_and_ci_zh.md`
10. `09a_slice_phase8_runtime_closeout_zh.md`
11. `09b_slice_phase8_governance_closeout_zh.md`

## 切片原则

- 每个 slice 都必须能单独指导规划、评审和 promotion 准备，不依赖 `work` 阶段临场决定核心方案。
- 任何 slice 只有在同步进 `.dualpad-builder/` 当前状态机后，才可升格为当前执行 authority；在此之前，下游实现、验证和 handoff 仍以 baseline 入口与 `.dualpad-builder/` 为准。
- `Phase 8` 从本轮起固定二拆：
  - `09a` 只负责 runtime closeout
  - `09b` 只负责 governance / docgen / CI closeout
  - `09b` 不得回头重开 `09a` 已冻结的 runtime 决策
- 每个 slice 都必须明确：
  - 目标与非目标
  - 依赖输入
  - 代码落点
  - 具体实现顺序
  - 验证入口
  - 退出条件
  - 需要保留给后续 slice 的稳定合同
- 相邻 slice 之间只通过明确的产物和合同交接，不通过“到时再看”的口头约定交接。

## 阅读方式

- 先读本文件确认本计划包的切片顺序与 authority 边界。
- 若要判断“当前能不能执行”，先回到 `docs/authoritative-baseline/README.md` 和 `.dualpad-builder/` 确认是否已经 promotion。
- 只有在需要历史背景、追踪旧口径或比对拆分前差异时，再回看 `dualpad_rearchitecture_plan_zh.md`。
- 若 `dualpad_rearchitecture_plan_zh.md` 与本 README 或任一 slice 冻结合同冲突，以本 README 与拆分后的 slice 文档为准。
- `Phase 8` 先读 `09_slice_phase8_cutover_cleanup_and_ci_zh.md`，再依次阅读 `09a`、`09b`；未 promotion 前，这仍是计划包内部顺序，不自动构成当前 Sprint 指令。
- 然后按顺序进入各 slice 文档推进规划与评审，不跳 phase，不并行跨越未冻结的基础合同。