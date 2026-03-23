# DualPad Input Architecture

## 当前默认主线

```text
HidReader
  -> PadState
  -> PadEventGenerator
  -> PadEventSnapshotDispatcher
  -> PadEventSnapshotProcessor
      -> SyntheticStateReducer
      -> BindingResolver / TriggerMapper / TouchpadMapper
      -> FrameActionPlan
      -> ActionLifecycleCoordinator
      -> ActionLifecycleTransaction
      -> ActionDispatcher
          -> NativeButtonCommitBackend
              -> PollCommitCoordinator
      -> native analog publish (inside PadEventSnapshotProcessor)
      -> unmanaged raw digital publish (inside PadEventSnapshotProcessor)
      -> AuthoritativePollState
  -> UpstreamGamepadHook
      -> XInputStateBridge
      -> Skyrim Poll producer
```

这条链里真正的数字主线是：

- `PadEventSnapshotProcessor`
- `FrameActionPlan`
- `ActionLifecycleCoordinator`
- `NativeButtonCommitBackend`
- `PollCommitCoordinator`
- `XInputStateBridge`

## 模块分层

### 1. HID / 协议 / 归一化

- `src/input/HidReader.*`
- `src/input/hid/*`
- `src/input/protocol/*`
- `src/input/state/*`

职责：

- 读取 DualSense HID 报文
- 解出 `PadState`
- 保留 raw 与 normalized 数据

### 2. 映射层

- `src/input/mapping/*`

职责：

- 生成一份与单个 HID snapshot 对齐的 `PadEventBuffer`
- 输出 `ButtonPress / ButtonRelease / AxisChange / Hold / Tap / Combo / Gesture`

### 3. Snapshot 与 reduction

- `src/input/injection/PadEventSnapshot.*`
- `src/input/injection/PadEventSnapshotDispatcher.*`
- `src/input/injection/SyntheticPadFrame.h`
- `src/input/injection/SyntheticStateReducer.*`

职责：

- 把 HID 线程事件快照安全转交到主线程
- 从事件列表与 raw state 缩减出 `SyntheticPadFrame`
- 保留 `downMask` 和 transient edge facts

### 4. 规划与生命周期

- `src/input/backend/FrameActionPlan.*`
- `src/input/backend/FrameActionPlanner.*`
- `src/input/backend/ActionBackendPolicy.*`
- `src/input/backend/ActionLifecycleCoordinator.*`
- `src/input/backend/ActionLifecycleTransaction.h`
- `src/input/ActionDispatcher.*`

职责：

- 解析绑定与上下文
- 生成 `PlannedAction`
- 对 lifecycle-owned 动作显式产出 `LifecycleTransaction`
- 按已定好的 `backend/contract/phase/outputCode` 分发

### 5. 数字主线

- `src/input/backend/NativeButtonCommitBackend.*`
- `src/input/backend/PollCommitCoordinator.*`
- `src/input/backend/IPollCommitEmitter.h`

职责：

- 将 `PlannedAction` 翻译成 `PollCommitRequest`
- 维护 Pulse/Hold/Repeat/Toggle 的 Poll 可见性状态机
- 输出标准手柄硬件位对应的 committed digital state
- 不直接把 gameplay action 语义写成桥接层最终状态

### 6. 最终桥接

- `src/input/injection/UpstreamGamepadHook.*`
- `src/input/XInputStateBridge.*`
- `src/input/AuthoritativePollState.*`

职责：

- 汇总 Poll 时刻唯一 authoritative synthetic state
- 该状态的正式口径是“虚拟 XInput 手柄硬件包”
- 在 `BSWin32GamepadDevice::Poll` 的 upstream call-site 上序列化最终 `XINPUT_STATE`

## 仍保留但不是主线的模块

### Unmanaged Raw Digital Publish

当前仍保留一个内部补写步骤：

- `PadEventSnapshotProcessor` 内部的 unmanaged raw digital publish

说明：

- 它只根据 `SyntheticPadFrame` 的 raw digital edge 事实补写统一 `AuthoritativePollState`
- 不再承担模拟量中转职责
- 不再接受 dispatcher 旁路 pulse/state 写入
- 它不是平行 backend，也不是默认数字主线，只是统一 poll contract 的未接管位补写步骤

历史上与 `CompatibilityFallback` / `native-button experiment` 相关的运行时代码入口都已移除。

### Keyboard helper

- `src/input/backend/KeyboardHelperBackend.*`
- `src/input/backend/KeyboardNativeBridge.*`

当前定位：

- helper backend
- mod / virtual key / 辅助键盘输出
- 不是 Skyrim PC 原生控制事件的默认主线

说明：

- 正式设计名称采用 `KeyboardHelperBackend`
- 当前代码类名已统一为 `KeyboardHelperBackend`

### Input modality

- `src/input/InputModalityTracker.*`

当前定位：

- 参考 AutoInputSwitch 的模式层
- 放开键盘/鼠标与手柄同时生效
- 驱动 `IsUsingGamepad / GamepadControlsCursor / remap-mode` 的平台判断
- 忽略 `KeyboardHelperBackend` 自发的 simulated keyboard 事件，避免 helper 输出把平台误切到 KBM

## 关键契约

- `FrameActionPlan` 是运行时合同。
- `ActionLifecycleTransaction` 是显式中间层，不是影子 plan。
- `NativeButtonCommitBackend` 是 translator，不应重解释 routing contract。
- `PollCommitCoordinator` 只负责 Poll materialization。
- `AuthoritativePollState` 是 gamepad 正式输出的唯一最终状态，且表示的是虚拟手柄硬件状态，不是插件动作状态。
- `XInputStateBridge` 只桥接最新 synthetic state，不重新做动作语义判断。
- 扳机、摇杆与标准按钮都优先按硬件位/轴 materialize，再交给 Skyrim 自己的 producer / handler 推导原生 user event。

## 推荐配套文档

- `docs/DOC_INDEX_zh.md`
- `docs/final_native_state_backend_plan.md`
- `docs/backend_routing_decisions.md`
- `docs/unified_action_lifecycle_model_zh.md`
- `docs/native_pc_event_semantics_zh.md`
- `docs/poll_commit_coordinator_stage3_zh.md`

