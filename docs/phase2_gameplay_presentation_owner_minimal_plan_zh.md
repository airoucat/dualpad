# Phase 2：Gameplay Presentation Owner 最小实施方案

这份文档只服务一个目标：

- 在不再次打坏菜单初始状态的前提下，
- 重新落地 `gameplay presentation` 的单一真相源，
- 并让 `Gameplay -> Menu` 入口继承与 gameplay HUD/platform 读取同一份结论。

它不是新总方案，而是对 [gameplay_ui_owner_code_ida_refactor_plan_zh.md](gameplay_ui_owner_code_ida_refactor_plan_zh.md) 里 Phase 2 的更小、更可执行拆分。

## 当前进度

- Step 1 已落地：
  - `GameplayOwnershipCoordinator` 已新增 `publishedGameplayPresentationOwner`
  - 当前只做发布与日志
  - 还没有接入 `IsUsingGamepad()`
  - 也还没有接入 `Gameplay -> Menu` 入口继承
- Step 2 已落地：
  - `InputModalityTracker` 已新增 `gameplayMenuEntrySeed`
  - raw gameplay keyboard/mouse/gamepad 事件会更新这个 seed
  - 当前同样只做采集与日志
- Step 3 已落地：
  - `Gameplay -> Menu` 时，菜单初始平台继承已改为读取 `gameplayMenuEntrySeed`
- Step 4 已落地：
  - gameplay 下的 `IsUsingGamepad()` 已改为读取 `publishedGameplayPresentationOwner`
  - 旧的 `publishedLookOwner + mouse-look active` 二次拼接已移除

## 背景

当前代码仍有两处结构性风险：

1. [InputModalityTracker.cpp](../src/input/InputModalityTracker.cpp) 的 gameplay `IsUsingGamepad()` 仍然自己组合：
   - `GameplayOwnershipCoordinator::GetPublishedLookOwner()`
   - `GameplayKbmFactTracker::IsMouseLookActive()`
   - `_gameplayOwner`
2. [InputModalityTracker.cpp](../src/input/InputModalityTracker.cpp) 的 `ReconcileContextState()` 在 `Gameplay -> Menu` 时，菜单初始继承仍然直接读粗粒度 `_gameplayOwner`

这说明：

- gameplay presentation 还没有单一真相源
- menu startup 和 gameplay HUD/platform 也没有读同一份 truth source

## 上次失败的根因

上次试做 Phase 2 后出现：

- 进暂停/日志菜单时 UI 一开始卡在键鼠
- 只有 d-pad 有反应，面键不稳定
- 鼠标 UI 残留
- 需要手柄/键鼠再交替动一下才恢复

根因不是概念错，而是两件事一起发生：

### 1. 真相源分裂

当时只把 gameplay 下的 `IsUsingGamepad()` 改去读新的 presentation owner，
但 `Gameplay -> Menu` 时菜单入口继承仍然走旧 `_gameplayOwner`。

结果变成：

- gameplay HUD/platform 读一份
- menu startup 读另一份

所以同一时刻会出现“游戏像手柄，菜单像键鼠”的分裂。

### 2. 发布时间太晚

新的 gameplay presentation 结论是在 gameplay 帧处理末尾才 publish。
但菜单平台初始化更早。

结合代码和 IDA 的判断：

- [ContextEventSink.cpp](../src/input/ContextEventSink.cpp) 在菜单开关时只先更新 `ContextManager`
- [InputModalityTracker.cpp](../src/input/InputModalityTracker.cpp) 真正应用上下文切换，要等后续 `ReconcileContextState()`
- IDA 里菜单平台链最终走 `_root.SetPlatform`

所以实际顺序更像是：

1. 菜单开始初始化
2. 引擎先问平台结论
3. tracker 还没完成新的 menu reconcile
4. 它仍按 gameplay 路径回答
5. 菜单首帧就以错误平台打开

一句话：

**上次 Phase 2 失败，不是 owner 判错，而是新的 truth source 在菜单初始化之后才生效。**

## 和 cursor 的边界

这轮 Phase 2 明确不处理 cursor handoff。

原因已经在文档和 IDA 里确认过：

- 菜单平台最终是 `_root.SetPlatform`
- cursor 则是另一条独立路径
- 鼠标/手柄切换时的“光标闪一下再跳回旧位置附近”属于 cursor position synchronization

所以这一轮只做：

- gameplay presentation truth source
- menu-entry seed / latch

不做：

- `CursorOwner` 语义调整
- `MenuCursor` / `NotifyMouseState(...)` handoff

## 目标不变量

Phase 2 成功以后，必须同时满足下面 4 条：

1. gameplay 下的 HUD / glyph / platform 读取统一 truth source
2. `Gameplay -> Menu` 时，菜单首帧继承同一份 truth source
3. `IsUsingGamepad()` 不再在 gameplay 下自行拼 `LookOwner + mouse-look`
4. cursor 路径不被一起卷入

## 最小落地策略

### Step 1：只新增 truth source，不改现有消费方

