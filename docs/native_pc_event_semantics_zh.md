# PC 原生事件语义追踪

更新时间：2026-03-14

## 目的

这份文档用于记录 Skyrim SE 1.5.97 原生 PC / gamepad user event 的注册表、handler 家族和当前项目动作面的语义归类。

本轮目标不是把所有动作一次性都迁到 `ButtonEvent` 主线，而是：

- 先把原生名字和原生语义追清楚
- 再只迁移那些已经有充分证据、而且能在当前 `Poll current-state ownership` 主线上安全表达的动作

## 总入口

原生 user event 注册表初始化函数：

- `sub_140C17FB0`

它会把全局 user event 表 `qword_142F25250` 填满，至少包括这些名字：

- Gameplay / core:
  - `Forward`
  - `Back`
  - `Strafe Left`
  - `Strafe Right`
  - `Move`
  - `Look`
  - `Activate`
  - `Left Attack/Block`
  - `Right Attack/Block`
  - `Dual Attack`
  - `ForceRelease`
  - `Pause`
  - `Ready Weapon`
  - `Toggle POV`
  - `Jump`
  - `Journal`
  - `Sprint`
  - `Sneak`
  - `Shout`
  - `Grab`
  - `Run`
  - `Toggle Always Run`
  - `Auto-Move`
  - `Quicksave`
  - `Quickload`
  - `Inventory`
  - `Stats`
  - `Map`
  - `Screenshot`
  - `Multi-Screenshot`
  - `Console`
  - `CameraPath`
  - `Tween Menu`
  - `Take All`
- Menu / UI:
  - `Accept`
  - `Cancel`
  - `Up`
  - `Down`
  - `Left`
  - `Right`
  - `PageUp`
  - `PageDown`
  - `Pick`
  - `PickNext`
  - `PickPrevious`
  - `Cursor`
  - `Zoom In`
  - `Zoom Out`
  - `Left Stick`
  - `PrevPage`
  - `NextPage`
  - `PrevSubPage`
  - `NextSubPage`
  - `LeftEquip`
  - `RightEquip`
  - `ToggleFavorite`
  - `Favorites`
  - `Hotkey1` 到 `Hotkey8`
  - `Quick Inventory`
  - `Quick Magic`
  - `Quick Stats`
  - `Quick Map`
  - `ToggleCursor`
  - `Wait`
  - `Click`
  - `MapLookMode`
  - `Equip`
  - `DropItem`
  - `Rotate`
  - `NextFocus`
  - `PreviousFocus`
  - `SetActiveQuest`
  - `PlacePlayerMarker`
  - `XButton`
  - `YButton`
  - `ChargeItem`
  - `PlayerPosition`
  - `LocalMap`
  - `LocalMapMoveMode`
  - `Item Zoom`
- Start / stop / state tokens:
  - `SprintStart`
  - `SprintStop`
  - `sneakStart`
  - `sneakStop`
  - `blockStart`
  - `blockStop`
  - `blockBash`
  - `attackStart`
  - `attackPowerStart`
  - `reverseDirection`
  - `Unequip`

## Gameplay handler 家族

Gameplay root 构造函数：

- `sub_1407073B0`

该函数明确注册了这些 gameplay handler：

- `MovementHandler`
- `LookHandler`
- `SprintHandler`
- `ReadyWeaponHandler`
- `AutoMoveHandler`
- `ToggleRunHandler`
- `ActivateHandler`
- `JumpHandler`
- `ShoutHandler`
- `AttackBlockHandler`
- `RunHandler`
- `SneakHandler`
- `TogglePOVHandler`

### 已实锤的 gameplay 语义

#### `Jump`

- 处理函数：`JumpHandler::slot4 = 0x1407090A0`
- 证据：
  - 只在 `value40 != 0 && value44 == 0` 这种按下边沿上工作
  - 没有对应 release 分支
- 语义结论：
  - `Pulse`

#### `Activate`

- 处理函数：`ActivateHandler::slot4 = 0x140708BF0`
- 证据：
  - press-edge 触发
  - 对内部状态字节有 start / clear 行为
  - 更像一次性离散触发，而不是持续 held owner
- 语义结论：
  - `Pulse`
  - 在插件侧可以 materialize 成 `MinDownWindowPulse`

#### `Sprint`

