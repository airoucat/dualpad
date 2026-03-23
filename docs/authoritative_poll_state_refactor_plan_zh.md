# Authoritative Poll State 重构计划

## 目标

把当前“数字结果一份、模拟量结果一份、桥接时再合并”的结构，收口为：

- 一套事件/语义层
- 一份最终 authoritative poll state
- 两条正式输出线：
  - gamepad current-state transport
  - mod keyboard transport

不再让桥接层承担语义合并职责。

补充口径：

- `AuthoritativePollState` 的目标不是承载“插件侧游戏动作状态”
- 它应承载“虚拟 XInput 手柄硬件状态”
- Skyrim 原生 `producer / handler` 语义继续由游戏自己从这份硬件状态推导

## 当前进度

- `Phase 1-5` 已落地：
  - `AuthoritativePollState` 已进入正式主线
  - `NativeButtonCommitBackend` 已把 committed digital state 发布到统一 poll state
  - `PadEventSnapshotProcessor` 已直接把规划后的模拟量发布到统一 poll state
  - `XInputStateBridge` 已改成只读统一 poll state
  - `InputCompatBridge / NativeStateBackend / VirtualGamepadState / PluginActionBackend / ModEventBackend` 等旧壳层已退出运行时代码
  - 统一 poll contract 当前已补齐核心字段：
    - `down/pressed/released/pulse`
    - `unmanagedDown/unmanagedPressed/unmanagedReleased/unmanagedPulse`
    - `committedDown/committedPressed/committedReleased`
    - `managedMask`
    - `context/contextEpoch/sourceTimestampUs/pollSequence`
    - `overflowed/coalesced`
    - `move/look/trigger`

当前运行时已经达到计划目标：一份 authoritative poll state + 两条正式输出线。

## 当前问题

当前主线已统一，剩下的是后续优化题，不再是架构阻塞：

- unmanaged raw digital publish 仍是 `PadEventSnapshotProcessor` 内部的一个窄步骤
- 它不是平行 backend，但未来仍可继续评估是否还能再收窄

## 新方案分层

### 1. 输入采集层

- `HidReader`
- `PadState`

职责：

- 读取 DualSense 硬件状态
- 输出一份稳定的原始 snapshot

### 2. 映射层

- `PadEventGenerator`
- `TriggerMapper`
- `TouchpadMapper`

职责：

- 从单帧 `PadState` 生成 `PadEvent` 列表
- 保证事件顺序与 snapshot 对齐

### 3. 快照传输层

- `PadEventSnapshotDispatcher`

职责：

- 把 HID 线程快照安全转交到主线程
- 保留一帧级事件原子性

### 4. 语义层

- `SyntheticStateReducer`
- `FrameActionPlan`
- `ActionLifecycleCoordinator`

职责：

- 处理 `Pulse / Hold / Repeat / Toggle / Combo / Tap`
- 只负责动作语义，不直接写桥接状态

### 5. 状态提交层

建议拆成两个模块，但它们共同写入一份统一状态：

- `DigitalCommitModule`
  - 由当前 `NativeButtonCommitBackend + PollCommitCoordinator` 演化而来
  - 负责数字动作的 Poll 可见性
- `AnalogStateModule`
  - 由当前 `PadEventSnapshotProcessor` 内的 planned analog publish 演化而来
  - 负责 `Move / Look / Trigger`

说明：

- 对扳机，插件侧正式输出仍是 `bLeftTrigger / bRightTrigger`
- 不在插件里重做 `AttackBlockHandler` 的战斗状态机
- 如果内部需要阈值、调试或诊断信息，可以作为附属 metadata 保存，但不改变正式输出契约

### 6. 统一输出状态层

新增或升级一个统一对象，例如：

- `AuthoritativePollState`

建议字段：

- `wButtons`
- `bLeftTrigger / bRightTrigger`
- `sThumbLX / sThumbLY`
- `sThumbRX / sThumbRY`
- `pressedMask / releasedMask / pulseMask`
- `managedMask`
- `pollSequence`
- `contextEpoch`
- `sourceTimestampUs`
- 调试/诊断用 metadata

职责：

