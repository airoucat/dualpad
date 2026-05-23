# DualPad Work Packages

从 `2026-04-18` 起，`DualPad` 默认按下面这组工作包推进，而不是直接把整个 `docs/` 树当成计划清单逐篇翻译。

当前先用这份 `README.md` 作为工作包注册表；后续如果某个工作包需要长期演进，再单独拆包目录。

`docs/plans/` 下的独立计划包可以作为设计输入引用，但它们不会自动成为当前工作包或 Sprint；默认 workflow 仍只认 `.dualpad-builder/` 里登记过的 current state。

## 当前推进状态（来自 `.dualpad-builder/`）

- `WF0`：`completed`
- `DP1`：`in_progress`
- `DP1a`：`completed`
- `DP2`：`in_progress`
- `DP3`：`in_progress`
- `DP4`：`in_progress`
- `DP4a`：`completed`
- `PH0`：`completed`（dispatcher / processor runtime replay proof 已验证通过）
- `PH1` - `PH8B`：`planned`
- `DP5`：`planned`
- 当前活跃 Sprint：无；`S-PH0` 已完成，`S-PH1` 仍 planned / not started

## 当前工作路（closed through `S-PH0`）

如果只是继续当前主线，而不是重新梳理整个仓库，默认按下面顺序走：

1. 先确认 workflow 入口：
   - `docs/authoritative-baseline/README.md`
   - `docs/harness/dualpad-builder.md`
   - `.dualpad-builder/spec.md`
   - `.dualpad-builder/feature_list.json`
   - `.dualpad-builder/sprint_plan.json`
   - `.dualpad-builder/progress.md`
2. `DP1a Route-health contract freeze` 已完成；如需复核，入口仍是：
   - `docs/current_input_pipeline_zh.md`
   - `docs/plans/dualpad_rearchitecture/01_slice_phase0_freeze_and_replay_barrier_zh.md`
   - `src/input/InputFramePump.cpp`
   - `src/input/injection/UpstreamGamepadHook.cpp`
   - `src/input/injection/PadEventSnapshotDispatcher.cpp`
- 边界：
  - 顶层合同继续只认 `route_state = active_fresh | active_stale | disabled`。
  - `drain_reason`、`last_poll_age_ms`、`hook_installed` 继续作为 secondary diagnostics。
  - 本 slice 不删除 `use_upstream_gamepad_hook=false`，不删除 stale assist drain，也不引入新的 owner 抽象。
3. `DP4a Glyph compat diagnostics freeze` 已完成；如需复核，入口仍是：
   - `docs/main_menu_glyph_current_status_zh.md`
   - `docs/plans/dualpad_rearchitecture/02_slice_phase1_catalog_and_manifest_compiler_zh.md`
   - `docs/plans/dualpad_rearchitecture/07_slice_phase6_prompt_projection_zh.md`
   - `src/input/glyph/ScaleformGlyphBridge.cpp`
   - `src/input/glyph/ScaleformGlyphBridge.h`
   - `config/DualPadBindings.ini`
- 边界：
  - 当前 DP4 仍是 repo-owned main-menu glyph compatibility surface，不是最终 glyph / prompt authority。
  - `status / fallback / ambiguity` 先只进入内部诊断面。旧 `DualPad_GetActionGlyphToken` 继续返回单个 token string；旧 `DualPad_GetActionGlyph` descriptor 继续保持 `ok / buttonArtToken / semanticId / contextName`。
  - 任何旧返回对象字段扩展，都必须等 `Phase 6` 定义完新 prompt contract 后再决定，不能默认旧 SWF 安全兼容。
4. `Phase 0` Replay Barrier 已完成；当前入口是：
   - `src/input_v2/telemetry/`
   - `tests/replay/golden/phase0/`
   - `scripts/dev/dualpad_trace_diff.py`
   - schema / harness bootstrap 已完成；`materialize-fixture` 只证明 schema / diff plumbing，不能作为 runtime replay proof。
   - runtime close-out 已由 `dispatcher` / `processor` mode 真实驱动 `PadEventSnapshotDispatcher` / `PadEventSnapshotProcessor` 后生成的 candidate bundle 与 batch diff 验证。
