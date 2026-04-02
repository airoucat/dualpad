# Phase 1-4 代码审阅结论

这份文档用于记录一次“按 [gameplay_ui_owner_code_ida_refactor_plan_zh.md](gameplay_ui_owner_code_ida_refactor_plan_zh.md) 回看当前实现”的代码审阅结论。

目标不是重写计划，而是回答三个问题：

1. Phase 1-4 目前哪些部分已经按计划落地。
2. 哪些部分只是“功能上能跑”，但与原计划存在结构偏差。
3. 后续应该先处理哪些问题。

## 总结

- Phase 1 方向基本正确，`GameplayKbmFactTracker` 已经把 gameplay KBM facts 从 `InputModalityTracker` 拆出。
- Phase 3 方向也基本正确，`DigitalOwner` 已经不再由 coordinator 直接修改 backend 状态，而是先产出 `DigitalGatePlan`，再由 processor/commit backend 消费。
- Phase 4 现在已经完成了“统一 degraded 入口 + clean recovery baseline”这一轮正式收口。
- **Phase 2 现在已经完成了“coordinator 发布 gameplay presentation state，tracker 只做 engine/menu 适配”这一轮正式收口。**

一句话说：

- Phase 2 不是“只有工程补丁”。
- 它现在真正生效的，是 `GameplayOwnershipCoordinator` 发布的 gameplay presentation state。
- `InputModalityTracker` 仍保留 `engineGameplayPresentationLatch + gameplayMenuEntrySeed`，但它们现在是 engine-facing adapter，不再自己生成 gameplay presentation 结论。

## Phase 1

### 已落地

- `GameplayKbmFactTracker` 已独立存在：
  - `src/input/GameplayKbmFactTracker.h`
  - `src/input/GameplayKbmFactTracker.cpp`
- `InputModalityTracker` 不再直接持有 gameplay KBM facts 的整套状态机。
- `GameplayOwnershipCoordinator` 已通过 `GameplayKbmFactTracker::GetFacts()` 读取 gameplay KBM facts。

### 风险

- 当前 `GameplayKbmFactTracker` 在按钮事件上仍频繁查询 `ControlMap::GetMappedKey(...)`。
- 这更像性能/缓存优化点，不是结构错误。

### 结论

- Phase 1 可视为基本完成。

## Phase 2

### 原计划目标

原计划希望做到：

1. `GameplayOwnershipCoordinator` 产出一个单一的 `publishedGameplayPresentationOwner`。
2. gameplay 下的 `IsUsingGamepad()` 直接消费它。
3. `Gameplay -> Menu` 初始继承也消费同一个 source。
4. `CursorOwner` 继续独立，不混入 gameplay presentation。

### 当前实际实现

当前运行时真正生效的是：

- `GameplayOwnershipCoordinator::GameplayPresentationState`
  - `engineOwner`
  - `menuEntryOwner`
- `InputModalityTracker`
  - `_engineGameplayPresentationLatch`
  - `_gameplayMenuEntrySeed`
- `ContextEventSink -> OnAuthoritativeMenuOpen(...)` 的 menu-open 提前同步

关键代码：

- `src/input/InputModalityTracker.cpp`
- `src/input/ContextEventSink.cpp`

而 `publishedGameplayPresentationOwner` 本身：

- 继续保留并发布
- 作为 `GameplayPresentationState.engineOwner` 的公开观察值
- 不再是“发布了但没有运行时消费者”的悬空值

关键代码：

- `src/input/injection/GameplayOwnershipCoordinator.h`
- `src/input/injection/GameplayOwnershipCoordinator.cpp`

### 当前结论

**Phase 2 现在已经接近原计划落地，但形式上不是“单个 owner 值直连引擎”，而是“coordinator state -> tracker adapter -> engine/menu”。**

它现在实际解决的是：

1. gameplay -> menu 初始继承时序
2. `IsUsingGamepad()` 在 gameplay/menu 过渡窗口里的错误消费
3. engine-facing 平台位的保守锁存

它现在没有做到的是：

- 把所有 runtime 语义都压成单个 `publishedGameplayPresentationOwner`

而是改成了更明确的一层：

- coordinator 发布 `GameplayPresentationState`
- tracker 只负责把它适配成
  - `engineGameplayPresentationLatch`
  - `gameplayMenuEntrySeed`

### 主要偏差

