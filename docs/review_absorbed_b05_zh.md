# B05 已吸收项

本文记录 `B05 Synthetic State Reduction / Frame Reduction Contract` 审核后，当前已经吸收或确认的结论。

## 结论

`SyntheticPadFrame` 这一层当前可以继续视为可信的单帧缩减合同，不需要先停下来重构；本轮没有发现必须立即修改运行时行为的 blocker。

## 已吸收

### F1 `pressedAtUs` / `firstPressUs` 语义澄清

- 位置：
  - [SyntheticPadFrame.h](/c:/Users/xuany/Documents/dualPad/src/input/injection/SyntheticPadFrame.h)
- 处理方式：
  - 补充了字段注释，明确区分：
    - `pressedAtUs / releasedAtUs`
      - 来自跨帧净 mask 变化
      - 用于持续按住（held）这类 mask-level 生命周期
    - `firstPressUs / lastReleaseUs`
      - 来自当前缩减帧内显式观测到的边沿
      - 对 pulse / same-frame press+release 更权威
- 当前运行时判断：
  - 这不是立即的运行时 bug
  - 因为 [ActionLifecycleCoordinator.cpp](/c:/Users/xuany/Documents/dualPad/src/input/backend/ActionLifecycleCoordinator.cpp)
    在 release 持续时间计算里，已经优先取 `firstPressUs / lastReleaseUs`，只有缺失时才回退到 `pressedAtUs / releasedAtUs`

## 已确认但本轮不改行为

### F2 pulse 按钮 `heldSeconds` 为 0

- 当前不改
- 原因：
  - 现有下游对 pulse 并不依赖 `heldSeconds`
  - pulse 的真实持续时间已经可由 `firstPressUs / lastReleaseUs` 计算
- 后续只有在 commit / lifecycle 明确需要 pulse duration 作为一等字段时，再考虑把它显式 materialize

### F3 overflow 下“归零式 pulse”不可恢复

- 当前不改
- 原因：
  - 这是架构已知限制，不是 reducer 单点能彻底解决的问题
  - 该风险已经通过 `overflowed` 继续向下游传播
- 后续批次应继续在：
  - `B07` 看 processor 有没有 full-state reconciliation
  - `B08` 看 poll commit 是否还有最小容错空间

## 本轮核实后的额外说明

- `SyntheticStateReducer` 仍保持单 snapshot 原子缩减：
  - 每次 `Reduce()` 先 `_latest = {}`
  - 再从同一份 `PadEventSnapshot` 依次派生 buttons / axes / semantic edge facts
- 当前 `mask` 级字段与 `slot` 级字段的双表达属于有意冗余：
  - `pressedMask / releasedMask / transientPressedMask / pulseMask`
  - 与 `buttons[i].pressed / sawPressEdge / sawPulse`
  服务的是不同查询路径，不视为契约冲突

## 留给下一批继续确认

- `B07`：
  - processor 在 `overflowed` 时是否有 full-state reconciliation
  - lifecycle / source-blocking 路是否正确消费 `SyntheticPadFrame`
- `B08`：
  - poll commit 层是否还需要显式 pulse duration
  - `gateAware / contextEpoch` 与 reducer 层时间戳字段之间有没有遗漏的耦合