5. 后续计划包必须按 `.dualpad-builder/sprint_plan.json` 里登记的 planned backlog 顺序单独晋升：
   - `Phase 1` ContextCatalog / ActionManifest groundwork
   - `Phase 2` Menu instance truth
   - `Phase 3` Presentation split
   - `Phase 4` Action graph / interaction engine
   - `Phase 5` Gameplay projection
   - `Phase 6` PromptProjection / PromptService cutover
   - `Phase 7` Ingress / resync
   - `Phase 8` cutover entry gate
   - `Phase 8A` runtime closeout
   - `Phase 8B` governance closeout
   - 不得把 `Phase 6` 直接排在 `Phase 1` 后面
   - 不得把 planned backlog 误写成 active / completed；每个 phase 真正开工前仍要把对应 Sprint 晋升并写入 `.dualpad-builder/progress.md`
6. 做 close-out、验证或 handoff 时，再收回 `DP5`：
   - `docs/current_cleanup_risk_review_zh.md`
   - `docs/reviews/README_zh.md`
   - `.dualpad-builder/progress.md`

## `DP1` Runtime input pipeline truth

- 目标：
  - 固定当前正式输入主链、生命周期边界和 `AuthoritativePollState` 口径
- 状态：
  - 进行中
- 首读：
  - `README.md`
  - `src/ARCHITECTURE.md`
  - `docs/current_input_pipeline_zh.md`
  - `docs/mapping_snapshot_atomicity_audit_and_injection_contract_zh.md`
  - `docs/unified_action_lifecycle_model_zh.md`

## `DP1a` Route-health contract freeze

- 目标：
  - 在不改变当前 owner 语义的前提下，冻结 `route_state / drain_reason / last_poll_age_ms` 的当前兼容态合同与验证面
- 状态：
  - 已完成
- 首读：
  - `docs/current_input_pipeline_zh.md`
  - `docs/plans/dualpad_rearchitecture/01_slice_phase0_freeze_and_replay_barrier_zh.md`
  - `.dualpad-builder/feature_list.json`
  - `.dualpad-builder/sprint_plan.json`
- 硬边界：
  - 顶层状态只认 `active_fresh / active_stale / disabled`
  - `drain_reason`、`last_poll_age_ms`、`hook_installed` 继续作为 secondary diagnostics；`frame_pump_disabled` 仍是 `drain_reason` 的取值，不额外冻结第二套 disabled 字段
  - 本 slice 不删除 rollback gate / stale assist drain，不引入新的 route hierarchy
- prove-out 固定命令：
  - `xmake build DualPadRouteHealthContractTests`
  - `xmake run DualPadRouteHealthContractTests`
  - `xmake build DualPad`


## `DP2` Menu context policy and gameplay/menu ownership

- 目标：
  - 固定 `MenuContextPolicy`、`InputContextNames`、`gameplay/menu` handoff 的当前 truth 和后续窗口
- 状态：
  - 进行中
- 首读：
  - `docs/menu_context_policy_current_status_zh.md`
  - `docs/gameplay_input_ownership_investigation_and_plan_zh.md`
  - `docs/gameplay_sustained_digital_and_cursor_handoff_plan_zh.md`
  - `docs/verification/menu_context_runtime_policy_matrix_zh.md`

## `DP3` Native routing, controlmap combo overlay, and mod-event helper

- 目标：
  - 固定 native routing、controlmap overlay、`ModEvent` helper backend 的正式支持面和剩余 gate
- 状态：
  - 进行中
- 首读：
  - `docs/backend_routing_decisions.md`
  - `docs/native_pc_event_semantics_zh.md`
  - `docs/controlmap_gamepad_event_inventory_zh.md`
  - `docs/controlmap_combo_profile_zh.md`
  - `docs/mod_event_keyboard_helper_backend_zh.md`

## `DP4` Dynamic glyph and menu presentation surfaces

- 目标：
  - 只围绕 repo 当前真实拥有的 glyph bridge 和页面 surface 推进动态 glyph
- 状态：
  - 进行中
- 首读：
  - `docs/main_menu_glyph_current_status_zh.md`
  - `docs/dynamic_glyph_svg_system_plan_zh.md`
  - `src/input/glyph/ScaleformGlyphBridge.cpp`
  - `src/input/glyph/ScaleformGlyphBridge.h`
  - `config/DualPadBindings.ini`
- 说明：
  - 当前 DP4 仍是 repo-owned main-menu glyph compatibility surface，不是最终 glyph / prompt authority。
  - 现行 ScaleformGlyphBridge -> BindingManager::GetTriggerForAction(...) 反查链只应视为过渡兼容实现。