#### 1. 已修复：runtime authoritative source 已收回 coordinator 发布层

现状：

- `GameplayOwnershipCoordinator` 现在会发布 `GameplayPresentationState`
  - `engineOwner`
  - `menuEntryOwner`
- `InputModalityTracker::ResolveIsUsingGamepad()` 仍读 `_engineGameplayPresentationLatch`
- menu-entry 继承仍读 `_gameplayMenuEntrySeed`
- 但这两个值现在由 tracker 从 coordinator state 同步而来，而不是 tracker 自己直接依据 gameplay 事件得出

也就是说：

- 原来“tracker 自己做 engine-facing truth”的高问题已经收口
- 当前更准确的口径是：
  - coordinator 负责 gameplay presentation 事实
  - tracker 负责 engine/menu 时序适配

#### 2. 当前保留的有意分层：`MoveOwner` 只进入 menu-entry，不直接进入 engine-facing gameplay latch

`publishedGameplayPresentationOwner` 会综合：

- `LookOwner`
- `MoveOwner`
- `CombatOwner`
- `DigitalOwner`
- coarse `GameplayOwner`

但当前 `GameplayPresentationState` 是分成两路的：

- `engineOwner`
- `menuEntryOwner`

其中：

- `MoveOwner` 会参与 `menuEntryOwner`
- 不会直接参与 `engineOwner`

这意味着：

- 这是当前保留的工程约束，而不是遗漏
- 原因是如果让左摇杆直接进入 engine-facing gameplay latch，会重新引入 `左摇杆 + 鼠标` 的 gameplay camera jitter

#### 3. 剩余限制：Phase 2 的 authoritative source 现在是 coordinator state，而不是单个 publisher 值

这不再是 bug，而是当前更清晰的设计现实：

- `publishedGameplayPresentationOwner` 对应 engine-facing gameplay presentation
- `publishedGameplayMenuEntryOwner` 对应 `Gameplay -> Menu` 初始继承
- 二者一起构成 `GameplayPresentationState`

也就是说，当前 authoritative source 是一个结构体状态，而不是单个枚举值。

### 当前建议

Phase 2 当前不建议回退，因为这轮已经完成了关键收口：

- `GameplayOwnershipCoordinator` 负责发布 gameplay presentation state
- `InputModalityTracker` 退回 engine/menu 适配层
- `engineGameplayPresentationLatch` / `gameplayMenuEntrySeed` 不再自行生成 gameplay presentation 事实
- 左摇杆仍然只进入 `menuEntryOwner`，不进入 `engineOwner`

这意味着当前方案的评价应是：

- 可靠性可以
- 工程上可接受
- 架构上已经明显优于此前“tracker 自己生成事实”的状态

当前更适合继续观察和补文档，而不是再回到“单个 publisher 直连引擎”的旧表述。

### Phase 2 待办

1. 把文档与代码口径统一成：
   - coordinator authoritative state：`GameplayPresentationState`
   - tracker engine-facing adapter：`engineGameplayPresentationLatch + gameplayMenuEntrySeed`
2. 继续观察这条保留约束是否足够稳定：
   - 左摇杆只进 `menuEntryOwner`
   - 不进 `engineOwner`
3. 如果未来再做 Phase 2.5，重点不是“回到旧直连”，而是评估是否要让 `GameplayPresentationState` 继续细化出更多 engine/menu 专用字段

### Phase 2 补充：menu 内平台回切修复

- 现象
  - 用键盘或鼠标把菜单切到 `KeyboardMouse` 后，菜单内后续手柄输入有时只能“内部抢回 owner”，但 UI/presentation 观感上不能立即切回 `Gamepad`，需要关菜单再开才恢复。

- 根因
  - menu 内 keyboard takeover 之前对 keyboard button 过宽，`held/up` 也会参与平台抢占。
  - menu 内从 `CursorOwner::KeyboardMouse -> Gamepad` 的真实切换，未必总能触发足够强的 UI 刷新；部分菜单会继续缓存鼠标态 affordance。

- 当前修法
  - `InputModalityTracker::HandleKeyboardEvent(...)`
    - menu 内 keyboard takeover 现在只认真正的 `down/char`
    - `held/up` 不再触发 `PromoteToKeyboardMouse(...)`
  - `InputModalityTracker::SetCursorOwner(...)`
    - 当 menu 内 cursor owner 发生真实翻转时，额外触发一次 `RefreshMenus()`
    - 用于把已经回到 `Gamepad` 的内部 owner 及时反映到菜单 UI

