# 映射层原子快照审计与注入层契约

本文专门对照 `AGENTS.md` 里的旧目标，核对当前代码的实际状态。重点回答两件事：

- 映射层是否已经输出“帧级原子事件快照”
- 注入层当前到底以什么契约消费这份快照并把输入送进游戏

本文只描述当前正式主线，不再把旧 `keyboard-native`、旧 `native button splice`、旧 `InputEventQueue` 拼接实验混入现行设计。

## 结论摘要

### 1. 映射层已经具备 producer 侧的帧级原子快照

当前 HID 线程上的流程是：

`PadState(previous/current) -> PadEventGenerator::Generate(...) -> PadEventBuffer -> PadEventSnapshot`

这条链路具备下面这些性质：

- 单次 `Generate(...)` 只基于一对 `previous/current PadState`
- 先 `Clear()` 再顺序生成整份 `PadEventBuffer`
- 最终把 `PadState + PadEventBuffer + sequence + timestamp` 一起封装进单个 `PadEventSnapshot`
- `SubmitSnapshot(...)` 提交的是整份 snapshot，而不是把其中事件拆成多段提交

因此，如果问题只问“映射层有没有生成单帧完整事件列表”，答案是 **有**。

### 2. 但 end-to-end 传递不是“严格逐帧、绝不异步”

当前 `PadEventSnapshot` 进入注入层前，还会经过：

`PadEventSnapshotDispatcher -> main-thread drain -> bounded budget -> backlog coalescing`

这意味着：

- snapshot 的消费单位仍然是“整份快照”
- 但它是 **异步排队** 到主线程消费的
- backlog 超出单次 Poll 预算时，会进行 **latest-state coalescing**

所以，如果要求是“事件列表一次性传给注入层，不分批或异步”，当前答案是 **不完全满足**。

更准确的说法应该是：

- **producer 侧**：单帧原子快照，成立
- **consumer 侧**：以整份 snapshot 为单位消费，成立
- **端到端严格逐帧直通**：当前不成立

### 3. 当前注入层正式契约已经独立于映射层实现

当前注入层不直接依赖 `PadEventGenerator / TriggerMapper / TouchpadMapper` 的内部细节。它只依赖统一输入契约：

- `PadEventSnapshot`
- `PadEventBuffer`
- `PadState`

也就是说，注入层的正式入口是：

`PadEventSnapshotProcessor::Process(const PadEventSnapshot&)`

这已经满足“注入层接口独立于映射层实现”的要求。

## 审计范围

本次核对主要基于这些文件：

- `src/input/HidReader.cpp`
- `src/input/mapping/PadEventGenerator.cpp`
- `src/input/mapping/AxisEvaluator.cpp`
- `src/input/mapping/TapHoldEvaluator.cpp`
- `src/input/mapping/ComboEvaluator.cpp`
- `src/input/mapping/TouchpadMapper.cpp`
- `src/input/mapping/TriggerMapper.cpp`
- `src/input/injection/PadEventSnapshot.h`
- `src/input/injection/PadEventSnapshotDispatcher.h`
- `src/input/injection/PadEventSnapshotDispatcher.cpp`
- `src/input/injection/PadEventSnapshotProcessor.h`
- `src/input/injection/PadEventSnapshotProcessor.cpp`
- `src/input/injection/SyntheticStateReducer.h`
- `src/input/injection/SyntheticStateReducer.cpp`
- `src/input/injection/SyntheticPadFrame.h`
- `src/input/AuthoritativePollState.h`
- `src/input/backend/NativeButtonCommitBackend.h`
- `src/input/backend/KeyboardHelperBackend.h`

## 映射层原子快照审计

### 1. 单帧事件列表的生成边界

`PadEventGenerator::Generate(...)` 的当前顺序是固定的：

1. `GenerateButtonEvents(...)`
2. `AxisEvaluator`
3. `TapHoldEvaluator`
4. `ComboEvaluator`
5. `TouchpadMapper`

这说明当前事件顺序不是“多个 mapper 各自异步推送”，而是单线程、固定顺序地写入同一个 `PadEventBuffer`。

`PadEventBuffer` 本身也是固定容量的 per-frame buffer，发生溢出时只会在这份 buffer 上记录：

- `overflowed`
- `droppedCount`

不会把多帧事件混写到同一个 buffer。

### 2. snapshot 的封装边界

在 `HidReader.cpp` 里，当前做法是：

1. 读取并归一化 `currentState`
2. 调 `eventGenerator.Generate(previousState, currentState, events)`
3. 直接构造 `PadEventSnapshot`
4. 把 `state/events/sourceTimestampUs/sequence` 一次性写进 snapshot
5. 调 `PadEventSnapshotDispatcher::SubmitSnapshot(snapshot)`

所以快照不是“只传事件、状态另算”，而是 **状态与事件一起封装**。

### 3. TriggerMapper 本身不破坏原子性

`TriggerMapper` 只做：

- `PadEvent -> Trigger`

它不排队、不缓存、不拆帧，也不会把一次 snapshot 拆成多个不同批次送下去。因此它不会破坏 producer 侧的原子快照属性。

## 当前注入层的真实消费语义

### 1. dispatcher 是异步主线程入口，不是直通调用

`PadEventSnapshotDispatcher` 当前语义是：

- HID 线程 submit snapshot
- 主线程 drain snapshot
- 每次 drain 有默认预算 `16`
- 超预算且 backlog 存在时，可能把剩余 pending snapshot 收敛成 latest-state coalesced snapshot

这一步是当前正式主线为了避免 backlog 和主线程长时间卡住做的折中。

因此“映射层生成原子快照”与“注入层严格逐帧消费每一份原始 snapshot”不是同一回事。

### 2. processor 消费的是整份 snapshot，而不是散事件