- 边界提醒：
  - 如果任务涉及 `FavoritesMenu` 页面级改造，第一步是恢复 SWF workspace，而不是直接在当前 repo 假设它已经存在

## `DP4a` Glyph compat diagnostics freeze

- 目标：
  - 在不改变旧 SWF 外部返回 shape 的前提下，冻结 glyph compat 诊断边界与最小验证面
- 状态：
  - 已完成
- 首读：
  - `docs/main_menu_glyph_current_status_zh.md`
  - `docs/plans/dualpad_rearchitecture/02_slice_phase1_catalog_and_manifest_compiler_zh.md`
  - `docs/plans/dualpad_rearchitecture/07_slice_phase6_prompt_projection_zh.md`
  - `.dualpad-builder/feature_list.json`
  - `.dualpad-builder/sprint_plan.json`
- 硬边界：
  - `status / fallback / ambiguity` 只先进入内部诊断面、日志、trace 与 targeted tests
  - 旧 `DualPad_GetActionGlyphToken` 继续返回单个 token string；旧 `DualPad_GetActionGlyph` descriptor 继续保持 `ok / buttonArtToken / semanticId / contextName`
  - 任何旧返回对象字段扩展，都必须等 `Phase 6` 定义完新的 prompt contract 后再决定
- prove-out 固定命令：
  - `xmake build DualPadGlyphResolutionCompatTests`
  - `xmake run DualPadGlyphResolutionCompatTests`
  - `xmake build DualPad`
  - 若本轮改到 `ScaleformGlyphBridge` 或 `SWF` surface，再补本机手工验证记录

## `PH0` Phase 0 replay barrier

- 目标：
  - 建立 repo-owned replay / diff / golden trace barrier，并把 `FavoritesMenu` 条件场景从默认退出条件中拆出。
- 状态：
  - 已完成；dispatcher / processor runtime replay proof 已验证通过
- 首读：
  - `docs/plans/dualpad_rearchitecture/01_slice_phase0_freeze_and_replay_barrier_zh.md`
  - `docs/plans/dualpad_rearchitecture/phase0_scenarios.json`
  - `src/input_v2/telemetry/`
  - `tests/replay/golden/phase0/`
  - `scripts/dev/dualpad_trace_diff.py`
- 硬边界：
  - `06_favorites_page_lr_accept_cancel` 仍是 `mandatory=false` / `conditional_live`；未恢复 workspace/source/artifact inventory 时不得成为默认退出条件。
  - `PH1` 仍为 planned / not started；不得把本 slice 的 replay barrier 当作 manifest compiler 或 PromptService cutover。
  - `ReplayHarness` 的 copy-only 行为只能称为 `materialize-fixture`；runtime proof 必须走 `dispatcher` / `processor` mode 生成 candidate bundle。
- prove-out 固定命令：
  - `xmake build DualPad`
  - `xmake build DualPadDInput8Proxy`
  - `xmake build DualPadReplayHarness`
  - `xmake build DualPadReplayHarnessTests`
  - `xmake run DualPadReplayHarnessTests`
  - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode dispatcher --output-root build/replay-dispatcher`
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay-dispatcher --report-root build/replay-diff-dispatcher`
  - `xmake run DualPadReplayHarness -- --batch tests/replay/golden/phase0 --mode processor --output-root build/replay-processor`
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay-processor --report-root build/replay-diff-processor`
  - `materialize-fixture` diff 只证明 schema / diff plumbing 与 fixture materialization；PH0 runtime close-out 以 `dispatcher` / `processor` mode 的 candidate output 和 batch diff 为准。

## `DP5` Validation, cleanup, and workflow honesty

- 目标：
  - 把验证、风险复查、handoff 文档和 workflow honesty 收成一条可复述的 close-out 链
- 状态：
  - 计划中
- 首读：
  - `docs/current_cleanup_risk_review_zh.md`
  - `docs/reviews/README_zh.md`
  - `.dualpad-builder/feature_list.json`
  - `.dualpad-builder/sprint_plan.json`
  - `.dualpad-builder/progress.md`

## 工作包使用方式

- planning 时，先确定当前属于哪个 `DP` 工作包
- 再从该工作包反查要读的 current truth 文档和代码入口
- 新的 slice、验证和 close-out 记录都同步写回 `.dualpad-builder/`
- 如果外部计划包尚未 promotion into `.dualpad-builder/`，实现与验证不得把它直接当成当前执行主线