先在 [GameplayOwnershipCoordinator.h](../src/input/injection/GameplayOwnershipCoordinator.h) /
[GameplayOwnershipCoordinator.cpp](../src/input/injection/GameplayOwnershipCoordinator.cpp) 新增：

- `publishedGameplayPresentationOwner`

但第一步只做：

- 产生和 publish 这个值
- 打日志

不做：

- 不改 `IsUsingGamepad()`
- 不改 `ReconcileContextState()`
- 不改菜单入口继承

这样先验证：

- 新 owner 是否稳定
- 它的切换序列是否真的比旧逻辑更合理

### Step 2：引入 menu-entry seed / latch

新增一个轻量级 `gameplay presentation seed`：

- 来源是 raw gameplay keyboard/mouse/gamepad activity
- 目标不是直接成为 owner
- 而是在菜单打开那一拍，提供一个“最新可消费的初值”

要求：

- seed 必须早于菜单初始化可读
- 不能等帧末 publish 后才出现

也就是说，Phase 2 真正需要两层：

- `seed/latch`：解决菜单初始化时序
- `publishedGameplayPresentationOwner`：解决 gameplay 持续表现

### Step 3：只改 `Gameplay -> Menu` 入口继承

第三步优先改 [InputModalityTracker.cpp](../src/input/InputModalityTracker.cpp) 的 `ReconcileContextState()`：

- `Gameplay -> Menu` 时
- 不再用 `_gameplayOwner`
- 而是先读最新 `gameplay presentation seed/latch`

这一步仍然不改 gameplay 下 `IsUsingGamepad()`。

原因：

- 上次炸菜单就是入口继承先出问题
- 所以应该先修 menu startup
- 先确保“菜单首帧正确”

### Step 4：最后才切 gameplay `IsUsingGamepad()`

等 `Gameplay -> Menu` 首帧已经稳定后，再把 gameplay 下的 `IsUsingGamepad()` 改成：

- 只读 `publishedGameplayPresentationOwner`

并删除旧逻辑里的：

- `publishedLookOwner + IsMouseLookActive()`

这一步必须最后做，因为它会影响：

- gameplay HUD/platform
- glyph/presentation
- 引擎内部 gameplay platform 判断

## 推荐 owner 组成

`publishedGameplayPresentationOwner` 不应该只看 `LookOwner`。

建议组合输入仍然是：

- `LookOwner`
- `MoveOwner`
- `CombatOwner`
- `DigitalOwner`
- coarse `GameplayOwner`

并加一个很短的 sticky/hysteresis，
只服务 gameplay presentation，
不服务通道 owner 本身。

它的职责是：

- 决定 gameplay 图标和平台表现
- 不参与 analog suppress
- 不参与 cursor

## 推荐实现顺序

按风险从低到高：

1. 发布 `publishedGameplayPresentationOwner`，只打日志
2. 增加 `gameplay presentation seed/latch`
3. 只改 `Gameplay -> Menu` 入口继承
4. 回归主菜单 / 暂停 / Journal / Map
5. 最后改 gameplay 下 `IsUsingGamepad()`

## 回滚策略

Phase 2 相关改动必须保留单独回滚点。

最小回滚面建议是：

- [GameplayOwnershipCoordinator.h](../src/input/injection/GameplayOwnershipCoordinator.h)
- [GameplayOwnershipCoordinator.cpp](../src/input/injection/GameplayOwnershipCoordinator.cpp)
- [InputModalityTracker.h](../src/input/InputModalityTracker.h)
- [InputModalityTracker.cpp](../src/input/InputModalityTracker.cpp)

并且分成两个回滚档：

### 回滚档 A

- 保留 `publishedGameplayPresentationOwner`
- 回滚 `IsUsingGamepad()` 消费
- 回滚 menu-entry seed

适用于：

- owner 本身稳定
- 但 menu startup 不稳

### 回滚档 B

- 完整撤掉 `publishedGameplayPresentationOwner`

适用于：

- 连 owner 本身的发布都不稳定

## 测试矩阵

### A. gameplay HUD / glyph

1. 鼠标看视角后，再用手柄继续操作
2. 只用左摇杆移动
3. 只用扳机 / 面键
4. 看 gameplay 图标是否仍能回手柄

### B. menu startup

1. gameplay -> Pause
2. gameplay -> Journal
3. gameplay -> Map

观察：

- 菜单首帧是否就是正确平台
- 是否还会出现：
  - 只有 d-pad 有反应
  - 面键不工作
  - 鼠标 UI 残留

### C. regression

1. 主菜单
2. 纯菜单内键鼠/手柄切换
3. `Sneak / Pause`

确保：

- 不把之前已经稳定的路径带坏

## 这轮不做的事

1. 不处理 cursor handoff
2. 不扩大 `DigitalOwner` 到 gameplay-domain 子上下文
3. 不继续碰 Sprint native mediation
4. 不在 `IsUsingGamepad()` gameplay 分支里追加新的特判

## 一句话结论

Phase 2 如果要稳定落地，关键不是“再造一个新 owner”，而是：

**先让 gameplay presentation 有单一真相源，再让 menu startup 在正确时机消费它。**
