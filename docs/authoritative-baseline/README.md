# DualPad Authoritative Baseline 入口

从 `2026-04-18` 起，`docs/authoritative-baseline/` 是 `DualPad` 的 workflow current truth 入口。

这里负责收口：

- 默认阅读顺序
- 当前工作包入口
- builder memory 与 sprint 路由
- graphify close-out 规则

它不是为了替代已经存在的 repo truth，而是把默认入口固定下来。详细代码 reality 仍优先看：

- `README.md`
- `src/ARCHITECTURE.md`
- `docs/DOC_INDEX_zh.md`
- `docs/current_*`
- `docs/*_current_status_zh.md`

`docs/plans/` 下的计划包默认视为设计输入或候选迁移方案；除非其状态已经明确同步进 `.dualpad-builder/`，否则不得越级自称当前执行主线。

## 默认阅读顺序

1. `docs/authoritative-baseline/2026-04-18-dualpad-authoritative-baseline.md`
2. `docs/authoritative-baseline/work-packages/README.md`
3. `docs/harness/dualpad-builder.md`
4. `.dualpad-builder/spec.md`
5. `.dualpad-builder/feature_list.json`
6. `.dualpad-builder/sprint_plan.json`
7. `.dualpad-builder/progress.md`
8. `README.md`
9. `src/ARCHITECTURE.md`
10. `docs/DOC_INDEX_zh.md`
11. 再按任务类型跳到对应当前事实文档

## 当前工作路（2026-04-20）

- 当前活跃 Sprint：
  - `S-DP1a`
- 当前直接焦点：
  - 先冻结 `DP1a Route-health contract freeze`，不要在 `DP4` glyph 线或后续 phase 上越过当前 gate
- 当前推荐推进顺序：
  1. 先看本目录入口、`work-packages/README.md`、`docs/harness/dualpad-builder.md` 和 `.dualpad-builder/` 记忆层，确认当前 Sprint、边界和 close-out 口径
  2. 先进入 `DP1a`：
     - `docs/current_input_pipeline_zh.md`
     - `docs/plans/dualpad_rearchitecture/01_slice_phase0_freeze_and_replay_barrier_zh.md`
  3. 只有 `DP1a` 完成后，才进入 `DP4a`：
     - `docs/main_menu_glyph_current_status_zh.md`
     - `docs/plans/dualpad_rearchitecture/02_slice_phase1_catalog_and_manifest_compiler_zh.md`
     - `docs/plans/dualpad_rearchitecture/07_slice_phase6_prompt_projection_zh.md`
  4. 只有 `DP1a -> DP4a` 完成后，才允许继续下游计划包；顺序必须与重构计划依赖保持一致：
     - `Phase 0`
     - `Phase 1`
     - `Phase 2`
     - `Phase 3`
     - `Phase 4`
     - `Phase 5`
     - `Phase 6`
  5. 需要做验证、风险复查或 handoff 时，再回到 `DP5` 路线：
     - `docs/current_cleanup_risk_review_zh.md`
     - `docs/reviews/README_zh.md`
     - `.dualpad-builder/progress.md`

## 当前工作包状态

- `WF0`：已完成
- `DP1`：进行中
- `DP1a`：当前活跃 Sprint
- `DP2`：进行中
- `DP3`：进行中
- `DP4`：进行中
- `DP4a`：进行中（当前 worktree 已启动，但不是活跃 Sprint）
- `DP5`：计划中

## 主题路由

主题路由只用于理解某个主题的 current reality，不替代上面的当前工作路 gate；若当前任务是继续推进实现，仍先按 `S-DP1a -> DP4a` 顺序进入。

- 输入主链 / runtime contract：
  - `docs/current_input_pipeline_zh.md`
  - `docs/backend_routing_decisions.md`
  - `docs/unified_action_lifecycle_model_zh.md`
  - `docs/mapping_snapshot_atomicity_audit_and_injection_contract_zh.md`
- 菜单上下文 / gameplay-menu ownership：
  - `docs/menu_context_policy_current_status_zh.md`
  - `docs/gameplay_input_ownership_investigation_and_plan_zh.md`
  - `docs/gameplay_sustained_digital_and_cursor_handoff_plan_zh.md`
- native routing / controlmap / mod event：
  - `docs/native_pc_event_semantics_zh.md`
  - `docs/controlmap_gamepad_event_inventory_zh.md`
  - `docs/controlmap_combo_profile_zh.md`
  - `docs/mod_event_keyboard_helper_backend_zh.md`
- 动态图标 / 表现层：
  - `docs/main_menu_glyph_current_status_zh.md`
  - `docs/dynamic_glyph_svg_system_plan_zh.md`
- 风险、审查与验证：
  - `docs/current_cleanup_risk_review_zh.md`
  - `docs/reviews/README_zh.md`
  - `docs/verification/menu_context_runtime_policy_matrix_zh.md`

## Builder Memory

repo 内默认记忆层固定为：

- `.dualpad-builder/spec.md`
- `.dualpad-builder/feature_list.json`
- `.dualpad-builder/sprint_plan.json`
- `.dualpad-builder/progress.md`

任何新的 planning / implementation / review，会先从这里拿当前 Sprint 和验证口径。

如果某个计划包想升格为当前执行 authority，必须先在 `.dualpad-builder/` 中留下可追踪的 promotion 痕迹；仅修改 `docs/plans/*` 不会自动改变默认 workflow 路由。

## Graphify

- 初始化：`python3 scripts/dev/setup_graphify_local.py`
- 重建：`python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`
- 查询：`python3 -m graphify query "show the main flow"`

Graphify 只负责上下文加速，不替代本目录、`README.md`、`src/ARCHITECTURE.md` 或 `.dualpad-builder/`。


