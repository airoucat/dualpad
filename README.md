# DualPad

Skyrim SE 1.5.97 / CommonLibSSE-NG 的 DualSense 输入重构项目。

当前项目已经从早期的 `keyboard-native` 与旧 `native button splice` 实验，收口到一条更明确的默认主线：

- 输入采集：`HidReader -> PadState`
- 映射快照：`PadEventGenerator -> PadEventSnapshot`
- 主线程规划：`SyntheticStateReducer -> FrameActionPlan -> ActionLifecycleCoordinator`
- 数字主线：`ButtonEventBackend -> PollCommitCoordinator`
- 最终桥接：`UpstreamGamepadHook -> XInputStateBridge`
- 模拟量桥接：`CompatibilityInputInjector::SubmitAnalogState() -> SyntheticPadState`

当前默认运行时不再依赖：

- `XInputGetState` IAT 输入 fallback
- `keyboard-native` 作为 Skyrim PC 原生事件主线
- consumer-side `ButtonEvent` 队列拼接作为默认实现

仍保留但已降级为辅助/遗留面的有：

- `KeyboardNativeBackend`
- `LegacyNativeButtonSurface`
- `NativeInputInjector`
- `CompatibilityInputInjector` 的 legacy digital fallback

## 当前主线原则

- `FrameActionPlan` 是运行时合同，不是影子日志。
- 数字主线采用 `Poll-owned current-state ownership`。
- `ButtonEventBackend` 只做 `PlannedAction -> PollCommitRequest` 翻译。
- `PollCommitCoordinator` 只负责 Poll 可见性 materialization。
- `KeyboardNativeBackend` 继续保留，但主要用于 `ModEvent / VirtualKey / F13-F20 / 辅助键盘输出`。
- `PluginActionBackend` 负责截图、快速存读档、菜单直开等插件行为。
- `ModEventBackend` 预留给未来 mod-facing 事件。

## 文档入口

- [docs/DOC_INDEX_zh.md](docs/DOC_INDEX_zh.md)
  - 当前文档总索引与推荐阅读顺序
- [docs/final_native_state_backend_plan.md](docs/final_native_state_backend_plan.md)
  - 当前主线、剩余阶段和长期目标
- [docs/backend_routing_decisions.md](docs/backend_routing_decisions.md)
  - 当前 backend ownership 与 routing 规则
- [docs/unified_action_lifecycle_model_zh.md](docs/unified_action_lifecycle_model_zh.md)
  - 统一动作生命周期模型
- [docs/native_pc_event_semantics_zh.md](docs/native_pc_event_semantics_zh.md)
  - 已追到的 Skyrim 原生 PC / gamepad user event 语义
- [docs/plan_a_long_term_edge_lifecycle_zh.md](docs/plan_a_long_term_edge_lifecycle_zh.md)
  - 在 `agents2/agents3` 约束下的长期 edge 生命周期方案
- [docs/poll_commit_coordinator_stage3_zh.md](docs/poll_commit_coordinator_stage3_zh.md)
  - `LifecycleTransaction -> PollCommitCoordinator` 第三阶段落地说明
- [src/ARCHITECTURE.md](src/ARCHITECTURE.md)
  - 当前代码模块与主链路总览

## 已知后续点

- `Menu.Scroll* -> Menu.Confirm` 这类跨按钮微小时序，当前仍是 future work。
- 少数顺序敏感 UI 场景，未来可能需要 `direct native event`，但当前不作为主线。
- 游戏侧 `transition/readiness` 机制仍只记录为后续逆向点，不在当前窗口继续硬推。
- HID 硬件序列号 gap 检测已记 TODO，等待后续协议/状态层升级时再接入。
- 调度层未来可考虑做 `snapshot coalescing` 优化：
  - 目标应是“一个 Poll 尽量反映 latest state，同时保留中间的瞬时边沿事实”。
  - 不建议走“每个 Poll 只处理一个旧 snapshot”的方向，以免放大 backlog 与输入延迟。

## 历史路线现状

- 旧 `keyboard-native` 主线实验：已归档，不再作为默认设计。
- 旧 `action-contract-output` 过渡实验：已被 `FrameActionPlan + PollCommitCoordinator` 替代。
- 旧 `native input reverse targets` / `upstream XInput experiment` 文档：已被当前架构文档与 review 吸收。