- 处理函数：`SprintHandler::slot4 = 0x140709580`
- 相关辅助：
  - `0x140704940`
  - `0x14070A5A0`
- 注册表中还存在：
  - `SprintStart`
  - `SprintStop`
- 语义结论：
  - `Hold / Start-Stop`
  - 明确是 stateful 家族，不是普通 pulse

#### `Sneak`

- 处理函数：`SneakHandler::slot4 = 0x1407094C0`
- 证据：
  - press-edge 触发
  - 没看到对称的 held 持续处理
  - 注册表同时存在 `sneakStart / sneakStop`
- 语义结论：
  - `Toggle-like discrete`
  - 在插件侧更适合 `ToggleDebounced`

#### `Shout`

- 处理函数：`ShoutHandler::slot4 = 0x1407093A0`
- 证据：
  - 按下时保存 event identity
  - 松开时明确清状态并调用收口逻辑
  - 明显有 state byte 持有
- 语义结论：
  - `Hold / Stateful`

#### `Attack / Block`

- 处理函数：`AttackBlockHandler::slot4 = 0x140708E30`
- 证据：
  - 同时处理 `Left Attack/Block`、`Right Attack/Block`、`Dual Attack`
  - 既有 press 分支，也有 release / clear 分支
  - 明显是 stateful 家族
- 进一步 IDA 结论：
  - `BSWin32GamepadDevice::Poll = 0x140C1AB40`
  - 扳机不会直接以“裸 byte”进入 handler，而是先走 `0x140C16FB0` 做阈值/死区归一化
  - 之后通过 `0x140C19220` 生成 trigger 事件：
    - 左扳机 = event id `9`
    - 右扳机 = event id `10`
  - 该事件会把 `current` 与 `previous` 两个浮点值一起写进队列对象，底层入队函数是 `0x140C16900`
  - `AttackBlockHandler` 消费的 descriptor 注册在 `sub_140C17FB0`：
    - `qword_142F25250 + 64 = Left Attack/Block`
    - `qword_142F25250 + 72 = Right Attack/Block`
    - `qword_142F25250 + 80 = Dual Attack`
    - `qword_142F25250 + 88 = ForceRelease`
- 语义结论：
  - `Hold / Stateful`
  - 更准确地说是 `trigger-owned stateful family`
  - 不适合被当成简单数字 pulse
  - 插件侧更适合 materialize 虚拟 `LT/RT` current-state，让 Skyrim 自己推导战斗语义，而不是在插件里重写 `AttackBlockHandler`

#### `Ready Weapon`

- 处理函数：`ReadyWeaponHandler::slot4 = 0x140709310`
- 证据：
  - press-edge 触发
  - 没看到持续 held owner 语义
- 语义结论：
  - `Pulse`

#### `Toggle POV`

- 处理函数：`TogglePOVHandler::slot4 = 0x140709600`
- 证据：
  - press-edge 设置内部 latch
  - release 时清 latch
  - 整体表现是离散切换，而不是持续 hold
- 语义结论：
  - `Pulse / Debounced discrete`

#### `Auto-Move`

- 处理函数：`AutoMoveHandler::slot4 = 0x140709080`
- 证据：
  - press-edge 触发
  - 直接翻转目标状态位
- 语义结论：
  - `Toggle-like discrete`

#### `Toggle Always Run`

- 处理函数：`ToggleRunHandler::slot4 = 0x140709690`
- 证据：
  - press-edge 翻转状态位
- 语义结论：
  - `Toggle-like discrete`

#### `Run`

- 处理函数：`RunHandler::slot4 = 0x140709370`
- 证据：
  - 按下和释放都参与状态更新
- 语义结论：
  - `Hold / Stateful`

## UI / interface handler 家族

Interface root 构造函数：

- `sub_1408A8DF0`

该函数明确注册了这些 handler：

- `ClickHandler`
- `DirectionHandler`
- `ConsoleOpenHandler`
- `QuickSaveLoadHandler`
- `MenuOpenHandler`
- `FavoritesHandler`
- `ScreenshotHandler`

### 已实锤的 UI 语义

#### `Accept / Cancel / Click`

- 处理函数：
  - `ClickHandler::slot5 = 0x1408A9890`
- 证据：
  - press-edge 触发
  - 通过 UI event queue 投递离散事件
  - 没有持续 held 语义
