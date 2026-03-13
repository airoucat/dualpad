# Action-Contract-Output 实验方案记录

更新时间：2026-03-13

## 目的

记录当前实验分支 `experiment/action-contract-output` 的设计方向。

这份文档只讨论一件事：

- 如何在 **不回到 `ButtonEvent` 失败路线** 的前提下，
- 为现有 `keyboard-native / scancode / bridge` 主线引入一套更普适的输出生命周期机制。

它不是新的 reverse notebook，也不是当前生产分支的既定结论。

## 背景

当前基线已经说明几件事：

- `Route B` 适合承接模拟量与原生 gamepad state
- `keyboard-native` 已经把 `Jump` 打通
- `Sprint / Activate / Sneak` 的主问题已经从 routing / validate 推进到 process / 时序语义
- `ButtonEvent` / “直接构造动作语义事件”这条路线已经论证失败，后续不再作为主方向

因此，下一个更值得验证的问题不是：

- 再做一个新的动作语义注入器

而是：

- 能不能把现有 keyboard-native 路线从“发 press/release/pulse 命令”升级成“统一的目标输出状态提交机制”

## 核心模型

更准确的链路是：

`输入触发器 -> 上下文 -> 目标输出动作(actionId) -> 输出 contract -> 具体后端/键位`

各层含义：

- 输入触发器
  - 手柄按钮
  - Combo / Hold / Tap
  - Axis
  - Touchpad 手势
- 上下文
  - Gameplay / Menu / Map / Inventory / Lockpicking ...
- 目标输出动作 `actionId`
  - `Game.Jump`
  - `Game.Sprint`
  - `Menu.Accept`
  - `ModEvent1`
- 输出 contract
  - `Pulse`
  - `Hold`
  - `Toggle`
  - `Repeat`
  - `Axis`
- 具体后端/键位
  - keyboard-native scancode
  - native axis
  - plugin 内部调用
  - mod 虚拟键

## 最重要的约束

### 1. contract 属于目标输出动作，不属于输入按键

这是本方案最重要的一条。

不能写成：

- “`L3` 天生是 Hold”
- “`Cross` 天生是 Pulse”

必须写成：

- `L3` 在当前上下文下解析成什么 `actionId`
- 该 `actionId` 需要什么输出生命周期

同一个输入触发器在不同上下文下可以对应不同 contract。

例如：

- `Cross -> Game.Jump -> Pulse -> Space`
- `Cross -> Menu.Accept -> Pulse -> Enter`
- `L3 -> Game.Sprint -> Hold -> L-Alt`
- `Circle -> Game.Sneak -> Toggle/Hold -> L-Ctrl`

### 2. 外部可见输出面尽量不变

本实验应优先替换“内部输出机制”，不要随意改变：

- actionId 命名
- INI 绑定格式
- mod 虚拟键池
- 对外暴露的 scancode/虚拟键约定

目标是：

- 内部统一
- 外部不变

### 3. 不重新引入 ButtonEvent 路线

本实验只讨论：

- keyboard-native
- scancode
- dinput8 bridge / proxy
- 上游键盘 hook 内部状态提交

不讨论：

- 重新构造 `ButtonEvent`
- 重新走“动作语义事件注入”

## 为什么现有实现还不够普适

当前实现已经比最早的 pulse-only 方案好，但它仍然偏“命令式”：

- `KeyboardNativeBackend` 主要暴露 `PulseAction()` / `QueueAction()`
- proxy 侧通过不同 `extra GetDeviceData calls` 处理 release 拖尾
- `Jump / Activate / Sprint / Sneak` 目前仍然容易退化成“动作特定参数”

这会导致两个问题：

- 容易回到“某动作再加一点窗口”的修法
- 难以给未来的自由映射、组合键、mod 输出提供统一生命周期

## 提议的机制变化

把“按命令发 press/release/pulse”改成“按帧提交目标输出状态”。

### 当前模式

- 映射层得出一个事件
- 注入层立刻发 `Press / Release / Pulse`
- 后端和 proxy 尝试把这个命令修补成游戏能稳定消费的形态

### 目标模式

- 映射层仍然先输出整帧原子事件快照
- 注入层把快照归约成一帧的 `DesiredOutputState`
- 输出调度器决定本帧哪些 scancode/轴/虚拟键应为 down / up / active
- 提交器在真正的游戏消费边界上提交状态差分

这意味着：

- 输入层负责“想做什么”
- 输出状态机负责“何时安全地下/保持/释放”

## 建议的三个核心组件

### 1. OrderedSnapshotQueue

职责：

- 保证 snapshot 顺序确定
- 禁止跨帧 merge 破坏原子性
- 明确记录 dropped / overflow / gap

设计要求：

- 允许 producer/consumer 解耦
- 不允许把多帧事件拼成一个逻辑帧
- 若跟不上，宁可显式记录 gap，也不要悄悄 coalesce

### 2. ScancodeLeaseScheduler

职责：

- 维护每个 scancode 的目标状态和生命周期 lease
- 根据 contract 决定：
  - `Pulse` 何时可视为完成
  - `Hold` 何时继续维持
  - `Toggle` 如何翻转
  - `Repeat` 如何重复
- 避免把动作名硬编码成大量分支

关键思想：

- 一个 scancode 的释放，不由“拍脑袋的固定毫秒数”决定
- 而由“是否已经经历足够的游戏消费边界”决定

### 3. KeyboardStateCommitter

职责：

- 在 `GetDeviceState / GetDeviceData / Poll post-event` 等真实消费边界提交状态
- 把 `DesiredOutputState` 变成：
  - `curState` 覆盖
  - `diObjData` 差分事件
  - bridge 命令

它不决定策略，只负责提交。

## 推荐的 contract 集合

