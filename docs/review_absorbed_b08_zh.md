# B08 已吸收项

本文记录 `B08 ButtonEvent Backend / Poll Commit Coordinator` 审核后，当前已经吸收或确认的结论。

## 已吸收

### F1 `SubmitSyntheticActionLocked()` 是第二套元数据来源

- 位置：
  - [ButtonEventBackend.cpp](/c:/Users/xuany/Documents/dualPad/src/input/backend/ButtonEventBackend.cpp)
- 当前处理：
  - 已补注释，明确该方法只是 `IActionLifecycleBackend` 的 legacy bridge
  - 当前主管线应以：
    - `planner -> ApplyPlannedAction() -> TranslatePlannedActionToCommitRequest()`
    为准
- 当前边界：
  - 这条路径仍然保留，用于兼容直接调用 `TriggerAction()/SubmitActionState()`
  - 但不应再被视为 commit 元数据的权威来源

### F2 `repeatDelayMs / repeatIntervalMs / nextRepeatAtUs` 当前是预留字段

- 位置：
  - [PollCommitCoordinator.cpp](/c:/Users/xuany/Documents/dualPad/src/input/backend/PollCommitCoordinator.cpp)
- 当前处理：
  - 已补注释，明确当前 `RepeatOwner` 仍是：
    - 首 edge 保留
    - sustained current-state down
    - 后续 repeat 交给 Skyrim 原生 producer
  - `nextRepeatAtUs` 只是未来显式 cadence 的 groundwork，当前 `TickRepeatSlot()` 不主动消费它

### F3 `Emit()` 在 poll-owned 主线里只是 token 记账

- 位置：
  - [INativeDigitalEmitter.h](/c:/Users/xuany/Documents/dualPad/src/input/backend/INativeDigitalEmitter.h)
  - [ButtonEventBackend.cpp](/c:/Users/xuany/Documents/dualPad/src/input/backend/ButtonEventBackend.cpp)
- 当前处理：
  - 已补注释，明确：
    - `Emit()` 不是直接向游戏注入 `BSInputEvent`
    - 它只是 commit FSM 的 edge acknowledgement / token bookkeeping
    - 真正对游戏可见的 materialization 发生在 `CommitPollState()` 导出的 `semanticDownMask`

## 已确认但本轮不改行为

### lifecycle 路的 `Press / Hold / Release` 消费完整

本轮继续确认：

- `HoldOwner`
  - `Press/Hold -> HoldSet`
  - `Release -> HoldClear`
- `RepeatOwner`
  - `Press/Hold -> RepeatSet`
  - `Release -> RepeatClear`
- `ToggleDebounced`
  - 只接受 `Press/Pulse`
  - 不会在 `Hold` 再翻一次
- `PulseMinDown`
  - 只接受 `Press/Pulse`

这部分当前不需要改行为。

### `gateAware / minDownMs / contextEpoch` 都已真实消费

本轮继续确认：

- `gateAware`
  - 通过 `ShouldOpenGateForSlot()` 真实参与 gate 判定
- `minDownMs`
  - 通过 `earliestReleaseAtUs` 真实决定 pulse 最短可见时间
- `contextEpoch`
  - 通过 `BeginFrame()` 的 stale invalidation 真实参与 slot 清理/收尾

## 本轮核实后的额外说明

- `ButtonEventBackend` 当前已经接近真正的 bridge / translator：
  - 主主管线是 `ApplyPlannedAction()`
  - legacy bridge 仍存在，但已被明确降格
- `PollCommitCoordinator` 当前的 `RepeatOwner` 不是自主 cadence repeat
  - 它是阶段性实现
  - 行为上仍对齐“原生 producer 在 held current-state 下继续产生 repeat”

## 留给下一批继续确认

- `B09`
  - `UpstreamGamepadHook` 如何消费 `managedMask / semanticDownMask`
  - `CompatibilityInputInjector` 的 analog/digital fallback 与当前 ButtonEvent 主线是否完全互补
- 后续结构收敛项
  - 如果未来要继续减少重复元数据来源，可把 planner 默认值进一步上提成共享常量，逐步消除 legacy bridge 中的镜像值
