# Phase 1-4 当前代码的优雅性补充审阅

这份文档只记录一个问题：**Phase 1-4 现在虽然已经基本稳定可用，但代码里还剩哪些“不丑到必须立刻修、但明显不够优雅”的点。**

这里不重复记录已经确认的功能 bug，也不推翻当前已验证通过的实现，只补充后续适合继续收口的结构债。

## 总结

- 当前 Phase 1-4 的主线已经能工作，重点问题不再是“哪里会炸”，而是“哪里还不够干净、后面容易继续长特判”。
- 当前最值得优先继续收口的，不是 Phase 4，而是：
  1. Phase 3 里 `GameplayOwnershipCoordinator -> InputModalityTracker` 的反向依赖
  2. Phase 2 里 `InputModalityTracker` 仍同时承担 UI 仲裁和 gameplay presentation 适配
- 其余项更像低风险 cleanup。

## Findings

### 1. Phase 3：`GameplayOwnershipCoordinator` 仍然反向依赖 `InputModalityTracker`

相关代码：
- [GameplayOwnershipCoordinator.cpp](../src/input/injection/GameplayOwnershipCoordinator.cpp) `UpdateDigitalOwnership(...)`
- [GameplayOwnershipCoordinator.cpp](../src/input/injection/GameplayOwnershipCoordinator.cpp) 第 120-129 行附近

现状：
- `UpdateDigitalOwnership(...)` 里仍然会读取 `InputModalityTracker::GetSingleton().IsGameplayUsingGamepad()`
- 也就是 coordinator 在做 digital owner 判定时，仍然需要回头询问 tracker 的 coarse gameplay owner

问题：
- 这让 Phase 3 现在还是“基本单向”，不是“彻底单向”
- coordinator 仍然不是纯粹的 `facts + framePlan -> ownership/gate plan`
- 后面只要 tracker 的 gameplay owner 语义再变一次，digital owner 这边也会被连带影响

更优雅的方向：
- 让 `PadEventSnapshotProcessor` 在同一帧里把 coarse gameplay fallback 一起作为参数传给 coordinator
- coordinator 不再直接依赖 tracker 单例

结论：
- 这是当前最值得继续收口的剩余结构债之一

### 2. Phase 2：`InputModalityTracker` 仍同时承担 UI 仲裁和 gameplay presentation 适配

相关代码：
- [InputModalityTracker.cpp](../src/input/InputModalityTracker.cpp) `HandleGameplayOnlyEvent(...)`
- [InputModalityTracker.cpp](../src/input/InputModalityTracker.cpp) 第 761-917 行附近
- [InputModalityTracker.cpp](../src/input/InputModalityTracker.cpp) `SyncGameplayPresentationFromCoordinator(...)`
- [InputModalityTracker.cpp](../src/input/InputModalityTracker.cpp) 第 351-372 行附近

现状：
- tracker 现在已经不再自己生成 gameplay presentation 事实，这点是对的
- 但 gameplay 事件到 coordinator hint、再到 `engineGameplayPresentationLatch / gameplayMenuEntrySeed` 的适配，仍然全部写在 tracker 里
- 而且 `RecordGameplayPresentationHint(...) + SyncGameplayPresentationFromCoordinator(...)` 这组调用在多个输入分支里重复出现

问题：
- 代码语义已经正确，但职责边界仍然偏厚
- 以后只要再加一种 gameplay 输入语义，仍然要改 tracker 的多个分支
- UI owner 状态机和 gameplay presentation adapter 还没有真正拆成两个概念

更优雅的方向：
- 至少先抽一个局部 helper，比如 `CommitGameplayPresentationHint(...)`
- 更进一步，可以把 gameplay 事件 -> coordinator state -> tracker latches 的这一小层正式提成 `GameplayPresentationAdapter`

结论：
- 这是可维护性问题，不是当前功能 bug
- 但它是 Phase 2 继续长分支逻辑的主要入口

### 3. Phase 2：菜单平台切换时仍可能发生重复 `RefreshMenus()`