第一版不宜过大，建议只保留这几类：

- `Pulse`
- `Hold`
- `Toggle`
- `Repeat`
- `Axis`

解释：

- `Jump / Activate / Menu.Accept` 更像 `Pulse`
- `Sprint` 更像 `Hold`
- `Sneak` 取决于目标行为，可为 `Toggle` 或 `Hold`
- 菜单方向长按、翻页更像 `Repeat`
- 摇杆、扳机仍然是 `Axis`

## 和自由映射的关系

只要 contract 不绑定到输入按键，而是绑定到输出动作，本方案不会天然伤害自由映射。

应维持这些能力：

- 同一个手柄键在不同上下文映射到不同动作
- Combo / Hold / Tap 仍然先于 backend routing 归一化
- 用户仍然通过 INI 写绑定
- 允许未来扩展新的 trigger 类型

不能做的事：

- 因为某个 action 目前更适合 `Hold`，就把某个输入键永久写死为 `Hold`
- 因为某个动作当前走 keyboard-native，就禁止它在别的上下文改走别的后端

## 和整合包 / Mod 兼容性的关系

这套方案理论上更有利于兼容，而不是更差。

前提是守住边界：

- 不乱改 actionId
- 不乱改 mod 虚拟键池
- 不乱改外部看到的 scancode / keycode
- 不把 mod 输出偷偷塞进 Skyrim gameplay 语义链

推荐原则：

- Skyrim 原生控制继续输出 Skyrim 控制
- plugin 自定义动作继续走 plugin 接口
- mod 动作继续走独立的 `Mod.* / ModEvent / VirtualKey.* / FKey.*` 输出面

## 当前已识别的坑

### 1. 不能把这套方案做成新的动作特判表

如果实现时写成：

- `if action == Sprint`
- `if action == Activate`
- `if action == Jump`

然后每个动作都有自己的一组时序参数，
那只是把今天的问题换了个地方继续存在。

正确方向是：

- 优先按 contract 分类
- 必要时按“输出能力类别”分类

只有在确实证明某类输出共享不了语义时，才增加更细粒度层次。

### 2. 不能让 snapshot 生产端原子，消费端却 coalesce

当前代码里最需要警惕的一个风险是：

- 映射层本身是单帧生成的
- 但 `PadEventSnapshotDispatcher` 仍保留过渡期 coalescing patch

这会破坏：

- Tap / Hold 边界
- Combo 顺序
- 生命周期动作的逐帧持续语义

本实验如果不先解决这一点，后面所有时序观察都可能被污染。

### 3. 不能让一个状态动作在一次生命周期里跨后端漂移

例如：

- `Sprint` 的按下走 keyboard-native
- 持有阶段却退回 compatibility
- 松开又回 keyboard-native

这是必须禁止的。

一条 stateful action lifecycle 必须只有一个 owning backend。

### 4. 不能忽视 context 切换时的持有态清理

典型风险：

- Gameplay 里按住 `Sprint`
- 中途切入 Menu / Console / Map
- 旧 lease 未释放

需要明确规则：

- 何时强制 release
- 何时 suspend
- 何时恢复

否则很容易出现“菜单出来后键还卡住”的现象。

### 5. 不能把 text-entry 安全问题重新放开

keyboard-native 后端仍然不是 text input backend。

必须继续保证：

- 文本输入框激活时，不意外打字
- 发生歧义时宁可不输出，也不要输出字符

### 6. 不能忽视多来源共享同一 scancode 的引用关系

未来自由映射增强后，可能出现：

- 不同 action 在不同上下文或不同组合下落到同一 scancode

此时输出状态机必须明确：

- 谁持有 lease
- 是否允许 refcount
- release 的归属如何计算

否则会出现：

- A 还没释放，B 提前把键抬掉
- 或者 A 已结束，但 B 的残留引用导致 stuck key

### 7. 不能把 Toggle 和 Hold 混成一种东西

这两个看似接近，但生命周期完全不同。

- `Hold`：源输入结束即应释放
- `Toggle`：由下一次切换动作决定翻转

若统一成“延迟 release 的 hold”，迟早会在 `Sneak` 这类动作上出问题。

### 8. 不能让 Repeat 退化成大量微 pulse

菜单导航类长按如果只是高频 pulse 堆积，
容易受帧率、队列拥堵、coalescing、游戏内部 repeat 节律影响。

`Repeat` 最好有独立 contract，而不是“很多个 `Pulse`”。

### 9. 不能把 mod 虚拟键和 Skyrim 控制共用同一语义层

mod 虚拟键的目标是：

- 提供稳定、可预留、不会和 controlmap 混淆的输出面

不要因为输出状态机统一了，
就把 mod 虚拟键重新塞回 Skyrim gameplay controls 里。

## 第一阶段建议

如果正式开始实验，建议顺序是：

1. 先去掉 snapshot coalescing，建立真正顺序化的 snapshot 消费链
2. 引入 `DesiredOutputState` / `ScancodeLease` 数据结构，但先只接 keyboard-native
3. 让 `Jump / Activate / Sprint / Sneak` 先只用 contract 驱动，不再直接写动作特判 timing
4. 在 debug 输出中同时打印：
   - 原始 snapshot
   - 归约后的目标输出状态
   - 每个 scancode lease
   - 实际提交差分

## 当前结论

这套实验方案的价值不在于“发明一个新的注入后端”，而在于：

- 把现有 keyboard-native 主线从命令式注入提升成状态式提交
- 让动作生命周期由统一 contract 驱动，而不是由零散 timing patch 驱动
- 保持自由映射、上下文切换、mod 输出边界不变

一句话总结：

- 不是 `输入按键 -> 特判动作 -> 发命令`
- 而是 `输入触发器 -> actionId -> contract -> 输出状态提交`