- 语义结论：
  - `Pulse`

#### `Up / Down / Left / Right`

- 处理函数：
  - `DirectionHandler::slot5 = 0x1408A99B0`
  - `DirectionHandler::slot3 = 0x1408A9B20`
- 证据：
  - 会维护当前方向
  - held current-state 下会持续产出方向事件
  - 与 producer `prevDown || curDown` 每 Poll 继续入队的证据一致
- 语义结论：
  - `Repeat-from-held current-state`

#### `Quicksave / Quickload`

- 处理函数：
  - `QuickSaveLoadHandler::slot5 = 0x1408AA940`
- 证据：
  - press-edge 触发
  - 识别 `Quicksave` 与 `Quickload`
  - 没有持续 held 语义
- 语义结论：
  - `Pulse`

#### `Tween Menu / Pause / Journal / Quick Inventory / Quick Magic / Quick Stats / Quick Map / Wait`

- 处理函数：
  - `MenuOpenHandler::slot5 = 0x1408AA140`
- 证据：
  - 全部是离散触发
  - handler 内做的是 menu open / cancel / overlay / wait 之类操作
  - 没有 repeat 或 hold owner 语义
- 语义结论：
  - `Pulse`

#### `Favorites`

- 处理函数：
  - `FavoritesHandler::slot5 = 0x1408A9D60`
- 证据：
  - press-edge 触发
  - menu state / gameplay state 满足时执行一次性操作
- 语义结论：
  - `Pulse`

#### `Screenshot / Multi-Screenshot`

- 处理函数：
  - `ScreenshotHandler::slot5 = 0x1408AAA10`
- 证据：
  - press-edge 触发
  - 仅设置两类 screenshot 标志位
- 语义结论：
  - `Pulse`

## producer 级证据

### 数字 held 的原生 repeat 语义

已验证：

- `BSWin32GamepadDevice::Poll = 0x140C1AB40`
- 数字 producer helper = `0x140C190F0`

关键事实：

- `prevDown || curDown` 时仍会继续走 producer helper
- held current-state 时，Skyrim 原生会每 Poll 继续产出数字记录

结论：

- `Menu Up / Down / Left / Right` 这类 repeat，不该在 backend 里私造无限 pulse 队列
- 更接近原生的方法是：
  - lifecycle 层维持 owned-down
  - producer 自己从 current-state 继续导出 repeat

## 当前项目动作面的推荐对齐

| 项目动作 | 原生名字 / 家族 | 原生语义 | 当前建议 |
|---|---|---|---|
| `Game.Jump` | `Jump` / `JumpHandler` | `Pulse` | `ButtonEvent + PulseMinDown` |
| `Game.Activate` | `Activate` / `ActivateHandler` | `Pulse` | `ButtonEvent + PulseMinDown` |
| `Game.Sprint` | `Sprint` / `SprintStart/Stop` / `SprintHandler` | `Hold / Start-Stop` | `ButtonEvent + HoldOwner` |
| `Game.Sneak` | `Sneak` / `sneakStart/stop` / `SneakHandler` | `Toggle-like discrete` | `ButtonEvent + ToggleDebounced` |
| `Game.Shout` | `Shout` / `ShoutHandler` | `Hold / Stateful` | `ButtonEvent + HoldOwner` |
| `Game.TogglePOV` | `Toggle POV` / `TogglePOVHandler` | `Pulse` | `ButtonEvent + Pulse` |
| `Game.Attack` | `Left/Right Attack/Block` / `AttackBlockHandler` | `Hold / trigger-stateful` | 不再作为正式独立 action transport；原生线应通过虚拟 `RT/LT` current-state 交给 Skyrim 自己推导 |
| `Game.Block` | `Left/Right Attack/Block` / `AttackBlockHandler` | `Hold / trigger-stateful` | 不再作为正式独立 action transport；原生线应通过虚拟 `RT/LT` current-state 交给 Skyrim 自己推导 |
| `Menu.Confirm` | `Accept` / `ClickHandler` | `Pulse` | `ButtonEvent + PulseMinDown` |
| `Menu.Cancel` | `Cancel` / `ClickHandler` | `Pulse` | `ButtonEvent + PulseMinDown` |
| `Menu.ScrollUp/Down/Left/Right` | `Up/Down/Left/Right` / `DirectionHandler` | `Repeat-from-held` | `ButtonEvent + RepeatOwner` |