相关代码：
- [InputModalityTracker.cpp](../src/input/InputModalityTracker.cpp) `PromoteToGamepad(...)`
- [InputModalityTracker.cpp](../src/input/InputModalityTracker.cpp) `PromoteToKeyboardMouse(...)`
- [InputModalityTracker.cpp](../src/input/InputModalityTracker.cpp) `SetPresentationOwner(...)`
- [InputModalityTracker.cpp](../src/input/InputModalityTracker.cpp) `SetCursorOwner(...)`
- [InputModalityTracker.cpp](../src/input/InputModalityTracker.cpp) 第 997-1074 行附近

现状：
- menu 内一次真实 handoff 里，`PromoteToGamepad/KeyboardMouse(...)` 会先改 cursor，再改 presentation
- `SetCursorOwner(...)` 在 menu context 下会 `RefreshMenus()`
- `SetPresentationOwner(...)` 也会 `RefreshMenus()`

问题：
- 当前行为是稳定的，但一个逻辑 handoff 实际上可能触发两次菜单刷新
- 它更像“可靠但偏粗”的工程实现
- 后面如果菜单刷新动作再变重，这里会是潜在噪声点

更优雅的方向：
- 把刷新请求做成同帧 coalesce
- 或者把 cursor/presentation 的 owner flip 先收集，再只触发一次统一 refresh

结论：
- 当前可接受，不建议为了“好看”立刻重构
- 但值得记录成后续 cleanup 点

### 4. Phase 1：`GameplayKbmFactTracker` 仍然在热路径里重复做 controlmap 查询和语义解码

相关代码：
- [GameplayKbmFactTracker.cpp](../src/input/GameplayKbmFactTracker.cpp) 第 44-51 行附近
- [GameplayKbmFactTracker.cpp](../src/input/GameplayKbmFactTracker.cpp) 第 96-196 行附近
- [GameplayKbmFactTracker.cpp](../src/input/GameplayKbmFactTracker.cpp) 第 240-316 行附近

现状：
- `ObserveButtonEvent(...)` 会在一次按钮事件里多次走 `ControlMap::GetMappedKey(...)`
- 同一套 `forward/back/strafe/attack/jump/activate/sprint` 语义映射，也在多个 helper 里重复解码

问题：
- 这不是 correctness 问题
- 但它让 Phase 1 的 fact tracker 看起来比实际职责更重，也让热路径代码显得很散

更优雅的方向：
- 缓存 gameplay context 下的关键映射
- 或把 semantic bit 的解码预先组织成表驱动结构

结论：
- 这是很典型的“功能没问题，但不够利落”的 cleanup 项

### 5. Phase 4：`PadEventSnapshotProcessor::Process(...)` 仍然偏大，恢复/规划/补偿逻辑交织

相关代码：
- [PadEventSnapshotProcessor.cpp](../src/input/injection/PadEventSnapshotProcessor.cpp) 第 585-700 行附近
- [PadEventSnapshotProcessor.cpp](../src/input/injection/PadEventSnapshotProcessor.cpp) 第 364-505 行附近

现状：
- Phase 4 的 clean baseline 语义现在已经对了
- 但 `Process(...)` 仍同时承担：
  - seq gap / degraded 判定
  - missing press recovery
  - 普通 planning
  - degraded source-block compensation
  - menu probe 日志
  - clean baseline commit

问题：
- 逻辑现在是正确的，但继续加恢复策略会让这个函数越来越难改
- 以后只要再碰 degraded path，就很容易把 planning 和 recovery 缠回一起

更优雅的方向：
- 把 degraded path 提成更明确的子流程，例如：
  - `PrepareDegradedSnapshot(...)`
  - `RecoverDegradedSnapshot(...)`
  - `FinalizeRecoveryBaseline(...)`

结论：
- 这是 Phase 4 的主要“可读性/可演进性”债，不是当前行为错误

## 推荐顺序

1. 先处理 Phase 3 的反向依赖
2. 再处理 Phase 2 的 gameplay presentation adapter 抽取
3. 之后再考虑菜单刷新 coalesce
4. Phase 1 的 mapping cache / 表驱动化放在 cleanup 阶段
5. Phase 4 的 `Process(...)` 拆分可以作为后续代码整理项，不必抢现在的主线

## 一句话结论

当前 Phase 1-4 最大的问题已经不再是“哪里不稳定”，而是：
- Phase 3 还差最后一点单向依赖收口
- Phase 2/4 还各自留着一层“功能正确但实现偏厚”的适配/编排代码

这些都值得继续做，但都不属于必须马上回滚或重构的大故障。
