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
          -> ButtonEventBackend
              -> PollCommitCoordinator
      -> CompatibilityInputInjector (legacy digital fallback + analog bridge)
  -> UpstreamGamepadHook
      -> XInputStateBridge
      -> Skyrim Poll producer
```

这条链里真正的数字主线是：

- `PadEventSnapshotProcessor`
- `FrameActionPlan`
- `ActionLifecycleCoordinator`
- `ButtonEventBackend`
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

- `src/input/backend/ButtonEventBackend.*`
- `src/input/backend/PollCommitCoordinator.*`
- `src/input/backend/INativeDigitalEmitter.h`

职责：

- 将 `PlannedAction` 翻译成 `PollCommitRequest`
- 维护 Pulse/Hold/Repeat/Toggle 的 Poll 可见性状态机
- 输出 `managedMask / semanticDownMask`

### 6. 最终桥接

- `src/input/injection/UpstreamGamepadHook.*`
- `src/input/XInputStateBridge.*`
- `src/input/SyntheticPadState.*`

职责：

- 在 `BSWin32GamepadDevice::Poll` 的 upstream call-site 上消费当前 synthetic state
- 将 analog state 与 committed digital state 合成最终 `XINPUT_STATE`

## 仍保留但不是主线的模块

### Compatibility / legacy

- `src/input/injection/CompatibilityInputInjector.*`
- `src/input/injection/LegacyNativeButtonSurface.*`
- `src/input/injection/NativeInputInjector.*`
- `src/input/injection/NativeInputPreControlMapHook.*`

这些模块仍存在，但当前只应视为：

- legacy digital fallback
- legacy native-button surface
- 调试 / 兼容 / 对比材料

它们不再代表默认数字主线。

### Keyboard-native

- `src/input/backend/KeyboardNativeBackend.*`
- `src/input/backend/KeyboardNativeBridge.*`

当前定位：

- helper backend
- mod / virtual key / 辅助键盘输出
- 不是 Skyrim PC 原生控制事件的默认主线

## 关键契约

- `FrameActionPlan` 是运行时合同。
- `ActionLifecycleTransaction` 是显式中间层，不是影子 plan。
- `ButtonEventBackend` 是 translator，不应重解释 routing contract。
- `PollCommitCoordinator` 只负责 Poll materialization。
- `XInputStateBridge` 只桥接最新 synthetic state，不重新做动作语义判断。

## 推荐配套文档

- `docs/DOC_INDEX_zh.md`
- `docs/final_native_state_backend_plan.md`
- `docs/backend_routing_decisions.md`
- `docs/unified_action_lifecycle_model_zh.md`
- `docs/native_pc_event_semantics_zh.md`
- `docs/poll_commit_coordinator_stage3_zh.md`
