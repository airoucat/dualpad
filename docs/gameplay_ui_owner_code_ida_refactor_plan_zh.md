# gameplay / UI owner 代码与 IDA 对照开发计划

这份计划基于三部分信息整理：

- `agents.md` 里最新一轮外部审阅观点
- 当前分支实际代码实现
- IDA 里几条关键游戏侧路径的反编译结果

目标不是继续局部止血，而是把后续开发顺序收成一条低风险主线。

## 已确认事实

### 1. 游戏侧菜单平台判断和 gameplay poll 是两条不同链

IDA 里 `0x140ECD970` 的函数会检查并调用 `_root.SetPlatform`。

这说明：

- 菜单 / Scaleform 平台表现最终只吃一个单一平台结论
- UI owner 适合做成单一 presentation 决策

IDA 里 `0x140C1AB40` 的函数会：

- 先拷贝上一帧 gamepad state
- 再调用 `XInputGetState`
- 再分发按钮和轴变化

这说明：

- gameplay 输入仍然是标准 `XInputGetState -> button/axis dispatch`
- gameplay owner 更适合放在 synthetic gamepad 注入链里做通道仲裁
- 不应该反过来把 gameplay owner 直接塞回 UI 平台判断总线上

### 2. 游戏侧光标本身也有独立状态与更新路径

IDA 里 `0x140ED2F90` 的函数会：

- 鼠标路径下直接 `GetCursorPos`
- 手柄路径下按 gamepad cursor 速度积分位置
- 然后向 UI 发送 mouse state

这说明：

- `CursorOwner` 和 `PresentationOwner` 不是同一个问题
- `cursor handoff / position synchronization` 后面仍应单独做
- 不应该继续把它混进 gameplay owner 讨论里

### 3. 当前代码的主要耦合点是这三处

#### A. `InputModalityTracker::IsUsingGamepad()` 仍然在 gameplay 下二次解释 `LookOwner`

[InputModalityTracker.cpp](../src/input/InputModalityTracker.cpp)

当前 gameplay 分支会：

- 读取 `GameplayOwnershipCoordinator::GetPublishedLookOwner()`
- 再叠加 `_lastGameplayMouseLookAtMs` 的 200ms 窗口
- 然后决定 `IsUsingGamepad()`

这意味着：

- `LookOwner` 还不是单一真相源
- gameplay channel owner 会反向污染 UI / glyph / platform 表现

#### B. `GameplayOwnershipCoordinator` 仍然直接调 backend 做 transient cancel

[GameplayOwnershipCoordinator.cpp](../src/input/injection/GameplayOwnershipCoordinator.cpp)

`UpdateDigitalOwnership()` 当前在 `Gamepad -> KeyboardMouse` 时直接调用：

- `NativeButtonCommitBackend::ForceCancelGateAwareGameplayTransientActions()`

这意味着：

- owner 判定层和 backend 执行层还不是单向依赖
- 之后要继续演进 `DigitalOwner` 时很容易再形成特判环

#### C. provenance-aware recovery 方向正确，但进入 recovery 的触发条件还不统一

[PadEventSnapshotProcessor.cpp](../src/input/injection/PadEventSnapshotProcessor.cpp)

当前：

- 只有发生 sequence gap 才进入 `RecoverMissingPressStateAfterResync()`
- 否则就走普通 `SynthesizeMissingButtonEdges()`

但 dispatcher 已经明确记录了：

- `coalesced`
- `overflowed`
- `crossContextMismatch`

这些退化信息目前没有统一作为 recovery 模式的入口。

## 当前判断

### 哪些方向已经对了

1. UI owner 和 gameplay owner 分层这个大方向是对的。
2. `LookOwner / MoveOwner / CombatOwner / DigitalOwner` 作为 gameplay channel ownership 的拆法是合理的。
3. `Pulse / Toggle / Hold / Repeat` 在下游执行层已经有明确 contract 和 commit mode，不需要推翻重来。
4. provenance-aware recovery 方向是对的，不应该回退到“简单全局禁用 missing press”。

### 哪些地方不该再继续堆补丁

