# B06 已吸收项

本文记录 `B06 Routing / Plan Contract` 审核后已经吸收到当前代码的修正，以及仍需留在后续批次继续确认的项。

## 已吸收

### F1 同帧 `ButtonPress + Combo` 双触发

- 修正位置：
  - [PadEventSnapshotProcessor.cpp](/c:/Users/xuany/Documents/dualPad/src/input/injection/PadEventSnapshotProcessor.cpp)
- 当前做法：
  - 先预扫描同帧中**已成功 resolve 的 `Combo` 事件**
  - 对同一 `sourceCode` 的 `ButtonPress`，跳过 planner / lifecycle 注册
  - 但仍保留 `SourceBlockCoordinator.Block()`，确保原始物理键不会漏回 Skyrim
- 当前语义：
  - 只要同帧 `Combo` 已命中，同 code 的 `ButtonPress` 就不再作为独立动作继续下游
  - 这条规则同时覆盖 pulse 与 lifecycle-owned 数字动作，避免“planner 双发”和“lifecycle 间接双发”

### F2/F3 `FrameActionPlanner` 中 Pulse 死分支

- 修正位置：
  - [FrameActionPlanner.cpp](/c:/Users/xuany/Documents/dualPad/src/input/backend/FrameActionPlanner.cpp)
- 当前做法：
  - 删掉 `ResolveDigitalPolicy()` 中对 Pulse contract 的等效动作名分支
  - 删掉 `ResolveMinDownMs()` 中对 PulseMinDown 的等效动作名分支
- 结果：
  - planner 现在明确表达“当前所有 `ButtonEvent + NativeButton + Pulse` 默认都走 `PulseMinDown`”
  - 不再留下“看起来像差异化、实际没有差异”的误导代码

### F4/F5 Console 相关映射说明不足

- 修正位置：
  - [ActionBackendPolicy.cpp](/c:/Users/xuany/Documents/dualPad/src/input/backend/ActionBackendPolicy.cpp)
- 当前做法：
  - 为 `Console.Execute -> MenuConfirm`
  - 以及 `ConsoleHistoryUp/Down -> MenuScrollUp/Down`
  补了注释，明确这是**项目侧物理位复用/近似映射**，不是 vanilla 原生名字级一一对应。

## 已确认但未在本轮继续扩大处理

### `ownsLifecycle` 不是 Combo 防重的正确边界

`B06` 报告把问题范围描述为“所有 Pulse contract 的 ButtonEvent 动作”。实际代码核对后，发现只依赖 `ownsLifecycle` 并不足以说明防重边界：

- pulse 动作当然会直接 planner 双发
- lifecycle-owned 动作如果不抑制 `ButtonPress`，也会通过 `ActionLifecycleCoordinator` 在同帧间接 materialize 出第二个原始动作

因此当前修正没有继续扩大 `ownsLifecycle` 的语义，而是直接在 processor 层按“同帧 combo 已命中”做统一压制。

## 留给后续批次继续确认

- `gateAware / minDownMs / contextEpoch` 在 Poll commit 层的实际消费，仍应由 `B08` 继续核实
- `contract / lifecyclePolicy / digitalPolicy` 三层是否还存在进一步可压缩冗余，仍应结合 `PollCommitCoordinator` 的真实使用路径再判断
