# DualPad Builder Harness

这是把 `harness + ce + graphify` 模板落到 `DualPad` 仓库后的默认工作流说明。

这里的目标不是引入一组松散 skill，而是把 repo 级执行协议固定下来：

- `harness` 负责项目状态、完成定义、进度同步
- `ce` 负责默认的规划、实现、审查执行
- `graphify` 负责代码图和结构化上下文加速

## Related Docs

- `docs/authoritative-baseline/README.md`
- `README.md`
- `src/ARCHITECTURE.md`
- `docs/DOC_INDEX_zh.md`

## Concept Mapping

| 模板概念 | 本仓库落地 |
| --- | --- |
| `planner` | 默认由 `ce:plan` 执行；先按 `docs/authoritative-baseline/work-packages/README.md` 确认当前工作包，再更新 `.dualpad-builder/` |
| `generator` | 默认由 `ce:work` 执行；只实现当前 slice 范围内的代码、测试、脚本和必要文档 |
| `evaluator` | 默认由 `ce:review` 执行；验证 truth drift、runtime regression、fallback 复活和 proof honesty |
| `sync` | 运行 graphify close-out，并把状态写回 `.dualpad-builder/feature_list.json`、`sprint_plan.json`、`progress.md` |
| `memory layer` | `.dualpad-builder/`，是 repo 内当前实施状态的默认记忆层 |

## Memory Layer

根目录下的 `.dualpad-builder/` 是默认的项目实施记忆层：

- `spec.md`
  - 项目目标、硬约束、非目标、完成定义
- `feature_list.json`
  - 按 `WF0`、`DP1-DP5` 跟踪状态与 `passes`
- `sprint_plan.json`
  - 当前 Sprint、依赖、退出标准、验证方式
- `progress.md`
  - 时间线、阻塞、验证结果、重要决策

## Default Execution Loop

1. `Planner`
   - 先读 `docs/authoritative-baseline/README.md`
   - 再读 `.dualpad-builder/spec.md`、`feature_list.json`、`sprint_plan.json`、`progress.md`
   - 再回到 `README.md`、`src/ARCHITECTURE.md` 和 `docs/DOC_INDEX_zh.md`
   - 确认当前要推进的是哪个 `DP` 工作包、哪个 slice
   - 若范围或资料仍不清楚，先回到 `ce:brainstorm`
   - 默认由 `ce:plan` 输出当前 slice 的目标、边界、退出标准
2. `Generator`
   - 只在当前 slice 范围内写代码、测试、脚本和必要文档
   - 默认由 `ce:work` 执行
   - 不把旧 `keyboard-native`、旧 fallback 或缺失的 `FavoritesMenu` workspace 偷渡成当前 truth
3. `Evaluator`
   - 对照当前 Sprint 声明的验证入口做审查
   - 默认由 `ce:review` 执行
   - 优先找 runtime contract 漂移、文档/实现错位、历史方案回流和 proof 伪闭环
4. `Sync`
   - 若本轮涉及代码文件，先执行：
     - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`
   - 然后同步更新 `.dualpad-builder/feature_list.json`
   - 同步更新 `.dualpad-builder/sprint_plan.json`
   - 最后向 `.dualpad-builder/progress.md` 追加记录

## Repo-Specific Rules

- current truth 入口根目录始终是 `docs/authoritative-baseline/`
- 详细代码 reality 仍以 `README.md`、`src/ARCHITECTURE.md`、`docs/DOC_INDEX_zh.md` 和当前事实文档为准
- 当前正式 runtime mainline 是：
  - `HidReader -> PadState -> PadEventSnapshotDispatcher / PadEventSnapshotProcessor shim -> IngressHub -> FrameAssembler -> DualPadRuntime -> InteractionEngine -> GameplayProjectionFrame -> PollOutputAdapter -> GameplayPresentationPublisher -> PromptRuntimeOwner -> SkyrimCompatibilitySurface / ScaleformPromptAdapter -> UpstreamGamepadHook -> XInputStateBridge`
- `src/input_v2/` 是唯一正式 runtime mainline；`PadEventSnapshotDispatcher / PadEventSnapshotProcessor` 只允许作为 shim / adapter。
- `AuthoritativePollState` 仅保留 legacy poll compatibility / XInput bridge 侧职责，不再作为 current mainline authority 描述。
- 当前 repo-owned 动态图标 surface 固定为 `ScaleformGlyphBridge -> ScaleformPromptAdapter -> PromptRuntimeOwner -> PromptService` 兼容路径；
  `ScaleformGlyphBridge` 与 `GlyphResolutionCompat` 不得恢复 `BindingManager`、trigger reverse lookup 或 menu fallback authority。
- `PH0` - `PH8b` closeout 已收口；当前无活跃 Sprint，不新增后续 runtime phase。
- `DP5` / `S-DP5` 是 planned post-closeout validation / governance hardening，不是新的 runtime phase。
- `FavoritesMenu` 页面级改造必须先恢复 workspace，再谈实现。
- 机器私有路径、Skyrim 实例路径和外部 live artifact 路径只写到 `AGENTS.win.md` / `AGENTS.mac.md`
- 共享文档一律使用 repo-relative 路径，不写机器私有绝对路径

## Validation Expectations

PH8b 之后的默认验证入口固定为：

- `scripts/ci/run_phase8_ci.ps1`
- `.github/workflows/dualpad-ci.yml`

默认 CI 必须直接引用同名 canonical targets：

- `DualPadReplayTests`
- `DualPadInputV2Tests`
- `DualPadIngressTests`
- `DualPadPromptSnapshotTests`
- `DualPadPropertyTests`
- `DualPadFuzzRegressionTests`
- `DualPadDocGen`

`docs/generated/*.md` 只能由 `DualPadDocGen` 生成；reviewed docs 只允许引用或解释这些 generated facts。

默认 close-out 还必须执行：

- `python -m json.tool .dualpad-builder/feature_list.json > $null`
- `python -m json.tool .dualpad-builder/sprint_plan.json > $null`
- `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`
- `git diff --check`

旧 focused prove-out 只作为对应历史 slice 或专项 regression 的支持性入口保留；它们不是 PH8b 之后默认最小证明，也不得替代同名 canonical targets。

默认 `xmake build DualPad` 与 `xmake build DualPadDInput8Proxy` 只产出 repo-local artifact，不写入本机 Skyrim / MO2 目录。本机部署必须显式启用 `dualpad_deploy=true` 并提供本机路径；这些路径只写入 `AGENTS.win.md` / `AGENTS.mac.md` 或本机配置，不进入共享 current truth。

`passes` 只能在对应验证实际执行并通过后改成 `true`。

## Definition Of Done

一个 Sprint / slice 只有同时满足下面条件才算完成：

- 当前范围内的代码、测试、文档已经落地
- 已执行该 slice 声明的验证，并且结果可复述
- 如果本轮改动涉及代码文件，已执行 `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`
- `.dualpad-builder/feature_list.json` 与 `.dualpad-builder/sprint_plan.json` 状态一致
- `.dualpad-builder/progress.md` 记录了开始、结论和风险
- 没有把历史 fallback、旧实验或缺失 workspace 包装成当前 repo truth
