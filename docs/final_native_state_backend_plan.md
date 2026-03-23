# Final Native-State Backend Plan

## 目标

维持一条稳定、可调试、可继续扩展的输入主线：

- 一个 DualSense 输入快照只进入一次主线程规划
- 动作语义先于 backend 决定
- 数字动作和模拟量各走清晰 owner
- 插件动作、mod 事件与 Skyrim control routing 解耦
- unmanaged raw digital publish 保留，但不污染主线

## 当前已经落地的主线

### Snapshot ingress

- `HidReader` 读取 DualSense 报文
- 协议层解出 `PadState`
- 映射层生成 `PadEventSnapshot`
- `PadEventSnapshotDispatcher` 把 snapshot 转交主线程

### Main-thread planning

- `SyntheticStateReducer` 生成 `SyntheticPadFrame`
- `BindingResolver / TriggerMapper / TouchpadMapper` 做触发器解析
- `FrameActionPlanner` 生成 `PlannedAction`
- `ActionLifecycleCoordinator` 负责 lifecycle-owned 动作
- `ActionLifecycleTransaction` 显式承接 lifecycle 事实

### Digital mainline

- `NativeButtonCommitBackend` 作为 `PlannedAction -> PollCommitRequest` translator
- `PollCommitCoordinator` 负责 Poll 可见性 materialization
- 标准手柄数字位发布到 `AuthoritativePollState`

### Analog mainline

- `PadEventSnapshotProcessor` 内部的 planned analog publish
- `AuthoritativePollState`
- 对扳机，正式输出是 `LT/RT` 硬件字节，不在插件里重做 `AttackBlockHandler`

### Final bridge

- `UpstreamGamepadHook` 在 `BSWin32GamepadDevice::Poll` 的 upstream call-site 中：
  1. `DrainOnMainThread()`
  2. 提交并读取最新 unified poll state
  3. 填充 `XINPUT_STATE`

## 当前已经明确不是主线的路线

- `XInputGetState` IAT 输入 fallback
- consumer-side `ButtonEvent` 队列拼接
- `keyboard-native` 作为 Skyrim PC 原生事件主线
- legacy native button splice 重新回流为默认实现

## 当前架构不变式

### 1. `FrameActionPlan` 是运行时合同

planner 决定：

- `actionId`
- `backend`
- `contract`
- `phase`
- `outputCode`
- `contextEpoch`

dispatch 和 commit 层不应再重新解释 routing contract。

### 2. 数字主线采用 Poll-owned ownership

当前设计的核心不是“直接往游戏事件队列塞动作名”，而是：

- 先维护一份 Poll 时刻可见的数字 current-state
- 再让 Skyrim 自己的 producer 从 current-state 推导原生行为

补充：

- `AuthoritativePollState` 表示的是虚拟 XInput 手柄硬件状态
- 这套原则同样适用于摇杆与扳机
- 对扳机，插件侧只负责稳定输出 `LT/RT`，原生 `Attack / Block / Dual Attack / ForceRelease` 由游戏自己处理

### 3. lifecycle 与 commit 分层

- `ActionLifecycleCoordinator` 负责 lifecycle 事实
- `ActionLifecycleTransaction` 负责显式中间表达
- `PollCommitCoordinator` 只负责 Poll visible materialization

### 4. unmanaged raw digital publish 必须降级

当前只剩 `PadEventSnapshotProcessor` 内部的 unmanaged raw digital publish 步骤仍在主树中，且它只能保留为：

- unmanaged raw digital fallback
- reverse / diagnostics
- 对比材料

补充：

- 它现在只消费 `SyntheticPadFrame` 已缩减出的 raw digital edge 事实
- 不再承担 touchpad/gesture 的 dispatcher 旁路脉冲职责
- `CompatibilityFallback` 已退出当前运行时代码里的 backend 枚举与默认 planner 兜底

旧 consumer-side native-button experiment 代码入口已经删除，不能重新长回平行主线。

## 仍未解决但已明确归位的问题

### 1. 跨按钮微小时序

例如：

- `Menu.ScrollDown -> Menu.Confirm`
- 方向键下 + `A` 快速连按

当前已经确认：

- 单键 first-edge 保留已成立
- 剩余问题属于跨按钮时序，不是单键 pulse 丢失

未来只能走两条之一：

- planner/commit 边界上的窄保序 contract
- 极少数顺序敏感 UI 场景的 `direct native event`

### 2. 游戏侧 transition/readiness

例如：

- `Loading / Fader / menu transition` 尾巴

当前只记录为 future reverse target，不在当前主线继续硬修。

### 3. Upstream native-state 进一步逆向

当前 `poll-xinput-call` 已经够用，但更上游、更窄、更 native 的 state boundary 仍值得继续逆向验证。

### 4. 硬件序列号 gap 检测

已经记为 TODO，等待之后在 HID / snapshot 层升级时接入。

## 后续阶段

### Phase 4

在不破坏当前主线的前提下，只处理两类 future work：

- 跨动作保序 contract
- 少量 direct native event 场景

### Phase 5

继续做 upstream native-state reverse，目标是：

- 更窄的 hook 边界
- 更少的 bridge 假设
- 更明确的与 Skyrim producer 对齐

### Phase 6

对 keyboard-native / mod-helper 路线继续收窄职责，而不是重新上升为默认控制主线。

另见：

- [authoritative_poll_state_refactor_plan_zh.md](authoritative_poll_state_refactor_plan_zh.md)
  - 统一最终输出状态的新方案重构计划