1. 不要再往 `InputModalityTracker::IsUsingGamepad()` 的 gameplay 分支里加新的 owner 特判。
2. 不要再把更多 gameplay facts 往 `InputModalityTracker` 塞。
3. 不要继续在 `GameplayOwnershipCoordinator` 里直接调 backend 做副作用。
4. 不要在 `Sprint` 上继续调 owner 阈值、lease、handoff 小补丁，除非先完成 owner 边界收口。

## 开发计划

### 当前进度

- Phase 1 已完成：`GameplayKbmFactTracker` 已拆出，gameplay KBM facts 不再由 `InputModalityTracker` 直接持有
- Phase 2 已试做并回滚：首次方案只替换了 gameplay 下 `IsUsingGamepad()` 的读法，没有同时收口菜单入口继承与发布时间，导致出现 UI 回归
- Phase 3 已完成：`DigitalOwner` 已改成 processor 统一应用的 `DigitalGatePlan`
- Phase 4 已完成第一阶段：`sequence gap / coalesced / overflowed / crossContextMismatch` 已统一走 provenance-aware degraded recovery
- Phase 5 暂缓：Sprint native mediation 不再继续投入，保持原生优先

### Phase 1：冻结边界，先把“事实采集”和“owner 决策”拆开

目标：

- `InputModalityTracker` 回到 UI owner / platform / cursor 相关职责
- gameplay KBM 活动事实不再作为 tracker 的内部副产品存在

做法：

1. 新增一个轻量 `GameplayKbmFactTracker`
2. 把这些字段和判定从 `InputModalityTracker` 迁出去：
   - mouse-look active
   - keyboard move facts
   - keyboard/mouse combat facts
   - keyboard/mouse digital facts
   - sprint active facts
3. `PadEventSnapshotProcessor` 在每帧规划前读取 `GameplayKbmFactTracker`
4. `GameplayOwnershipCoordinator` 不再直接依赖 `InputModalityTracker`

验收：

- gameplay owner 的日志输出与现状一致
- UI owner 行为不变
- `InputModalityTracker` 不再直接暴露 gameplay KBM facts API

### Phase 2：给 gameplay 表现层单独建真相源，不再借道 `LookOwner`

目标：

- `LookOwner` 只解决 look 通道 ownership
- gameplay 图标 / platform presentation 有独立决策
- gameplay -> menu 切换时，菜单入口继承与 gameplay 展示读取同一份真相源
- `CursorOwner` 明确保持独立，不并入 gameplay presentation

做法：

1. 在 `GameplayOwnershipCoordinator` 新增 `publishedGameplayPresentationOwner`，但它不再是“只在帧末发布给 `IsUsingGamepad()` 的新字段”，而是明确作为 gameplay presentation 的单一真相源。
2. 它的输入不是单个 `LookOwner`，而是：
   - `LookOwner`
   - `MoveOwner`
   - `CombatOwner`
   - `DigitalOwner`
   - coarse `GameplayOwner`
3. 给 presentation owner 一个短 sticky / hysteresis，避免 look/move/combat 在阈值边缘反复翻转时把 gameplay 图标抖成键鼠/手柄来回切。
4. `InputModalityTracker::IsUsingGamepad()` 在 gameplay 下最终只读这个 published presentation owner，不再自己组合 `LookOwner + mouse-look active`。
5. `InputModalityTracker::ReconcileContextState()` 在 `Gameplay -> Menu` 切换时，不再从 `_gameplayOwner` 继承菜单初始平台，而是改为读取同一个 `publishedGameplayPresentationOwner` 作为 menu-entry seed。
6. 为了解决“菜单打开那一拍可能还没等到帧末 publish”的时序问题，增加一个轻量的 gameplay presentation latch：
   - raw keyboard/mouse/gamepad 事件只更新 seed/hint
   - coordinator 在帧规划后把它收敛成正式 `publishedGameplayPresentationOwner`
   - `Gameplay -> Menu` 切换时优先消费最新 latch，而不是消费旧的 `_gameplayOwner`
7. `GamepadControlsCursor()` 和 menu cursor handoff 继续保持独立；IDA `0x140ED2F90` 已确认鼠标光标与手柄光标本来就是另一条链，Phase 2 不把 cursor 同步一起卷进去。
8. 保留旧逻辑做 runtime rollback 开关，先 A/B 验证，只测试 gameplay HUD / 暂停菜单 / Journal / Map 这几页。

