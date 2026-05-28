# DualPad Builder Spec Snapshot

## Objective

在当前 `authoritative baseline` 下，把 `DualPad` 作为 `Skyrim SE 1.5.97 / CommonLibSSE-NG` 的 DualSense 输入重构项目维护在 PH0-PH8b closeout 后的稳定基线；默认执行协议固定为 `harness + ce + graphify`，让 planning、implementation、review 和状态同步在不同会话里保持一致。

## Product Surfaces

- `DP1` Runtime input pipeline truth
- `DP2` Menu context policy and gameplay/menu ownership
- `DP3` Native routing, controlmap combo overlay, and mod-event helper
- `DP4` Dynamic glyph and menu presentation surfaces
- `DP5` Validation, cleanup, and workflow honesty

## Hard Constraints

- current truth 入口根目录固定为 `docs/authoritative-baseline/`
- 当前唯一正式支持面仍是 `Skyrim SE 1.5.97`
- 当前正式 runtime mainline 是：
  - `HidReader -> PadState -> PadEventSnapshotDispatcher / PadEventSnapshotProcessor shim -> IngressHub -> FrameAssembler -> DualPadRuntime -> InteractionEngine -> GameplayProjectionFrame -> PollOutputAdapter -> GameplayPresentationPublisher -> PromptRuntimeOwner -> SkyrimCompatibilitySurface / ScaleformPromptAdapter -> UpstreamGamepadHook -> XInputStateBridge`
- `src/input_v2/` 是唯一正式 runtime mainline；`PadEventSnapshotDispatcher / PadEventSnapshotProcessor` 只允许作为 shim / adapter。
- `AuthoritativePollState` 仅保留 legacy poll compatibility / XInput bridge 侧职责，不再作为 current mainline authority 描述
- `PH0` - `PH8b` closeout 已收口；当前无活跃 Sprint，不新增后续 runtime phase
- 当前 repo-owned 动态图标 surface 固定为主菜单 `startmenu.swf`、共享 `ScaleformGlyphBridge` shim、`ScaleformPromptAdapter`、`PromptRuntimeOwner` 和 `PromptService`
- `ScaleformGlyphBridge` / `GlyphResolutionCompat` 不得恢复 `BindingManager`、trigger reverse lookup 或 menu fallback authority
- `FavoritesMenu` 的专项 SWF workspace 当前不在 repo 内；如果任务落到这个页面，必须先恢复 workspace 再继续
- planning / implementation / review 默认采用 `Planner -> ce:plan`、`Generator -> ce:work`、`Evaluator -> ce:review`
- 只要本轮改动涉及代码文件，收尾前必须执行：
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`
- Graphify 只负责上下文加速，不替代 baseline 文档、`README.md`、`src/ARCHITECTURE.md` 或 `.dualpad-builder/`
- 共享文档只写 repo-relative 路径，不写机器私有绝对路径

## Requirements Snapshot

- `B1` `DP1` 必须把 runtime input pipeline truth 保持在 `src/input_v2/` 单主线；legacy poll compatibility 只能作为 bridge side effect 描述，不能重新成为 current mainline authority
- `B2` `DP2` 必须把菜单上下文策略和 `Gameplay -> Menu` ownership truth 与已知异常分开记录
- `B3` `DP3` 必须把 native routing、controlmap combo overlay 和 `ModEvent` helper backend 的正式支持面说清楚
- `B4` `DP4` 必须按当前 repo 真正拥有的 prompt/glyph compatibility surface 推进：`ScaleformGlyphBridge -> ScaleformPromptAdapter -> PromptRuntimeOwner -> PromptService`
- `B5` `DP5` 必须把验证、cleanup、handoff 和 workflow honesty 串成 post-closeout hardening 链；它不是新的 runtime phase
- `B6` 所有默认工作流都必须同步更新 `.dualpad-builder/` 记忆层
- `B7` Graphify 本地自动化必须可初始化、可重建、可查询

## Non-Goals

- 不把旧 `keyboard-native` 或 `XInputGetState` fallback 重新包装成默认主线
- 不恢复 `BindingManager`、trigger reverse lookup 或 menu fallback 作为当前 glyph authority
- 不新增后续 runtime phase
- 不把缺失的 `FavoritesMenu` workspace 写成当前 repo truth
- 不跳过 planning / review / memory sync，直接把会话当一次性脚本执行
- 不把机器私有路径、MO2 部署路径或外部 live artifact 路径写进共享文档
- 不把 graphify 图结果当成 authoritative contract

## Delivery Units

- `WF0` Harness + CE + Graphify workflow bootstrap
- `DP1` Runtime input pipeline truth
- `DP2` Menu context policy and gameplay/menu ownership
- `DP3` Native routing, controlmap combo overlay, and mod-event helper
- `DP4` Dynamic glyph and menu presentation surfaces
- `DP5` Post-closeout validation, cleanup, and workflow honesty hardening

## Done Definition

- `WF0`、`DP1-DP5` 的状态与验证结果都能在 `.dualpad-builder/` 中追溯
- 若存在当前激活的 Sprint / slice，必须有明确退出标准和验证入口；当前 PH8b baseline 下 `current_sprint=null`
- `passes` 只在对应验证实际通过后更新
- 代码工作结束前完成 graphify close-out
- 最终 handoff 不把历史 fallback、旧实验或缺失 workspace 冒充成当前真相
