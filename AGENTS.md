# DualPad 协作入口

本文件是任何新对话进入本仓库时的默认入口。

如果当前机器是 macOS，读完本文件后必须继续阅读 `AGENTS.mac.md`。

如果当前机器是 Windows，读完本文件后必须继续阅读 `AGENTS.win.md`。

## Update Adjustment (2026-04-18, workflow bootstrap baseline)

- 从 `2026-04-18` 起，仓库的 workflow current truth 入口根目录固定为：
  - `docs/authoritative-baseline/`
- 这个目录负责收口默认阅读顺序、工作包入口、builder memory 和 graphify close-out 规则；
  它不替代下面这些已经存在的 repo truth：
  - `README.md`
  - `src/ARCHITECTURE.md`
  - `docs/DOC_INDEX_zh.md`
  - `docs/current_*`
  - `docs/*_current_status_zh.md`

## 当前仓库状态

- 当前正式主线是：
  - `HidReader -> PadState -> PadEventSnapshot -> FrameActionPlan -> ActionLifecycleCoordinator -> NativeButtonCommitBackend -> AuthoritativePollState -> UpstreamGamepadHook -> XInputStateBridge`
- 当前唯一正式支持面仍是：
  - `Skyrim SE 1.5.97`
  - `CommonLibSSE-NG`
- `Gameplay presentation owner` 的 Phase 2 已并入默认主线，不再作为运行时回退开关。
- 动态图标当前已落地、且可直接在 repo 内继续推进的 surface 仍是：
  - `src/input/glyph/ScaleformGlyphBridge.*`
  - `config/DualPadBindings.ini`
  - `Interface/startmenu.swf`
- `FavoritesMenu` 的专项 SWF patch workspace、页面源码和页面级 broker 当前不在 repo 内；
  如果任务重新落到该页面，第一步是恢复工作区，而不是直接修页面逻辑。
- `DP1a`、`DP4a` 与 `PH0` 已完成；`PH1` - `PH8B` 仍是 `.dualpad-builder/` planned backlog；
  这些后续 slice 不是当前 active Sprint，开工前仍必须按 builder memory 从 `planned` 晋升并记录 progress。

## 新对话的默认阅读顺序

任何新任务，默认先读：

1. `AGENTS.md`
2. 若当前机器是 macOS，再读 `AGENTS.mac.md`
3. 若当前机器是 Windows，再读 `AGENTS.win.md`
4. `docs/authoritative-baseline/README.md`
5. `docs/harness/dualpad-builder.md`
6. `.dualpad-builder/spec.md`
7. `.dualpad-builder/feature_list.json`
8. `.dualpad-builder/sprint_plan.json`
9. `.dualpad-builder/progress.md`
10. `README.md`
11. `src/ARCHITECTURE.md`
12. `docs/DOC_INDEX_zh.md`

## DualPad Builder 默认工作流

- 本仓库默认采用 `harness + ce + graphify` 作为工作流主栈。
- repo 内的 harness 记忆层固定在：
  - `.dualpad-builder/`
- 三段式默认映射固定为：
  - `Planner -> ce:plan`
  - `Generator -> ce:work`
  - `Evaluator -> ce:review`