## 当前已确认的“不要再混同”的点

### 1. `Shout` 不是 pulse

它和 `Jump / Activate / Confirm` 不同，原生更接近 stateful hold。

### 2. `TogglePOV` 不该继续走插件私有相机切换

游戏里已经有原生 `Toggle POV` handler，且是离散语义。

### 3. `Attack / Block` 不是简单数字 button pulse

它们更接近 trigger 家族的 stateful 处理。

补充：

- 更准确地说，它们也不是“插件侧应该直接维护的一套战斗动作状态机”
- 当前更合适的架构方向是：
  - 插件侧只 materialize 虚拟手柄 `LT/RT` 硬件状态
  - 游戏侧 `trigger producer -> AttackBlockHandler` 继续负责原生战斗语义

### 3.1 数字键 / 摇杆 / 扳机的统一边界

基于这轮 IDA 结果，三类输入都能统一到“虚拟手柄硬件状态”这一层，但不应该统一成同一套插件侧语义：

- 数字键
  - `Poll` 内通过 `0x140C190F0`
  - 更接近 `current down + held` 驱动的 button event
- 摇杆
  - 先通过 `0x140C16E40` 做 deadzone/归一化
  - 再通过 `0x140C16AB0` 生成 `x / y` 二维向量事件
  - 左右摇杆对应 event id `11 / 12`
- 扳机
  - 先通过 `0x140C16FB0` 做阈值归一化
  - 再通过 `0x140C19220` 生成 `current / previous` trigger 事件
  - 左右扳机对应 event id `9 / 10`

因此更合理的统一方式是：

- 插件侧统一 materialize `wButtons / LT / RT / thumbsticks`
- 让 Skyrim 自己继续区分 button producer、stick producer、trigger producer
- 不在插件里重写这三类原生 handler 的消费逻辑

### 4. `Book.PrevPage / NextPage` 不应与 `PageUp / PageDown` 继续视作同一件事

注册表里同时存在：

- `PageUp / PageDown`
- `PrevPage / NextPage`

因此当前代码把 `Book.PreviousPage / NextPage` 压成 `MenuPageUp / MenuPageDown` 只是历史兼容写法，不应被视为原生定义。

### 5. `OpenInventory / OpenMap / OpenJournal / OpenFavorites / OpenSkills`

这些当前是插件动作名，不等价于已经被 current-state 物理按钮面稳定承载的原生 user event。

当前可以确认游戏有这些相关原生名字：

- `Inventory`
- `Stats`
- `Map`
- `Journal`
- `Favorites`
- `Quick Inventory`
- `Quick Magic`
- `Quick Stats`
- `Quick Map`

但它们和当前手势动作之间还不是一一对应关系，所以这轮不强行迁到 `ButtonEvent` 主线。

## 本轮代码改造准则

只迁移满足以下条件的动作：

1. 原生 handler / 语义已被 IDA 实锤
2. 当前主线能用 `Poll current-state ownership` 安全表达
3. 与现有物理按钮有明确一一对应关系

因此本轮优先迁正：

- `Shout`
- `TogglePOV`
- `Attack / Block` 的 contract 归类

而以下内容先记录，不抢改：

- `Wait / QuickSave / QuickLoad`
- `Inventory / Map / Journal / Favorites / Stats` 与当前手势动作之间的一一对应
- `PageUp / PageDown` 与 `PrevPage / NextPage` 的彻底拆分
## 第二批补充验证（2026-03-14）

### `sub_140C17FB0` 已确认的关键偏移

这轮把注册表偏移也直接钉出来了，后续讨论 UI 家族时应优先引用这些偏移，而不是只说字符串名：

- `Quicksave` = `+200`
- `Quickload` = `+208`
- `Console` = `+264`
- `Tween Menu` = `+280`
- `Accept` = `+296`
- `Cancel` = `+304`
- `Up / Down / Left / Right` = `+312 / +320 / +328 / +336`
- `PageUp / PageDown` = `+344 / +352`
- `PrevPage / NextPage` = `+520 / +528`
- `Quick Inventory / Quick Magic / Quick Stats / Quick Map` = `+648 / +656 / +664 / +672`
- `Wait` = `+688`
- `Click` = `+696`