验收：

- 不再出现“只因为鼠标看过视角，gameplay 图标长期卡在键鼠”
- 也不再出现“刚切回手柄就一帧抖回键鼠”
- 不再出现“游戏里已经是手柄表现，但一进暂停/日志菜单又以键鼠样式打开”
- 不再出现“菜单初始只有 d-pad 有反应、面键不工作、鼠标 UI 残留，直到再动一次鼠标/手柄才纠正”

### Phase 3：把 `DigitalOwner` 从“直接副作用”改成“gate plan”

目标：

- coordinator 只做 owner 决策
- backend 只做执行

做法：

1. `GameplayOwnershipCoordinator` 输出 `DigitalGatePlan`
   - `suppressNewTransient`
   - `cancelExistingTransient`
2. `PadEventSnapshotProcessor::FinishFramePlanning()` 统一消费这个 gate plan
3. `NativeButtonCommitBackend` 不再直接读取 `GameplayOwnershipCoordinator`
4. 删除 `GameplayOwnershipCoordinator -> NativeButtonCommitBackend::ForceCancel...` 这条直接调用

验收：

- `DigitalOwner` 切换引起的 cancel / suppress 只有一条单向链
- backend 日志里不再出现“自己再推断一遍 owner”的路径

### Phase 4：统一 provenance-aware recovery 的触发入口

目标：

- recovery 是否启动，不再只看 sequence gap
- `coalesced / overflowed / crossContextMismatch` 也进入统一规则

做法：

1. processor 里引入统一的 degraded-frame 判定
2. 只要 snapshot 进入 degraded 模式，就走 `RecoverMissingPressStateAfterResync()` 同类规则
3. `SynthesizeMissingButtonEdges()` 只用于 clean frame
4. `Pulse / Toggle / Hold / Repeat` 仍按当前 contract 规则恢复

验收：

- `Sneak` 和 `Pause` 不回归
- 菜单里 `Accept / Cancel / Up / Down` 仍能正常恢复
- 不再依赖菜单特判来保稳定

### Phase 5：Sprint 暂缓，等边界收完再回头做 native mediation

目标：

- Sprint 不再继续消耗 owner 主线精力
- 先把 owner 架构收干净

做法：

1. 保持当前“尽量不改原生 sprint 语义”的状态
2. 只保留 probe / 文档
3. 等 Phase 1-4 完成后，再决定是否做：
   - `SingleEmitterHold`
   - queue-level native keyboard mediation

验收：

- ownership 主线稳定后，再单独评估 Sprint 是否值得继续投入

## 暂不投入的地方

以下方向现在都不建议继续投入：

1. `IsUsingGamepad()` gameplay 分支的新特判
2. `LookOwner` 直接驱动 gameplay 图标 / 平台
3. `GameplayOwnershipCoordinator` 里继续追加 backend 副作用
4. Sprint handoff 细节补丁
5. 针对某个菜单或某个键的 missing-press 特例修复

## 推荐测试顺序

每做完一个 phase，只测对应面，不做全局乱测：

1. UI owner
   - 主菜单
   - Journal
   - Map
   - 鼠标移动 / 点击 / 手柄重新接管

2. gameplay owner
   - LookOwner：鼠标视角 / 右摇杆视角
   - MoveOwner：键盘移动 / 左摇杆移动
   - CombatOwner：鼠标战斗 / 扳机
   - DigitalOwner：Jump / Activate / Sneak / Pause

3. degraded snapshot recovery
   - `Sneak`
   - `Pause / TweenMenu`
   - 菜单里的 `Accept / Cancel / Up / Down`

## 一句话结论

后续最值得做的不是再修某个 owner 现象，而是：

- 先把 gameplay facts 从 `InputModalityTracker` 拆出去
- 再给 gameplay presentation 建独立真相源
- 再把 `DigitalOwner` 的 backend 副作用改成单向 gate plan
- 最后统一 provenance-aware recovery 入口

这样后面的 UI owner、gameplay owner、glyph presentation、Sprint、cursor handoff 才能分别演进，而不是继续互相拖拽。
