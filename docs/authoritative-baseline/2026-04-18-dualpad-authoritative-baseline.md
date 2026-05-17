# DualPad Authoritative Baseline (2026-04-18)

## Purpose

从 `2026-04-18` 起，把 `DualPad` 的默认 workflow 收口成 `harness + ce + graphify`：

- `harness`
  - 固定 repo 入口、工作包和 builder memory
- `ce`
  - 负责默认的 planning、implementation、review
- `graphify`
  - 负责代码图和 repo-local 上下文加速

## Current Repo Truth

- 当前正式主线仍是：
  - `HidReader -> PadState -> PadEventSnapshot -> FrameActionPlan -> ActionLifecycleCoordinator -> NativeButtonCommitBackend -> AuthoritativePollState -> UpstreamGamepadHook -> XInputStateBridge`
- `Skyrim SE 1.5.97` 与 `CommonLibSSE-NG` 是当前唯一正式支持面。
- `Gameplay presentation owner` Phase 2 已并入默认主线，不再当成运行时 toggle。
- 当前 repo-owned 动态图标 surface 以主菜单 `startmenu.swf` 和共享 `ScaleformGlyphBridge` 为准。
- `FavoritesMenu` 的专项 SWF patch workspace、页面级 glyph broker 和 execution broker 当前不在仓库中；
  如果任务重新落到该页面，第一步是恢复 workspace，而不是继续沿用旧 handoff 的假设。

## Current Delivery Units

- `WF0`
  - Harness + CE + Graphify workflow bootstrap
- `DP1`
  - Runtime input pipeline truth
- `DP2`
  - Menu context policy and gameplay/menu ownership
- `DP3`
  - Native routing, controlmap combo overlay, and mod-event helper
- `DP4`
  - Dynamic glyph and menu presentation surfaces
- `DP5`
  - Validation, cleanup, and workflow honesty

## Default Constraints

- current truth 入口根目录固定为 `docs/authoritative-baseline/`
- repo-local builder memory 固定为 `.dualpad-builder/`
- 默认执行协议固定为：
  - `Planner -> ce:plan`
  - `Generator -> ce:work`
  - `Evaluator -> ce:review`
- 共享文档必须使用 repo-relative 路径
- 机器私有路径只写到 `AGENTS.win.md` 或 `AGENTS.mac.md`
- Graphify 只负责上下文加速，不替代 `README.md`、`src/ARCHITECTURE.md`、`docs/DOC_INDEX_zh.md` 或 `.dualpad-builder/`
- 只要本轮改动涉及代码文件，收尾前必须执行：
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`

## Source Docs

当前 baseline 主要引用这些现有 truth：

- `README.md`
- `src/ARCHITECTURE.md`
- `docs/DOC_INDEX_zh.md`
- `docs/current_input_pipeline_zh.md`
- `docs/backend_routing_decisions.md`
- `docs/menu_context_policy_current_status_zh.md`
- `docs/main_menu_glyph_current_status_zh.md`
- `docs/mod_event_keyboard_helper_backend_zh.md`

本文件本身不取代它们，而是把它们收口成默认入口。
