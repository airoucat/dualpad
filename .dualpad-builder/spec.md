# DualPad Builder Spec Snapshot

## Objective

在当前 `authoritative baseline` 下，把 `DualPad` 作为 `Skyrim SE 1.5.97 / CommonLibSSE-NG` 的 DualSense 输入重构项目持续推进；默认执行协议固定为 `harness + ce + graphify`，让 planning、implementation、review 和状态同步在不同会话里保持一致。

## Product Surfaces

- `DP1` Runtime input pipeline truth
- `DP2` Menu context policy and gameplay/menu ownership
- `DP3` Native routing, controlmap combo overlay, and mod-event helper
- `DP4` Dynamic glyph and menu presentation surfaces
- `DP5` Validation, cleanup, and workflow honesty

## Hard Constraints

- current truth 入口根目录固定为 `docs/authoritative-baseline/`
- 当前唯一正式支持面仍是 `Skyrim SE 1.5.97`
- 当前正式运行时主线是：
  - `HidReader -> PadState -> PadEventSnapshot -> FrameActionPlan -> ActionLifecycleCoordinator -> NativeButtonCommitBackend -> AuthoritativePollState -> UpstreamGamepadHook -> XInputStateBridge`
- `Gameplay presentation owner` Phase 2 已并入默认主线，不再当成运行时 toggle
- 当前 repo-owned 动态图标 surface 只以主菜单 `startmenu.swf` 和共享 `ScaleformGlyphBridge` 为准
- `FavoritesMenu` 的专项 SWF workspace 当前不在 repo 内；如果任务落到这个页面，必须先恢复 workspace 再继续
- planning / implementation / review 默认采用 `Planner -> ce:plan`、`Generator -> ce:work`、`Evaluator -> ce:review`
- 只要本轮改动涉及代码文件，收尾前必须执行：
  - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`
- Graphify 只负责上下文加速，不替代 baseline 文档、`README.md`、`src/ARCHITECTURE.md` 或 `.dualpad-builder/`
- 共享文档只写 repo-relative 路径，不写机器私有绝对路径

## Requirements Snapshot

- `B1` `DP1` 必须把运行时输入主链、生命周期边界和 `AuthoritativePollState` 口径保持在当前主线上
- `B2` `DP2` 必须把菜单上下文策略和 `Gameplay -> Menu` ownership truth 与已知异常分开记录
- `B3` `DP3` 必须把 native routing、controlmap combo overlay 和 `ModEvent` helper backend 的正式支持面说清楚
- `B4` `DP4` 必须按当前 repo 真正拥有的 glyph bridge / 页面 surface 推进，不得假设缺失的 `FavoritesMenu` workspace 已存在
- `B5` `DP5` 必须把验证、cleanup、handoff 和 workflow honesty 串成可复述的 close-out 链
- `B6` 所有默认工作流都必须同步更新 `.dualpad-builder/` 记忆层
- `B7` Graphify 本地自动化必须可初始化、可重建、可查询

## Non-Goals

- 不把旧 `keyboard-native` 或 `XInputGetState` fallback 重新包装成默认主线
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
- `DP5` Validation, cleanup, and workflow honesty

## Done Definition

- `WF0`、`DP1-DP5` 的状态与验证结果都能在 `.dualpad-builder/` 中追溯
- 当前激活的 Sprint / slice 有明确退出标准和验证入口
- `passes` 只在对应验证实际通过后更新
- 代码工作结束前完成 graphify close-out
- 最终 handoff 不把历史 fallback、旧实验或缺失 workspace 冒充成当前真相