- 当前评价
  - `keyboard takeover` 只认 `down/char` 这一条，属于架构上正确且应长期保留的修正。
  - `menu 内 cursor-owner flip 时补一次 RefreshMenus()` 这一条，更像窄范围的工程兼容修复，但当前日志与实测都表明它是稳定、可接受的。

- 日志验证结论
  - 最新 run 中，已经能稳定看到：
    - `Presentation Gamepad -> KeyboardMouse via mouse-button`
    - 后续又有
    - `Presentation KeyboardMouse -> Gamepad via gamepad`
  - 说明“键鼠进菜单后切不回手柄”的问题在当前实现下已恢复正常。

## Phase 3

### 已落地

- `GameplayOwnershipCoordinator::UpdateDigitalOwnership(...)` 现在产出 `DigitalGatePlan`
- `PadEventSnapshotProcessor::FinishFramePlanning(...)` 统一消费：
  - `suppressNewTransientActions`
  - `cancelExistingTransientActions`
- `NativeButtonCommitBackend` 不再主动查询 owner 判定来做 suppress/cancel

关键代码：

- `src/input/injection/GameplayOwnershipCoordinator.cpp`
- `src/input/injection/PadEventSnapshotProcessor.cpp`
- `src/input/backend/NativeButtonCommitBackend.cpp`

### 当前结论

- Phase 3 主方向已经达成。

### 剩余风险

- `DigitalOwner` 目前仍只认 `InputContext::Gameplay`
- 这看起来是当前有意保守限制，不建议在本轮 review 里顺手扩大到 gameplay-domain 子上下文

## Phase 4

### 已落地

当前 `degradedSnapshot` 已统一包含：

- sequence gap / resync
- `snapshot.coalesced`
- `snapshot.overflowed`
- `snapshot.events.overflowed`
- `snapshot.crossContextMismatch`

clean frame 才走：

- `SynthesizeMissingButtonEdges(...)`

degraded frame 统一走：

- `RecoverMissingPressStateAfterResync(...)`

关键代码：

- `src/input/injection/PadEventSnapshotProcessor.cpp`

### 当前实现状态

`PadEventSnapshotProcessor` 现在已经不再维护那组名不副实的：

- `_lastStableContext`
- `_lastStableContextEpoch`
- `_lastStableDownMask`

而是改成了显式的 `RecoveryBaseline`：

- `valid`
- `context`
- `contextEpoch`
- `downMask`
- `sequence`

当前规则是：

- `lastProcessedSequence` 继续表示“最后成功处理到哪一帧”
- `RecoveryBaseline` 只表示“最后一个 clean frame 的恢复参考基线”
- degraded frame 不再覆盖 recovery baseline
- recovery 日志会显式打印 `baselineSeq`

这意味着 Phase 4 现在的恢复模型已经变成：

- clean frame：
  - 走 `SynthesizeMissingButtonEdges(...)`
  - 帧尾提交 clean recovery baseline
- degraded frame：
  - 统一走 `RecoverMissingPressStateAfterResync(...)`
  - 只消费已有 clean baseline，不会污染它

### 剩余关注点

当前剩下的重点已经不是“stable 语义错误”，而是：

- 继续观察 clean/degraded baseline 在真实 overflow/coalesce 场景下的日志序列
- 确认 `SameContextGap / CrossContextBoundary / HardResync` 三种 recovery kind 的覆盖面是否符合预期
- 后续如需继续增强，可再把 recovery probe 提炼成更明确的统计指标

## 推荐处理顺序

1. 先处理 Phase 2 的结构偏差
   - 明确 authoritative source 到底是谁
2. 再做一轮 Phase 4 的实机日志验证
   - 确认 clean baseline 模型在真实 degraded 场景下表现稳定
3. 之后再评估是否要继续扩大 Phase 3 的 gameplay-domain 覆盖范围

## 一句话结论

当前最值得优先处理的，不是推翻 Phase 1-4，而是：

- **承认并收口 Phase 2 的真实实现模型**
- **把 Phase 4 的恢复基线正式升级成 clean/degraded 双层语义**

这样后续继续演进时，代码和文档才不会再次脱节。