### `PageUp / PageDown` 不是 `DirectionHandler` 那一族

这轮新增的实锤是：

- `DirectionHandler` 明确只直接比较 `Up / Down / Left / Right`（`+312 / +320 / +328 / +336`）
- `sub_1408A8650` 会直接比较 `Accept / Cancel / Tween Menu / PageUp / PageDown`
  - `Accept = +296`
  - `Cancel = +304`
  - `PageUp = +344`
  - `PageDown = +352`
  - `Tween Menu = +280`

结论：

- `PageUp / PageDown` 至少不应继续被视为“和方向键完全同一 handler 家族”
- 因此当前项目里把它们简单归入“方向键 repeat 语义”的做法，只能算保守兼容，不应视为原生定义

### `PrevPage / NextPage` 已确认存在，但本轮尚未完全追到直接 consumer

这轮已经确认：

- `PrevPage = +520`
- `NextPage = +528`

但它们的直接 consumer 还没有像 `PageUp / PageDown` 那样在本轮完全钉到具体 handler。

因此当前项目采取的收敛方式是：

- 先把 `Book.PreviousPage / Book.NextPage` 从 `Menu.PageUp / Menu.PageDown` 的语义身份里拆开
- 在代码层面保留独立的 action 常量和独立的 `NativeControlCode`
- 但在当前 `ButtonEvent current-state` 主线上，仍允许它们落到相同的物理位（`L1 / R1`）

也就是说，当前状态是：

- 语义身份已拆开
- 物理位暂时仍可复用
- lifecycle policy 还不应在没有更多逆向证据前贸然重定

### `Wait / QuickSave / QuickLoad` 的当前边界

这轮进一步确认：

- `QuickSave / QuickLoad` 原生属于 `QuickSaveLoadHandler`，语义是 `Pulse`
- `Wait` 原生属于 `MenuOpenHandler`，语义也是 `Pulse`

但当前主线并不是“直接发原生 user event 名字”，而是：

- 走 `BSWin32GamepadDevice::Poll` 的 synthetic gamepad current-state
- 由游戏自己把物理手柄位翻译成 user event

因此是否能安全迁移，取决于“当前虚拟手柄面是否真的能物理表达该动作”，而不只是“游戏里存在这个原生名字”。

当前项目默认绑定里：

- `Wait` 绑定在 `Create`
- `QuickSave` 绑定在 `BackLeft`
- `QuickLoad` 绑定在 `BackRight`

其中：

- `Create` 仍在当前 XInput bridge 可表达面内
- `BackLeft / BackRight` 目前不在标准 XInput button bridge 主面内

所以本轮结论是：

- `QuickSave / QuickLoad` 继续保留插件路径，不抢迁
- `Wait` 已可从 `0x0020 -> Back/Create` 这条当前主线稳定物理位进入 future migrate 候选，
  但本轮先不抢改 action 面与默认绑定
- `Journal` 同理，`0x0010 -> Options/Start` 已在当前 current-state bridge 可稳定表达，
  不属于“没有稳定 current-state 物理位”的 future 清单

### 本轮代码对齐结果

- `TogglePOV` 已明确收回到原生 `ButtonEvent + Pulse` 主线
- 旧的插件私有相机切换实现已退出主执行路径
- `Book.PreviousPage / Book.NextPage` 已拥有独立 action 常量与独立 `NativeControlCode`
- 当前 `ButtonEvent` 会把它们 materialize 到 `DPad Left / DPad Right`，以对齐当前
  `BookMenu` 里已验证可工作的物理翻页位；它们在代码语义层面仍然不是
  `Menu.PageUp / Menu.PageDown` 的同义词
- `Menu.PageUp / Menu.PageDown` 已从 `RepeatOwner` 保守退回默认 `Pulse` 语义；`Book.PreviousPage / NextPage` 继续维持独立身份，等待更多 consumer 证据后再决定是否同步调整 policy

## 第三批全量复核（2026-03-14）

这轮额外对“当前项目里已经建模成 actionId，并且会进入 `ButtonEvent / CompatibilityFallback`
数字主线”的动作面做了一次逐项复核。目标不是把所有原生事件都立刻迁进主线，而是确认：

- 现在已经接入的动作里，哪些 contract 与原生 handler 语义一致
- 哪些只是实现层 materialization 错了
- 哪些确实应该立刻改 policy