`PadEventSnapshotProcessor::Process(...)` 的当前结构是：

1. reset / sequence-gap 处理
2. `SyntheticStateReducer.Reduce(snapshot, context)`
3. 基于 `snapshot.events` 和 reduced frame 做 `FrameActionPlan`
4. lifecycle planning
5. native / keyboard helper dispatch
6. 将结果写入 `AuthoritativePollState`

这说明当前消费单位仍是 **整份 snapshot**，而不是把事件拆成多个独立 API 调用。

### 3. 当前已经不存在旧的 SyntheticPadState 主导注入层

`AGENTS.md` 里提到的 `SyntheticPadState` 更接近旧设计口径。当前代码里正式状态分成两层：

- `SyntheticPadFrame`
  - reducer 输出的“本次 reduced frame 事实”
- `AuthoritativePollState`
  - 最终统一虚拟手柄硬件状态

也就是说，当前正式主线已经不是：

`PadEvent list -> 一个长期可变 SyntheticPadState -> 注入`

而是：

`PadEventSnapshot -> SyntheticPadFrame -> FrameActionPlan / backend commit -> AuthoritativePollState -> transport`

## 当前注入层契约

## 1. 原生手柄线

当前正式原生线是：

`PadEventSnapshot`
-> `SyntheticStateReducer`
-> `FrameActionPlan`
-> `ActionLifecycleCoordinator`
-> `NativeButtonCommitBackend + AxisProjection + UnmanagedDigitalPublisher`
-> `AuthoritativePollState`
-> `UpstreamGamepadHook + XInputStateBridge`
-> `Skyrim Poll / producer / handler`

这个设计的核心是：

- 插件负责 materialize “虚拟 XInput 手柄硬件状态”
- Skyrim 自己负责从这份硬件状态推导 Jump / Sprint / AttackBlock / Menu 等原生语义

当前并不主张在插件里直接重建 `ButtonEvent / ThumbstickEvent / InputEventQueue` 主线。

### 2. 自定义事件线

当前自定义动作不是混在 native poll state 里，而是：

`FrameActionPlan -> ActionDispatcher -> ActionExecutor -> CustomActionDispatcher`

例如截图类插件功能就走这条线。

### 3. Mod 事件线

当前 `ModEvent / VirtualKey / FKey` 不走原生手柄线，而是：

`FrameActionPlan -> KeyboardHelperBackend -> KeyboardNativeBridge -> dinput8 proxy -> third-party mod`

目前公开逻辑槽位是 `ModEvent1-24`，不是旧文档里的 `ModEvent1-8`。

## 与 AGENTS 旧目标的对齐结果

### 已满足

- 映射层 producer 侧会生成单帧完整事件列表
- 事件顺序固定且和同一份 `PadState` snapshot 对齐
- 注入层入口独立于映射层内部实现
- 单帧 snapshot 会一次性作为一个对象交给注入层
- Press / Release / Hold / Tap / Combo / Gesture / Touchpad / Axis 都已有统一事件契约
- 长按、短按、组合键、摇杆、扳机、触控板都已进入正式主线
- `BindingConfig` 已明确拒绝 `FN + Face` 这类禁用 chord
- 有可选 debug 日志开关，能查看 mapping / action plan / synthetic frame / authoritative poll frame

### 部分满足

- “不分批或异步”
  - producer 侧满足
  - end-to-end 主线程交付不满足
- “不丢帧、不吞键”
  - 正常负载下当前实现已尽量保证
  - backlog 时会 bounded drain + coalescing，因此它依赖当前设计对“latest-state 优先”的接受，而不是严格逐原始帧回放

### 当前明确不按 AGENTS 旧说法执行

- 不再把 `ButtonEvent / ThumbstickEvent / InputEventQueue / BSInputDeviceManager` 直接注入当成默认主线
- 不再把 `ModEvent` 固定成 `1-8`
- 不再把 `SyntheticPadState` 作为唯一长期状态容器
- 不再把 Skyrim PC keyboard-exclusive 事件统一压回 `KeyboardHelperBackend`

## 当前最准确的设计结论

如果未来继续沿当前主线演进，应当把输入契约理解成：

### producer 侧契约

`PadState(previous/current) -> atomic per-frame PadEventSnapshot`

### transport 侧契约

`PadEventSnapshot` 以整份单位跨线程传递，但允许：

- 异步主线程 drain
- bounded drain
- backlog 时 latest-state coalescing

### consumer 侧契约

`PadEventSnapshot -> SyntheticPadFrame -> FrameActionPlan -> AuthoritativePollState / KeyboardHelperBackend`

因此，当前最稳妥的表述不是：

“系统已经保证每个 HID frame 的事件列表绝不异步、绝不 coalesce 地传给注入层”

而是：

“系统已经保证映射层输出单帧原子快照；注入层以整份快照为消费单位，但主线程交付是预算化、允许 coalescing 的。”

## 建议

### 1. 文档口径保持当前说法

后续所有文档应统一成下面这句：

- **映射层有 producer 侧帧级原子快照**
- **当前正式注入主线不是严格逐帧直通，而是 snapshot-unit + bounded drain + latest-state coalescing**

### 2. 如果未来要追求“严格每帧直通”

那需要改的是 dispatcher / transport contract，而不是去怀疑 `PadEventGenerator / TriggerMapper / TouchpadMapper` 有没有生成完整单帧事件列表。

### 3. 继续观察项

- 映射层 `Combo:*` 绑定还缺少专项实机验证
- `BindingResolver` 的 subset fallback 还未收紧
- `Hotkey3-8` 的 combo-native 仍待补测

这些都属于当前正式主线上的后续验证项，但不影响本文对“映射层帧级原子快照”结论的判断。
