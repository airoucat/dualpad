# B09 已吸收项

本文记录 `B09 Upstream State Bridge / XInput State Bridge / Compatibility Bridge` 审核后，当前已经吸收或确认的结论。

## 本轮结论

`B09` 没有发现必须立刻修改行为的 blocker。当前最适合吸收的是把几处容易被误解的桥接语义写清楚，避免后续把“latest-known current-state cache”误判成“时序错位 bug”。

## 已吸收

### `XInputStateBridge` 的时间基准说明

- 位置：
  - [XInputStateBridge.cpp](/c:/Users/xuany/Documents/dualPad/src/input/XInputStateBridge.cpp)
- 当前处理：
  - 已补注释，明确：
    - `UpstreamGamepadHook` 会在同一个 Poll thunk 里先 `DrainOnMainThread()`
    - 然后才调用 `FillSyntheticXInputState()`
    - 因此 `SyntheticPadState::ConsumeFrame()` 和 `ButtonEventBackend::CommitPollState()` 读取的是同一 Poll 窗口内的 latest-known state
- 当前判断：
  - 审核里的 `F1` 指出的“上一帧/当前帧时序错位”并不直接成立
  - 当前模型更准确的说法是：
    - `Compatibility` 提供 latest-known fallback current-state
    - `ButtonEvent` 提供 current poll-owned managed state
    - bridge 负责按 `managedMask` 做优先级合并

### `SyntheticPadState::ConsumeFrame()` 的非破坏性读取语义

- 位置：
  - [SyntheticPadState.h](/c:/Users/xuany/Documents/dualPad/src/input/SyntheticPadState.h)
- 当前处理：
  - 已补注释，明确 `ConsumeFrame()` 是非破坏性读取
  - `heldDown` 持续保留直到显式清除
  - `pulseDown` 由过期时间驱动自清理
- 当前判断：
  - 审核里的 `F4` 更接近命名/语义提示问题，不是运行时 bug

### `CompatibilityInputInjector::_virtualHeldDown` 的用途说明

- 位置：
  - [LegacyDigitalBackfill.h](/c:/Users/xuany/Documents/dualPad/src/input/injection/LegacyDigitalBackfill.h)
- 当前处理：
  - 已补注释，明确它用于 `CompatibilityFallback` 计划动作的 stateful synthetic holds
  - 也就是 `ActionDispatcher` 里 `CompatibilityState` 那条 `Press/Hold/Release` materialization
- 当前判断：
  - 审核里的 `U2` 不是缺调用链，而是此前代码语义没有写清楚

## 已确认但本轮不改行为

### F2 `handledButtons` 清理只能向前止漏，不能回收历史泄漏

- 当前不改
- 原因：
  - 这是 compatibility fallback 的已知阶段性边界
  - 并不构成“旧路线重新成为主线”的直接证据
  - 若后续要继续收紧，应结合更完整的 compat fallback 缩减方案一起做，而不是在本轮孤立改时序

### F3 `PulseButton()` 的 50ms 固定窗口

- 当前不改
- 原因：
  - 这是 compatibility/raw fallback 的窗口，不是 ButtonEvent 主线的 pulse 时长合同
  - 目前缺少运行时证据表明它已造成真实回归
  - 若后续要继续统一 pulse 手感，应在 compat/legacy 收敛时整体调整

### F5 `hasAxis == false` 时归零

- 当前不改
- 原因：
  - 当前策略明确偏向“避免模拟量残留”
  - 审核里提到的是潜在边缘抖动风险，不是现有主线已证实 bug

## 继续有效的总体判断

- `UpstreamGamepadHook` 仍是默认数字主线的唯一有效入口
- `XInputStateBridge` 负责：
  - `legacyDownMask = frame.downMask & ~managedMask`
  - `combinedDownMask = legacyDownMask | semanticDownMask`
- `CompatibilityInputInjector` 当前仍是 legacy/raw fallback，而不是默认数字主线
- `XInputHapticsBridge` 只处理 `XInputSetState` 震动桥接，不再承担输入主线

## 留给后续批次继续确认

- 如果后面还要继续审核：
  - `B10/B11` 可进一步检查 legacy surface 是否还能再收薄
- 如果后面要继续做行为收敛：
  - 再结合真实运行日志判断 compatibility pulse 50ms 是否需要和主线 pulse 策略统一
