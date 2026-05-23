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

## 当前工作路（2026-05-23）

- 当前活跃 Sprint：
  - 无；`S-PH0` 与 `S-PH1` 已完成，`S-PH2` 仍 planned / not started。
- 当前直接焦点：
  - `DP1a Route-health contract freeze`、`DP4a Glyph compat diagnostics freeze` 与 `PH0 Phase 0 replay barrier` 已完成。
  - `PH0` 的 runtime proof 已通过 dispatcher / processor mode 候选输出与 batch diff；`materialize-fixture` 仍只代表 schema / diff plumbing。
  - `PH1` 已完成并通过 prove-out；`PH2` 仍为 planned / not started。
- 当前推荐推进顺序：
  1. 先看本目录入口、`work-packages/README.md`、`docs/harness/dualpad-builder.md` 和 `.dualpad-builder/` 记忆层，确认当前 Sprint、边界和 close-out 口径
  2. `DP1a` 已完成；如需复核，入口仍是：
     - `docs/current_input_pipeline_zh.md`
     - `docs/plans/dualpad_rearchitecture/01_slice_phase0_freeze_and_replay_barrier_zh.md`
  3. `DP4a` 已完成；如需复核，入口仍是：
     - `docs/main_menu_glyph_current_status_zh.md`
     - `docs/plans/dualpad_rearchitecture/02_slice_phase1_catalog_and_manifest_compiler_zh.md`
     - `docs/plans/dualpad_rearchitecture/07_slice_phase6_prompt_projection_zh.md`
  4. `Phase 0` 已完成；replay barrier 入口固定为：
     - `src/input_v2/telemetry/`
     - `tests/replay/golden/phase0/`
     - `scripts/dev/dualpad_trace_diff.py`
  5. 后续计划包仍必须按依赖顺序单独晋升，不得把 planned backlog 误写成 active / completed：
     - `Phase 2`
     - `Phase 3`
     - `Phase 4`
     - `Phase 5`
     - `Phase 6`
     - `Phase 7`
     - `Phase 8`
     - `Phase 8A`
     - `Phase 8B`
     - 这些 phase 已作为 planned backlog 登记到 `.dualpad-builder/`，但只有对应 Sprint 从 `planned` 晋升后才是当前执行 authority。
  6. 需要做验证、风险复查或 handoff 时，再回到 `DP5` 路线：
     - `docs/current_cleanup_risk_review_zh.md`
     - `docs/reviews/README_zh.md`
     - `.dualpad-builder/progress.md`

## 当前工作包状态

- `WF0`：已完成
- `DP1`：进行中
- `DP1a`：已完成
- `DP2`：进行中
- `DP3`：进行中
- `DP4`：进行中
- `DP4a`：已完成
- `PH0`：已完成（dispatcher / processor runtime replay barrier 已验证通过）
- `PH1`：已完成
- `PH2` - `PH8B`：计划中（已登记为 builder backlog，未晋升为当前 active Sprint）
- `DP5`：计划中

## 主题路由

主题路由只用于理解某个主题的 current reality，不替代上面的当前工作路 gate；若当前任务是继续推进实现，必须先在 `.dualpad-builder/` 晋升对应 Sprint。

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


