# B07 已吸收项

本文记录 `B07 Processor Orchestration / Lifecycle / Source Blocking` 审核后，当前已经吸收或确认的结论。

## 已吸收

### F1 overflow 时缺少 mask 级 source-block 补偿

- 位置：
  - [PadEventSnapshotProcessor.cpp](/c:/Users/xuany/Documents/dualPad/src/input/injection/PadEventSnapshotProcessor.cpp)
- 问题：
  - 当 `PadEventBuffer` overflow 丢失了某个 `ButtonPress` 事件时
  - reducer 仍可能从 raw mask 看出 `pressedMask`
  - 但 processor 原先完全按事件驱动，因此不会 register lifecycle，也不会 block source
  - 结果是该位可能漏回 Skyrim 原生物理输入
- 当前修正：
  - 在 `syntheticFrame.overflowed` 时
  - 额外比对：
    - `syntheticFrame.pressedMask`
    - 与事件列表中真实观察到的 `ButtonPress` mask
  - 对“mask 级看到了新按下、但事件列表里没有 `ButtonPress`”的位，补做：
    - `SourceBlockCoordinator.Block(...)`
    - `handledButtons |= ...`
- 当前边界：
  - 这是一个**防御性补漏**
  - 只用于 overflow 场景
  - 不尝试在该路径上补完整 binding resolve / lifecycle register
  - 目标仅是防止物理输入泄漏给 Skyrim

## 已确认但本轮不改行为

### F2 Combo 预扫描的双重 Resolve

- 当前不改
- 原因：
  - 这是低优先级性能问题
  - 当前绑定规模很小，双重 `Resolve()` 开销可忽略
  - 若后续确实需要压微观性能，可再把 combo 预扫描结果缓存成可复用结构

### F3 Touchpad synthetic event 的 compat pulse 直通

- 当前不改
- 原因：
  - 现有高位 touchpad synthetic bit 不会被 Skyrim 原生识别成实际 gameplay/menu 动作
  - 风险主要在 diagnostics 噪音，不是当前运行时 bug
  - 是否继续收这条旁路，应结合更后面的 compat/legacy 清理批次再决定

## 本轮核实后的额外说明

- `PadEventSnapshotProcessor` 当前仍是可信的主线程 orchestration 层：
  - `Reset / overflow warn / sequence gap / Reduce / BeginFrame / CollectPlanned / CollectLifecycle / FinishFrame / compat fallback`
  顺序保持稳定
- `_framePlan` 仍是该层真实 dispatch 合同
- `_plannedNativeState` 当前仍只用于观测 / debug，不参与决策
- lifecycle-owned 动作的 `Press / Hold / Release` materialization 仍以 `SyntheticButtonState` 的显式 edge facts 为准

## 留给下一批继续确认

- `B08`
  - `ButtonEventBackend::ApplyPlannedAction()` 如何真正消费 lifecycle 产出的 `Press / Hold / Release`
  - `PollCommitCoordinator` 是否完整消费了 `gateAware / minDownMs / contextEpoch`
- `B09`
  - `CompatibilityInputInjector` 在使用 `handledButtons` 后，是否还存在边界重复注入