- 成为 Poll 时刻唯一 authoritative synthetic state
- 以“虚拟手柄硬件状态”作为正式契约
- bridge 层只读它，不再拼装语义

### 7. 传输层

#### 7.1 Gamepad transport

- `UpstreamGamepadHook`
- `XInputStateBridge`

职责：

- 在 `BSWin32GamepadDevice::Poll` 内部 call-site 写入最终 `XINPUT_STATE`
- 只读 `AuthoritativePollState`

#### 7.2 Mod transport

- `KeyboardHelperBackend`
- `KeyboardNativeBridge`
- `dinput8` proxy

职责：

- 只处理 `ModEvent / VirtualKey / FKey / 固定虚拟键池`
- 与 gamepad current-state 输出完全解耦

## 数据流

新方案下的数据传输应当是：

`PadState`
-> `PadEventSnapshot`
-> `FrameActionPlan`
-> `DigitalCommitModule + AnalogStateModule`
-> `AuthoritativePollState`
-> `Gamepad transport`

这条主线的含义是：

- 先把插件侧输入收敛成一份虚拟手柄硬件包
- 再让 Skyrim 自己从 `Poll -> producer -> handler` 推导原生动作
- 这套原则不仅适用于扳机，也适用于其它已具备标准 XInput 硬件身份的原生按钮

统一边界说明：

- 数字键、摇杆、扳机在 Skyrim 原生里并不是同一种 producer 形态
- 数字键更接近 `down / held` 驱动的 button event
- 摇杆更接近 `x / y` 二维向量事件
- 扳机更接近 `current / previous` 归一化值驱动的 trigger event
- 因此新架构统一的是“虚拟手柄硬件状态输出契约”，不是“在插件里把三类输入强行揉成同一套语义状态机”

`ModEvent` 则是：

`FrameActionPlan`
-> `KeyboardHelperBackend`
-> `KeyboardNativeBridge`
-> `dinput8 proxy`

## 模块收口策略

### 保留

- `PadEventSnapshotDispatcher`
- `SyntheticStateReducer`
- `FrameActionPlan`
- `ActionLifecycleCoordinator`
- `NativeButtonCommitBackend` 的数字语义/commit 职责
- `PadEventSnapshotProcessor` 的模拟量发布职能
- `KeyboardHelperBackend`
- `UpstreamGamepadHook`

### 重构

- `NativeButtonCommitBackend`
  - 从“直接给桥接层吐 committedButtons”
  - 变成“把标准手柄按钮结果写入统一 poll state”
- `PadEventSnapshotProcessor`
  - 从“planner 后处理器”
  - 变成“统一 poll state 的模拟量与 unmanaged raw digital 发布方”
- `XInputStateBridge`
  - 从“数字 + 模拟量合并器”
  - 变成“单纯 serializer / transport writer”

### 退场目标

- 旧 compat/native-state 壳层退出运行时代码

注意：

- 本轮代码清理后，旧 `NativeInputPreControlMapHook / NativeInputInjector / LegacyNativeButtonSurface` 实验链已不再是未来方案的一部分

## 分阶段实施

### Phase 1

- 定义 `AuthoritativePollState`
- 明确字段、所有权与 frame/poll 生命周期

### Phase 2

- 让 `NativeButtonCommitBackend / PollCommitCoordinator` 写入统一状态的数字部分
- 保持现有行为完全不变

### Phase 3

- 让模拟量直接写入统一状态
- 去掉旧 native-state 中转层

### Phase 4

- 改造 `XInputStateBridge`
- 只从统一状态读取
- 移除 bridge 层的数字/模拟量合并逻辑

### Phase 5

- 让 raw/unmanaged digital publish 收敛为纯内部补写步骤
- 清理 compat/native-state 壳层与空 backend

## 验收标准

- 每 Poll 只存在一份 authoritative synthetic state
- 数字动作语义与当前版本一致
- 模拟量延迟不高于当前版本
- `XInputStateBridge` 不再做动作语义判断
- 原生线输出的正式契约是虚拟手柄硬件状态，不是插件侧 gameplay action 表
- 正式输出口径稳定为两条：
  - handpad native current-state
  - mod keyboard events