本轮复核后的收敛结论是：

- `Jump / Activate / MenuConfirm / MenuCancel / TogglePOV / MenuPageUp / MenuPageDown`
  继续按 `Pulse` 处理
- `Sprint / Shout / Attack / Block` 继续按 `Hold / Stateful` 处理
- `MenuScrollUp / MenuScrollDown / MenuLeft / MenuRight`
  以及当前复用它们的 `DialoguePreviousOption / DialogueNextOption / FavoritesPreviousItem /
  FavoritesNextItem / ConsoleHistoryUp / ConsoleHistoryDown`
  继续按 `Repeat-from-held current-state` 处理
- `Sneak` 继续保留 `Toggle-like discrete` 的语义身份，但在 executor 层必须 materialize 成
  press-edge 的 debounced pulse，不能再把它锁成 held-latch
- `Book.PreviousPage / Book.NextPage` 本轮正式从 `RepeatOwner` 收回 `Pulse`

这轮实锤出的两个修正点分别是：

1. `Book.PreviousPage / Book.NextPage`
   - 目前仍未完全钉到直接 consumer
   - 但已经没有原生证据支持把它们归入 `DirectionHandler` 那种 `Repeat-from-held` 家族
   - 实际运行里它们又主要来自触摸板滑动这种离散事件
   - 结合当前运行时验证到的“书本里 DPad 左右可以翻页”，当前 `ButtonEvent`
     主线的安全 materialization 应为 `Pulse + DPad Left/Right`，而不是 `RepeatOwner`
     或 `L1 / R1`

2. `Sneak`
   - 原生 `SneakHandler` 仍然是 press-edge 驱动的离散切换
   - 之前的问题不在 contract 判断，而在 `ToggleDebounced` 被 executor 错误 materialize
     成了 held current-state
   - 因此这轮把修正落在 executor，而不是把 `Sneak` 改回 `Hold`

## 第四批 `controlmap.txt` 核对（2026-03-14）

这轮额外对 `G:/skyrim_mod/bsa-interface/interface/controls/pc/controlmap.txt`
做了直接核对，目的是确认当前 `ButtonEvent current-state` 主线上已经接入的物理位，
哪些确实与 PC controlmap 一致，哪些仍然只是项目侧的保守/历史映射。

### 已与 PC controlmap 对齐的当前主线映射

- `Game.Jump` -> `0x8000` -> `360_Y`
- `Game.Activate` / `Menu.Confirm` -> `0x1000` -> `360_A`
- `Menu.Cancel` / `Book.Close` -> `0x2000` -> `360_B`
- `Game.Sprint` -> `0x0100` -> `360_LB`
- `Game.Sneak` -> `0x0040` -> `360_L3`
- `Game.Shout` -> `0x0200` -> `360_RB`
- `Game.TogglePOV` -> `0x0080` -> `360_R3`
- `Menu.Up / Down / Left / Right` -> `0x0001 / 0x0002 / 0x0004 / 0x0008`
- `Book.PreviousPage / Book.NextPage` -> `0x0004 / 0x0008`

其中最后一项是这轮新确认的重点：

- `Book` 上下文里的 `PrevPage / NextPage` 在 PC controlmap 中明确就是 gamepad
  `Left / Right`
- 因此 `Book.PreviousPage / Book.NextPage` 不应再 materialize 到 `L1 / R1`
- 当前代码已经同步改成 `DPad Left / DPad Right`

### 当前仍然不能视作“已被 PC controlmap 实锤”的项目映射

- `Menu.PageUp / Menu.PageDown`
  - PC controlmap 里有 `PageUp / PageDown` 名字
  - 但在 `Menu Mode` / `Book` 这类当前主线上下文里，并没有对应的 gamepad `0x0100 / 0x0200`
  - 因此当前项目把它们落到 `L1 / R1` 只能算项目侧 helper 语义，不能视为原生定义

- `Console.Execute`
  - PC controlmap 的 `Console` 上下文里没有一个叫 `Execute` 的 gamepad user event
  - 当前 `Console.Execute -> MenuConfirm(360_A)` 也只能算项目侧约定，不应写成“原生 controlmap 映射”