- 若范围、资料或切片边界仍不清楚，可以先用 `ce:brainstorm`，但它是补充入口，不替代默认主链。
- Sync 约束：
  - Sprint、工作包、验证状态变化时，同步更新 `.dualpad-builder/feature_list.json` 与 `.dualpad-builder/sprint_plan.json`
  - 每次开始、完成、返工、阻塞一个 slice 时，都要向 `.dualpad-builder/progress.md` 追加记录
  - 只要本轮改动涉及代码文件，在宣告完成前必须执行：
    - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`
  - Graphify 只负责上下文加速，不替代 `docs/authoritative-baseline/`、`README.md`、`src/ARCHITECTURE.md` 或 `.dualpad-builder/`

## Graphify 本地自动化

- `graphify-out/` 是本地生成物，不提交到 Git。
- 仓库内版本化 hooks 位于 `.githooks/`，用于在本机 `commit / checkout / merge / rebase` 后自动重建代码图。
- 每台新机器 clone 或首次进入仓库后，只需要执行一次：
  - `python3 scripts/dev/setup_graphify_local.py`
- 这一步会：
  - 自动安装或复用本机 `graphifyy`
  - 配置本地 `git config core.hooksPath .githooks`
  - 补齐本机 `.codex/hooks.json`
  - 在本地尚无 `graphify-out/graph.json` 时自动生成首轮代码图
- 根目录不是 Node 项目，因此 graphify 入口统一使用 Python 脚本：
  - 初始化：`python3 scripts/dev/setup_graphify_local.py`
  - 手动重建：`python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`
  - 查询：`python3 -m graphify query "show the main flow"`

## 工作规则

- 新文档一律使用 repo-relative 路径。
- 机器私有路径、Skyrim 实例路径、日志路径、外部 live artifact 路径只写到 `AGENTS.win.md` 或 `AGENTS.mac.md`，不要写进共享文档。
- 不要把 `keyboard-native`、旧 `native button splice`、`XInputGetState` fallback 或缺失的 `FavoritesMenu` workspace 当成当前默认真相。
- 如果任务是主菜单/通用动态图标：
  - 继续沿 `ScaleformGlyphBridge + BindingManager + token/descriptor + repo-owned SWF` 主线推进
  - 但当前推进前仍先回 `docs/authoritative-baseline/README.md` 与 `.dualpad-builder/` 确认 gate；不得绕过后续 Sprint promotion 直接启动 `PH1` 或 `PromptService`
- 如果任务是 `FavoritesMenu`：
  - 第一步先恢复 SWF workspace 与页面源码，再重新做 artifact inventory
- 在宣称验证通过前，必须真的跑过对应命令或手工验证步骤，并把结果写进 `.dualpad-builder/progress.md`

## 快速路由

- “先看什么？”
  先读本文件、当前机器对应的 `AGENTS.win.md` 或 `AGENTS.mac.md`，再读 `docs/authoritative-baseline/README.md`、`docs/harness/dualpad-builder.md` 和 `.dualpad-builder/` 记忆层 4 个文件。

- “我要理解当前架构”
  先读 `docs/authoritative-baseline/2026-04-18-dualpad-authoritative-baseline.md`、`README.md`、`src/ARCHITECTURE.md`、`docs/DOC_INDEX_zh.md`，再按主题跳到当前事实文档。

- “我要改输入主链 / native routing”
  先读 `docs/authoritative-baseline/work-packages/README.md`，再读 `docs/current_input_pipeline_zh.md`、`docs/backend_routing_decisions.md`、`docs/unified_action_lifecycle_model_zh.md` 和当前 `.dualpad-builder/sprint_plan.json`。

- “我要改菜单上下文 / gameplay-menu ownership”
  先读 `docs/menu_context_policy_current_status_zh.md`、`docs/gameplay_input_ownership_investigation_and_plan_zh.md`、`docs/gameplay_sustained_digital_and_cursor_handoff_plan_zh.md`。

- “我要改动态 glyph”
  先读 `docs/authoritative-baseline/README.md`、`docs/authoritative-baseline/work-packages/README.md` 和当前 `.dualpad-builder/sprint_plan.json`，确认当前 gate；`DP1a`、`DP4a` 与 `PH0` 已完成，后续 glyph / prompt 工作仍必须先晋升对应 Sprint。`docs/dynamic_glyph_svg_system_plan_zh.md` 只用于长期 SVG / Widget 方案，不替代当前 compat surface 合同。

- “按默认工作流继续”
  先读 `docs/harness/dualpad-builder.md`、`.dualpad-builder/spec.md`、`.dualpad-builder/feature_list.json`、`.dualpad-builder/sprint_plan.json` 和 `.dualpad-builder/progress.md`，然后按 `Planner -> ce:plan`、`Generator -> ce:work`、`Evaluator -> ce:review` 推进当前 slice。

- “我要重建 graphify”
  在 repo root 执行 `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`；若是新机器首次进入仓库，先执行 `python3 scripts/dev/setup_graphify_local.py`。

## graphify

This project has a graphify knowledge graph at `graphify-out/`.

Rules:
- Before answering architecture or codebase questions, read `graphify-out/GRAPH_REPORT.md` for god nodes and community structure
- If `graphify-out/wiki/index.md` exists, navigate it instead of reading raw files
- Treat graphify as a context accelerator, not the source of truth over `docs/authoritative-baseline/`, `README.md`, `src/ARCHITECTURE.md`, or `.dualpad-builder/`
- Repo-local Git hooks rebuild the graph after `commit / checkout / merge / rebase`
- After modifying code files in this session, run `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`