- `ConsoleHistoryUp / ConsoleHistoryDown`
  - PC controlmap 的 `Console` 上下文原生名字是 `PickNext / PickPrevious`
  - 当前项目把它们复用成 `MenuScrollUp / Down` 的 held-repeat，只能算近似语义，不是名字级一一对应

### 后续应优先关注的 controlmap 偏差点

- `MapMenu` 当前若继续沿用通用 `[Menu]` 绑定，并不严格符合 PC controlmap
  - 因为 `Map Menu` 里的 `Up / Down / Left / Right` gamepad 列是 `0xFF`
  - 真正常驻的 gamepad 事件是 `Look / Click / PlayerPosition / LocalMap / Journal`

- `Journal / Inventory / Stats / Creations`
  - 这些上下文里都有各自特有的 gamepad action
  - 当前若只继承通用 `[Menu]`，最多算可用默认，不算“按原生 controlmap 定义对齐”

## 第五批：已确认“原生 user event 存在，但当前没有稳定 current-state 物理位”的 future 清单（2026-03-14）

这批动作后续如果要做得更原生，应优先考虑：

- 补更完整的按上下文 current-state 物理面
- 或新增 direct native event 路，而不是继续走 `UIMessage` / 插件捷径

当前先明确记录，不在这轮硬迁。

### A. 明确没有 gamepad current-state 位的原生名字

- `Quicksave / Quickload`
  - `controlmap.txt` 的 gamepad 列是 `0xFF`
  - 当前默认绑定落在 `BackLeft / BackRight`，属于项目私有扩展位，不在标准 XInput current-state 主面内

- `Quick Inventory / Quick Magic / Quick Stats / Quick Map`
  - 原生名字存在
  - `controlmap.txt` 的 gamepad 列是 `0xFF`
  - 如果以后要走更原生的路径，更适合 direct native event 或重新定义项目 action 面

- `Pause / Console / Screenshot / Multi-Screenshot / CameraPath`
  - 原生名字存在
  - Gameplay controlmap 没有稳定 gamepad current-state 位
  - 不应误判成“只要接入 ButtonEvent current-state 就能自然得到”

### B. 原生存在，但当前不是稳定单物理位表达

- `Favorites`
  - Gameplay controlmap 的 gamepad 定义是 `0x0001,0x0002`
  - 这是组合位，不是当前主线里那种稳定单物理位 current-state 表达
  - 后续若要更原生，需要单独决定是组合 current-state 还是 direct native event

### C. 当前项目 action 面相关，但还没有稳定一一对应 current-state 物理身份

- `Game.OpenInventory / Game.OpenMagic / Game.OpenMap / Game.OpenFavorites / Game.OpenSkills`
  - 游戏里存在相关原生名字或近邻家族
  - 但当前项目 action 面与原生 `Inventory / Quick Inventory / Map / Quick Map / Favorites / Stats / Quick Stats`
    之间还不是稳定的一一对应
  - 后续不应直接继续沿用 `UIMessage` 作为最终形态，但也不能草率塞进当前 `ButtonEvent` 物理面

### D. 当前明确不属于这份 future 清单的

- `Wait`
  - Gameplay controlmap = `0x0020`
  - 当前 XInput current-state bridge 已可稳定表达该物理位

- `Journal`
  - Gameplay controlmap = `0x0010`
  - 当前 XInput current-state bridge 已可稳定表达该物理位

## 第六批：`RepeatOwner` 二阶段收敛（2026-03-14）

本轮没有把 repeat contract 再次私有化到 backend，也没有开始在 executor 里伪造独立 repeat pulse 时钟。

当前的二阶段收敛点是：

- `RepeatOwner` 不再简单等同于 `HoldOwner`
- 它现在会保留首个 `Press` edge，即使 `Release` 在下一个 Poll 前到达，也不会让这次短按在 Poll 窗口里直接蒸发
- 一旦第一拍被 materialize 成可见 `down`，后续持续按住仍然保持 current-state `down`，
  由 Skyrim 自己的 producer 继续导出 native repeat-from-held 记录

也就是说，本轮 `RepeatOwner` 的目标不是“在 backend 里再造 repeat 时钟”，而是：

- 修掉“短按方向/列表动作在一个 Poll 窗口里被 hold 化实现吞掉首 edge”的结构问题
- 继续把 current-state held 交给原生 producer 处理
