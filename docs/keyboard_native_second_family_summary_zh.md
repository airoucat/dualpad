# Keyboard-Native 第二家族总结

## 结论

`Activate / Sprint / Sneak` 当前已经可以确认不是 `Jump` 那条处理家族。

- `Jump` 的关键分叉在 `sub_140C11600`，并且已经通过 scoped `gateObj + 0x121 -> 0` coexistence patch 修好。
- `Activate / Sprint / Sneak` 不走这条 `raw-space -> translated Jump` 升级链。
- 这三个动作在 `dinput8` proxy、`GetDeviceState`、`GetDeviceData`、`InjectDiObjDataEvents` 这一整段都已经命中，但游戏内仍然不生效。

## 黑盒事实

在“游戏设置启用手柄”时，真实物理键盘的：

- `E`
- `L-Ctrl`
- `L-Alt`

也都不生效。

这说明第二家族的问题不是：

- `dinput8` proxy 伪装不够像真键盘
- bridge/proxy 丢帧
- 只有 DualPad synthetic keyboard 会失败

而是游戏本身在 `gamepad enabled` 模式下，对这一组键的处理路径就不同。

## 已确认路径

当前静态与运行时结合后的第二家族路径是：

1. `BSWin32KeyboardDevice::Poll`
2. generic raw record 生成链：
   - `sub_140C190F0`
   - `sub_140C16900`
3. 分发到：
   - `sub_1408A7CA0`
   - `sub_1408A85C0`
   - `sub_1408A8650`
4. 在 `sub_1408A8650` 中，通过：
   - `sub_140C151C0(qword_142F257A8, 0, rawCode, &outCode)`
   - `BSWin32KeyboardDevice::vfunc + 0x30 = sub_140C18BA0`
   做 `DIK -> 键码` 查询
5. 然后通过：
   - `sub_140EDA8B0`
   - `sub_140EC3ED0`
   把事件送到 `qword_141EC0A78 + 208`

## 这条路径意味着什么

这条链更像：

- generic keyboard key / interface key 路线
- UI / InterfaceControls handler tree

而不像：

- gameplay control action 语义路线

静态证据包括：

- `sub_1408A8650` 会把原始键码转换成类似 Enter、方向键、普通键码的事件
- `sub_140EDA8B0` 只是往一个小型 key event 缓冲里写记录
- `sub_140EC3ED0` 把这些记录发往 `qword_141EC0A78 + 208`
- 同一棵 handler 树里能看到：
  - `ClickHandler`
  - `DirectionHandler`
  - `ConsoleOpenHandler`
  - `MenuOpenHandler`
  - `FavoritesHandler`

因此第二家族现在最像是：

- 在 `gamepad enabled` 模式下
- 被送进了一条 interface/generic key 路线
- 而不是 gameplay action 语义路线

## 与 Jump 的区别

`Jump`：

- 在 `sub_140C11600` 里完成 `raw-space -> translated Jump`
- 主要问题是 `gateObj + 0x121`

`Activate / Sprint / Sneak`：

- 不在 `sub_140C11600` 里升级成对应 gameplay 语义
- 会进入 `sub_140C15E00`
- 但在 dispatch 时仍保持 raw / empty 或落到错误 family
- 真正问题更早，而且更像 mode gate / 路由选择问题

## 当前最可信判断

第二家族的 blocker 不是单个键的 bridge/proxy 伪装问题，而是：

- Skyrim 在 `gamepad enabled` 模式下
- 把这一组键路由到了 generic key / interface family
- 没有进入它们本应使用的 gameplay action 语义链

## 新增静态结论

进一步静态分析后，可以把第二家族的路径再收窄为：

1. raw keyboard event 先进入 generic raw record 链：
   - `sub_140C190F0`
   - `sub_140C16900`
2. 后续进入：
   - `sub_1408A7CA0`
   - `sub_1408A85C0`
   - `sub_1408A8650`
3. 然后通过：
   - `sub_140EDA8B0`
   - `sub_140EC3ED0`
   送入 `qword_141EC0A78 + 208`

这条链对应的是 generic keyboard / interface key 事件缓冲，而不是
`Jump` 使用的 gameplay action 升级链。

### 关键支持点

- `sub_140C151C0` 的 caller 目前只看到 `sub_1408A8650`
- `sub_140C18BA0` 会把 `DIK` 查询为 keyboard device 自己表里的 `DWORD`
- 该值更像 `keyboard_english.txt` 里的虚拟键/Flash 键码，而不是 controlmap gameplay action
- `sub_140EDA8B0` 只是把事件写入一个 20-byte generic key 缓冲
- `sub_140EC3ED0` 把这条事件发往 `qword_141EC0A78 + 208`

### handler tree 归属

从静态数据可以直接看到同一棵树上包含：

- `ClickHandler`
- `DirectionHandler`
- `ConsoleOpenHandler`
- `MenuOpenHandler`
- `FavoritesHandler`

因此第二家族当前可以高置信度判定为：

- 被导入了 InterfaceControls / generic key handler tree
- 而不是 gameplay controls 的动作语义树

## qword_142EC5BD0 状态位修正

之前对 `qword_142EC5BD0` 某些字段的理解需要修正：

- `+280 / +284`
  - 由 `sub_140C11C60 / sub_140C11CF0 / sub_140C11D10` 维护
  - 是 bitmask 及其保存/恢复副本
- `+288`
  - 由 `sub_140C11F30` 维护
  - 实际是一个递增/递减计数，不是简单布尔

这意味着第二家族当前不应再简单理解成“某个 bool 打开后就压掉键盘”，而更像：

- 某种 mode / tree ownership 计数变化后
- 这组键被导向了 interface/generic key 路线

## 下一步

下一步应继续静态追这两块：

1. `sub_1408A7CA0 / sub_1408A85C0 / sub_1408A8650` 的上游调用者与所属对象
2. `qword_142EC5BD0 + 288 / + 292` 相关 mode 位的设置逻辑

目标不是继续补 proxy，而是找到：

- 在 `gamepad enabled` 模式下
- 谁把第二家族导向了 interface/generic key 路线
- 以及正常 gameplay 语义链本应从哪里进入

## 新增对象关系结论

进一步静态分析后，`qword_142F257A8` 这组对象关系也可以更明确地定性：

- `qword_142F257A8` 不是单纯的 gameplay control owner
- 它更像一个和 `BGSMoviePlayer` / menu controls 相关的注册表或 owner
- `sub_1405A4BE0 / sub_1405A4C80 / sub_1405A5040` 都会把 `BGSMoviePlayer` 或 `BSTEventSink<InputEvent*>` 注册到 `qword_142F257A8`

同时，`sub_1408A7A90` 构造出来的对象也已经比较清楚：

- 它本身是一个 `BSTEventSink<InputEvent*>`
- 同时还挂了 `BSTEventSink<MenuModeChangeEvent>`
- 构造时会创建一整棵 handler tree：
  - `ClickHandler`
  - `DirectionHandler`
  - `ConsoleOpenHandler`
  - `QuickSaveLoadHandler`
  - `MenuOpenHandler`
  - `FavoritesHandler`
  - `ScreenshotHandler`

从它的虚表附近字符串可以直接看到：

- `fKeyboardRepeatDelay::Controls`
- `fKeyboardRepeatRate::Controls`

这说明这棵树更像：

- interface controls
- keyboard repeat / generic keyboard input controls

而不像：

- gameplay controls

## 对当前主线的影响

这意味着第二家族当前最可信的解释进一步收窄为：

- `Activate / Sprint / Sneak` 被送进了一棵以 generic keyboard / interface controls 为核心的处理树
- 这棵树和 menu / movie / keyboard repeat controls 有明显关系
- 因此当前主线不应再假设 `qword_142F257A8` 就是 gameplay action owner

换句话说：

- `Jump` 家族仍然以 `sub_140C11600` 为主战场
- 第二家族则更像是被 mode / ownership / interface routing 提前导错了树

## 新增 generic route 静态结论

继续静态追之后，第二家族的路由可以再收窄一层：

1. `sub_1408A85C0 -> sub_1408A8650` 可以正式视为 generic/interface 键盘翻译树。
   - `sub_1408A85C0` 按 `event+0x0C` 分流。
   - 当 `event+0x0C == 0` 时，直接进入 `sub_1408A8650`。
   - `sub_1408A8650` 内部通过 `sub_140C151C0(qword_142F257A8, 0, rawCode, &mapped)` 查询 keyboard device 的 generic 键码，再调用：
     - `sub_140EDA8B0`
     - `sub_140EDA930`
     - `sub_140EDA980`
     向 `qword_142F4E668` 追加 generic key / repeat / axis 类事件，最后通过 `sub_140EC3ED0(qword_141EC0A78 + 208, ...)` 发到 interface controls 树。

2. `sub_140C151C0` 走的是 `BSWin32KeyboardDevice::vfunc+0x30 = sub_140C18BA0`。
   - `sub_140C18BA0` 只是用 raw DIK 码查 keyboard device 哈希表，并返回表项对象 `+0x0C` 的 `_DWORD`。
   - 结合 `sub_1408A8650` 里出现的常量 `13 / 37..40 / 9`，以及相邻数据里的：
     - `fKeyboardRepeatDelay::Controls`
     - `fKeyboardRepeatRate::Controls`
   - 这更像 `DIK -> generic keycode / virtual-key-like code` 路线，不像 gameplay action 翻译。

3. `sub_140C15180` 那条“拿完整 descriptor”的路径目前不是第二家族主线。
   - 它只被：
     - `sub_140C11F60`
     - `sub_1408F0490`
     使用。
   - 其中 `sub_1408F0490` 明显是在拼装：
     - `text`
     - `buttonName`
     - `sortIndex`
     - `buttonID`
     这类 UI / Scaleform 数据，不像 gameplay 事件消费。

4. `BSWin32VirtualKeyboardDevice` 当前也不是第二家族主战场。
   - `sub_140524180` 的 case `18/19` 确实会通过 `sub_140C152F0(qword_142F257A8)` 取到 slot3 `BSWin32VirtualKeyboardDevice`，再调其 vfunc `+0x58`。
   - 但当前这个 vfunc 槽位在对象上是 `nullsub`，更像模式通知，不像第二家族的实际消费点。

5. 因此第二家族现在更可信的解释是：
   - 它们不是“差一点进入 Jump 的 gameplay 升级链”；
   - 而是在更早就被导入了 `sub_1408A7CA0 -> sub_1408A85C0 -> sub_1408A8650` 这棵 generic/interface 键盘树；
   - 这也解释了黑盒现象：游戏设置启用手柄时，真实物理键盘的 `E / L-Ctrl / L-Alt` 也失效。

## 下一步建议

第二家族当前最值得继续静态追的点是：

1. `sub_1408A7CA0` 所属对象/上游 dispatcher 到底是谁。
   - 现在它只有数据 xref，很像某个 controls tree 的 vtable 方法。

2. `sub_140C15280(qword_142F257A8)` 这条总 gate。
   - `sub_1408A85C0` 的 case `1` 已经明确受它控制。
   - 它本质上又等价于：
     - `BSPCGamepadDeviceHandler` 是否挂着真实 delegate/device。

3. `sub_1408A7CA0` 上游如何决定 second-family 事件进入 `event+0x0C == 0/1/2` 哪条分支。

## 新增对象与总 gate 结论

继续静态分析后，可以再补几条高信号结论：

1. `sub_1408A7A90` 是第二家族当前那棵 interface/generic tree 的实际 owner/dispatcher 构造函数。
   - 它把单例写到 `qword_142F003F8`
   - 同时继承：
     - `BSTEventSink<InputEvent*>`
     - `BSTEventSink<MenuModeChangeEvent>`
   - 构造时会：
     - `sub_1405A5510(qword_142F257A8, this)` 注册到 `qword_142F257A8`
     - `sub_14056B600(qword_141EBEB20 + 96, this + 8)` 注册 menu mode sink
     - `sub_1408A8DF0(this)` 创建 handler tree

2. 这棵树的虚表槽位现在可以更明确地看：
   - `off_1416B8308 + 0x08` -> `sub_1408A9700`（析构/清理）
   - `off_1416B8308 + 0x10` -> `sub_1408A7CA0`
   - `off_1416B8308 + 0x28` -> `sub_1408A7F00`
   - `sub_1408A7CA0` 本身只有数据 xref，没有普通代码调用者，符合“vtable 方法”的特征

3. `sub_1408A7CA0` 是第二家族当前 generic/interface 路由的核心 process 函数。
   - 它遍历输入事件链
   - 命中后先跑 `sub_1408A93E0 / sub_1408A9270`
   - 然后把事件继续分流到 `sub_1408A85C0`
   - 对某些 `raw + no-family` 事件还会直接造一条 generic event 丢进 `qword_141EC0A78 + 208`

4. `sub_140C15280(qword_142F257A8)` 现在可以视为 second-family 的总 gate 之一。
   - 它本质上是：
     - 取 `qword_142F257A8 + 112`（slot2）
     - 调 `BSPCGamepadDeviceHandler::vfunc+56`
   - 而这个 vfunc 的实现 `sub_140C19E00()` 只是：
     - `return handler->delegate != 0`
   - 也就是说，它真正表达的是：
     - 当前是否存在活跃的 gamepad device delegate

5. `sub_1408A85C0` 的 case `1` 已明确受这条 gate 控制：
   - 如果 `sub_140C15280(qword_142F257A8)` 为真，就直接返回
   - 否则走 `sub_1408A8CD0`
   - 这说明 gamepad delegate 的存在，不只是 UI 状态位，而是真会改变 second-family 的处理树

6. 因此第二家族当前最可信的解释进一步收窄为：
   - `Activate / Sprint / Sneak` 进入的是 `qword_142F003F8` 所属的 interface/generic controls tree
   - 这棵树的 process 核心是 `sub_1408A7CA0`
   - 而 `sub_140C15280(qword_142F257A8)`（也就是 gamepad delegate 存在与否）会改变它在内部走哪条支路

## 收敛后的下一步

第二家族现在最值得继续追的，已经不是 proxy，也不是 `11600/15E00`，而是：

1. `sub_1408A7CA0` 内部：
   - `event+0x0C` 是在哪里、按什么条件被设成 `0/1/2`
   - 为什么 `Sprint / Sneak / Activate` 会稳定落到 generic/interface 路径

2. `sub_140C15280(qword_142F257A8)`：
   - 哪些更高层模式切换/设置逻辑会让它保持为真
   - 有没有比全局平台切换更窄的 patch 点，只影响第二家族分支选择

## 新增树结构与注册顺序结论

继续静态分析后，第二家族又多了几条可直接使用的结构性结论：

1. gameplay tree 和 interface/generic tree 都注册到同一个 owner：`qword_142F257A8`。
   - gameplay tree 构造函数是 `sub_140704970`
   - interface/generic tree 构造函数是 `sub_1408A7A90`
   - 两者都会调用 `sub_1405A5510(qword_142F257A8, this)` 注册为 `BSTEventSink<InputEvent*>`

2. 在 `sub_1405ACC20` 初始化顺序里：
   - 先构造 `qword_142EC5BD8 = sub_140704970(...)`（gameplay tree）
   - 后构造 `qword_142F003F8 = sub_1408A7A90(...)`（interface/generic tree）
   - 因此第二家族当前的问题不能简单理解成“gameplay tree 根本不存在”

3. `sub_1408A8DF0` 现在可以把 interface/generic tree 的 child handler 明确命名：
   - `Child[0] / 0x8AAB70` = `ClickHandler`
   - `Child[1] / 0x8AABD0` = `DirectionHandler`
   - `Child[2] / 0x8AABA0` = `ConsoleOpenHandler`
   - `Child[3] / 0x8AAE60` = `QuickSaveLoadHandler`
   - `Child[4] / 0x8AACF0` = `MenuOpenHandler`
   - `Child[5] / 0x8AAC90` = `FavoritesHandler`
   - `Child[6] / 0x8AAEE0` = `ScreenshotHandler`

4. 这进一步说明：
   - 第二家族当前进入的确实是一棵 interface controls / generic key handler tree
   - 而不是 gameplay action handler tree

5. `sub_140704970` 自己就构造了完整的 gameplay handler 集合，至少包括：
   - `SprintHandler`
   - `ActivateHandler`
   - `SneakHandler`
   - `JumpHandler`
   - `ShoutHandler`
   - `ReadyWeaponHandler`
   - `AttackBlockHandler`
   - `TogglePOVHandler`
   - `RunHandler`
   - `MovementHandler`
   - `LookHandler`

6. 因此第二家族当前应理解为：
   - 不是“游戏里没有 Sprint / Activate / Sneak 的 gameplay handler”
   - 而是“这些键在当前模式下没有走到 gameplay tree，反而被导入了 interface/generic tree”

## 新增 remap mode 相关排除结论

还要补一条容易误判但现在已经可以排除的线：

1. `sub_1408F02B0` 不是普通 gameplay 模式切换，而是 `StartRemapMode` 的回调。
   - 它会：
     - `sub_1405A5510(qword_142F257A8, &off_141DF5800)`
     - `*(_BYTE *)(qword_142F003F8 + 130) = 1`
     - `*(_BYTE *)(qword_142EC5BD8 + 80) = 1`

2. 对应关闭逻辑是：
   - `sub_1408F0330`
   - `sub_1408F09C0`
   它们会把上述两个字节恢复为 `0`，并注销 remap sink。

3. 这说明：
   - `qword_142F003F8 + 130`
   - `qword_142EC5BD8 + 80`
   这两个位和 remap mode 明确相关
   - 不能把它们直接等同于“当前 second-family 失败的常规 gamepad enabled 根因”

4. 因此当前第二家族主线仍应保持为：
   - interface/generic tree 路由
   - `sub_140C15280(qword_142F257A8)` 这条总 gate
   - 以及 `sub_1408A7CA0 / sub_1408A85C0` 内部的分支选择

## 新增 shared owner / gameplay tree 结构结论

继续静态往上追以后，第二家族现在又多了几条可直接使用的结构性结论：

1. `qword_142F257A8` 的 shared owner 分发方式已经坐实：
   - 注册通过 `sub_1405A5510(qword_142F257A8, sink)` 完成
   - 实际分发在 `sub_1405A5270`
   - 它会按注册顺序遍历 sink，调用 `vfunc[1]`
   - 只要某个 sink 返回 `true`，就会短路停止

2. gameplay root 和 interface root 的真正 process 入口现在可以直接定名：
   - gameplay root vtable[1] = `sub_140704DE0`
   - interface root vtable[1] = `sub_1408A7CA0`

3. `sub_140704DE0` 这棵 gameplay tree 不是空壳。
   - 它会遍历输入事件链
   - 先调用 `sub_140706C10`
   - 再调用 `sub_140706D90`
   - 其中真正的 gameplay handler 集合由 `sub_1407073B0` 构造并注册

4. `sub_1407073B0` 已确认会显式构造这些 gameplay handler：
   - `SprintHandler`
   - `ActivateHandler`
   - `SneakHandler`
   - `JumpHandler`
   - 以及 `Movement / Look / ReadyWeapon / Shout / AttackBlock / TogglePOV / Run` 等

5. 这说明第二家族当前的问题不能再理解成：
   - “游戏里根本没有 Sprint / Activate / Sneak 的 gameplay handler”
   而应该理解成：
   - “这些事件没有满足 gameplay tree 的 handler 匹配条件，因此 gameplay root 没有把它们吃掉；随后 interface root 继续看到这些事件，并把它们送进 generic/interface tree”

6. gameplay handler 自己的 validate 条件现在也已经能定到 descriptor 槽位：
   - `ActivateHandler::validate = qword_142F25250 + 0x38`
   - `JumpHandler::validate = qword_142F25250 + 0x78`
   - `SprintHandler::validate = qword_142F25250 + 0x88`
   - `SneakHandler::validate = qword_142F25250 + 0x90`

7. 这和当前第二家族的失败形态是吻合的：
   - 它们进入 interface/generic tree 时，比较 family 一直还是 `Click(+0x2B8)`
   - 因此既不会命中上述 gameplay handler 的 descriptor 槽位
   - 后续又会被 `ClickHandler / DirectionHandler` 这一族按 generic/interface 逻辑处理

## 当前最可信的第二家族总图景

到这一步，第二家族当前最可信的图景可以收敛成：

1. `Activate / Sprint / Sneak` 并不是“没有 gameplay tree 可走”。
2. gameplay tree 和 interface/generic tree 都挂在同一个 owner：`qword_142F257A8`。
3. 这些事件当前没有满足 gameplay tree 的匹配条件。
4. gameplay root (`sub_140704DE0`) 没有把它们消费掉。
5. interface root (`sub_1408A7CA0`) 随后继续看到这些事件，并把它们送进 `sub_1408A85C0 / sub_1408A8650` 这棵 generic/interface 键盘树。
6. `sub_140C15280(qword_142F257A8)` 仍然是这棵树的重要总 gate，因为它直接影响 `sub_1408A85C0` 的 case `1` 分支选择。

## 收敛后的下一步

第二家族现在最值得继续追的已经不是：
- proxy provenance
- `sub_140C11600`
- `sub_140C15E00`

而是：

1. 为什么 second-family 事件没有拿到 gameplay handler 期待的 descriptor 槽位。
2. `sub_1408A7CA0 / sub_1408A85C0` 为什么会继续接住这些未被 gameplay tree 吃掉的事件。
3. `sub_140C15280(qword_142F257A8)` 以及其上层模式位，是否存在比“全局平台切换”更窄的 patch 点，只影响第二家族的 routing。

## 新增家族数量与成员清单

继续把 gameplay tree 和 interface/generic tree 自己的注册表拆开后，
目前至少可以把“家族”分成两层来记录：

### 一、当前已经坐实的高层动作家族

这是 DualPad keyboard-native 逆向里真正已经坐实的动作分组：

1. `Jump` 家族
   - 当前已确认成员：
     - `Game.Jump`
   - 特征：
     - 走 `sub_140C11600` 的 gameplay 升级链
     - 关键 gate 是 `gateObj + 0x121`

2. 第二家族
   - 当前已确认成员：
     - `Game.Activate`
     - `Game.Sprint`
     - `Game.Sneak`
   - 特征：
     - 不走 `Jump` 那条升级链
     - 当前更像被导入 `interface/generic` 路由树

说明：
- 目前只应把这两类当作“已经坐实”的高层动作家族。
- 其他动作还没有完成同样强度的确认，不应提前归类。

### 二、shared owner 下的两棵根树

从引擎实现层看，`qword_142F257A8` 下现在已经确认有两棵根树：

1. gameplay root
   - 根 process：`sub_140704DE0`
   - 构造函数：`sub_140704970`
   - 当前已确认注册的 handler 数量：13
   - 当前已确认成员：
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

2. interface/generic root
   - 根 process：`sub_1408A7CA0`
   - 构造函数：`sub_1408A7A90`
   - 当前已确认 child handler 数量：7
   - 当前已确认成员：
     - `ClickHandler`
     - `DirectionHandler`
     - `ConsoleOpenHandler`
     - `QuickSaveLoadHandler`
     - `MenuOpenHandler`
     - `FavoritesHandler`
     - `ScreenshotHandler`

### 三、gameplay root 的 secondary-list 成员

`sub_1407073B0 -> sub_140706070(a3=1)` 还额外给 gameplay root 维持了一组
secondary handler list。当前已确认这组数量为 5：

- `ActivateHandler`
- `SprintHandler`
- `AttackBlockHandler`
- `RunHandler`
- `TogglePOVHandler`

这个结论很重要，因为它说明：

- 第二家族并不等于“secondary-list 家族”
- 例如：
  - `Sprint` 在 secondary list 里
  - `Activate` 在 secondary list 里
  - 但 `Sneak` 不在 secondary list 里
- 然而 `Sneak` 仍然和 `Activate / Sprint` 表现出相同的第二家族失败形态

所以 second-family 的根因不能简单归结为 `a3=1` 那组 secondary-list 分流。

## 新增 gameplay validate / process 级别结论

继续拆 gameplay handler 后，还可以确认：

1. validate 槽位目前已经能稳定定名：
   - `ActivateHandler::validate -> qword_142F25250 + 0x38`
   - `JumpHandler::validate -> qword_142F25250 + 0x78`
   - `SprintHandler::validate -> qword_142F25250 + 0x88`
   - `SneakHandler::validate -> qword_142F25250 + 0x90`

2. 至少对 `Activate / Sprint / Sneak` 来说，handler 本体并不复杂：
   - `SprintHandler::process` 主要看 `event+0x28/+0x2C`
   - `ActivateHandler::process` 主要按按下/释放逻辑走
   - `SneakHandler::process` 也主要按按下/释放逻辑走

3. 因此第二家族当前更应理解为：
   - descriptor / routing mismatch
   - 而不是 handler 本身语义过于复杂

## 新增 shared owner 分发语义结论

`qword_142F257A8` 的 shared owner 分发方式现在也可以固定记录：

1. owner 分发函数是 `sub_1405A5270`
2. 它会按注册顺序遍历 sink，并调用各自的 `vfunc[1]`
3. 只要某个 sink 返回 `true`，owner 就会短路停止继续分发

但当前还有一个关键实现差异：

- gameplay root 的 `vfunc[1] = sub_140704DE0`
- 它当前返回值始终是 `0`
- 也就是说 gameplay root 主要依赖内部 handler side effect，而不是靠 owner-level
  `return true` 去短路

这意味着：

- 如果 second-family 事件没有命中 gameplay handler 的 validate
- gameplay root 就不会把它们“吃掉”
- interface root 随后仍然会继续看到同一事件

这和当前第二家族的静态/运行时现象是吻合的。

## 新增 gameplay root 总 gate 结论

`sub_140704DE0` 在进入 gameplay handler 遍历前，会先经过 `sub_140706AF0`。

当前能确认的是：

- 它不是一个简单的“手柄开/关 bool”
- 它同时依赖：
  - menu / movie / camera / player state
  - 若干全局对象状态
  - 以及 `a1 + 80` 这样的 root 内部状态

因此它目前只能视为：

- gameplay root 的总 allow gate
- 但还不能直接把它当作 second-family 的单一根因

当前更高置信度的判断仍然是：

- second-family 的主问题在 gameplay descriptor/routing mismatch
- 而不是先从 `sub_140706AF0` 整棵 gameplay tree 就被完全关掉开始解释

## 新增 gameplay handler 级别结论

继续把 gameplay tree 自己的 handler 拆开后，第二家族又多了一条很重要的判断：

1. `Sprint / Activate / Sneak / Jump` 的 gameplay handler validate 都非常直接。
   - `ActivateHandler::validate = qword_142F25250 + 0x38`
   - `JumpHandler::validate = qword_142F25250 + 0x78`
   - `SprintHandler::validate = qword_142F25250 + 0x88`
   - `SneakHandler::validate = qword_142F25250 + 0x90`

2. 它们的 process 本体并不像 `Jump` 的 `sub_140C11600` 那样依赖复杂升级链。
   - `SprintHandler::process` 直接看 `event+0x28/+0x2C`（按下/释放幅值）并触发 sprint 相关动作
   - `ActivateHandler::process` 直接按按下/释放时序触发 activate 逻辑
   - `SneakHandler::process` 直接按按下/释放逻辑触发 sneak/toggle 逻辑

3. 这意味着第二家族当前的问题应进一步理解为：
   - 不是“gameplay handler 太复杂所以 keyboard-native 不好伪装”
   - 而是“事件在进入 gameplay tree 时，没有拿到这些 handler validate 期待的 descriptor 槽位”

4. 也就是说，第二家族当前更像是：
   - routing / descriptor family 问题
   - 而不是 `Sprint / Activate / Sneak` 各自后段语义实现的问题

## 新增 shared owner 短路语义结论

还可以再补一条对 patch 方向很重要的结论：

1. `qword_142F257A8` 的 shared owner 分发函数 `sub_1405A5270` 会：
   - 按注册顺序遍历 sink
   - 调用各 sink 的 `vfunc[1]`
   - 一旦某个 sink 返回 `true`，就停止继续分发

2. 但 `gameplay root` 的 `vfunc[1] = sub_140704DE0` 本身当前返回值仍然是 `0`。
   - 它主要靠 side effect 驱动 gameplay handler
   - 这意味着如果 gameplay root 没有在内部把 second-family 事件吃掉，后面的 interface root 仍然会继续看到同一事件

3. 因此第二家族当前更可信的完整图景是：
   - gameplay root 先看到事件
   - 但因为 descriptor 不匹配，`Sprint/Activate/Sneak` 的 gameplay handler validate 没命中
   - gameplay root 没有产生有效 side effect，也没有 short-circuit owner
   - interface root 随后继续接管同一事件，并按 generic/interface tree 处理
## 新增 interface/generic 事件构造链结论（补充）

继续静态分析 `sub_1408A7CA0 / sub_1408A85C0 / sub_1408A8650` 后，可以把
second-family 落入的那条 interface/generic 键盘树再收窄一层：

1. `sub_140EDA8B0`
   - 只是向 `qword_142F4E668` 里的 20-byte generic key 事件缓冲追加一条记录。
   - 这条记录更像小型 generic keyboard event，不像 gameplay action event。

2. `sub_140EDA930`
   - 只是向另一块 12-byte 缓冲追加记录。
   - 当前更像 repeat / 辅助小事件缓冲，不像 gameplay action 构造。

3. `sub_140EDA980`
   - 只是向 24-byte generic 事件缓冲追加记录。
   - `sub_1408A7CA0 / sub_1408A8650 / sub_1408A8CD0` 都会调用它来构造
     interface/generic 事件。

4. `sub_140EC3ED0`
   - 统一把这些 generic/interface 事件发到 `qword_141EC0A78 + 208` 这个 sink。
   - 所以 second-family 当前更像是被包装成 generic/interface key 事件，再发给
     interface controls tree。

## 新增 gameplay handler 复杂度修正（补充）

继续看 gameplay root 里对应 handler 后，可以更明确地修正一个旧印象：

1. `Activate / Sprint / Sneak / Jump` 的 gameplay handler 并不都依赖复杂升级链。
2. `SprintHandler::process`
   - 主要直接看 `event+0x28 / event+0x2C` 的按下/释放值。
3. `ActivateHandler::process`
   - 虽然更长，但本质仍然是按按下/释放时序触发 activate 行为。
4. `SneakHandler::process`
   - 也主要是按下/释放值驱动 toggle/hold 类逻辑。

因此，对 second-family 当前更可信的解释是：

- 不是“gameplay handler 本身太复杂，所以 keyboard-native 很难伪装”；
- 而是“second-family 事件在进入 gameplay tree 时，没有拿到这些 handler validate
  期待的 descriptor 槽位，因此 handler 根本没机会进入自己的正常语义逻辑”。

## 新增当前最可信模型（补充）

截至目前，second-family 的最可信整体模型是：

1. `Activate / Sprint / Sneak` 已经通过 `dinput8` proxy 成功进入游戏。
2. gameplay tree 的对应 handler 也确实存在：
   - `Activate -> +0x38`
   - `Sprint -> +0x88`
   - `Sneak -> +0x90`
3. 但这些事件没有命中上述 gameplay validate descriptor 槽位。
4. gameplay root 因此没有真正吃掉这些事件。
5. 共享 owner 没有在 gameplay root 处 short-circuit。
6. interface/generic root 随后继续看到同一事件，并把它们包装成
   generic/interface key 事件去处理。

这也解释了为什么：

- `Jump` 可以通过 `sub_140C11600 + 0x121` scoped patch 修好；
- `Activate / Sprint / Sneak` 却不是同一类问题。
## 2026-03-11：second-family 第一版直挂 validate 补丁（已回退）

当前已经把 second-family 的第一版补丁从“dispatch owner 下 child validate 枚举安装”前移到 gameplay validate helper 本体：

- `ActivateHandler::validate -> 0x70A5B0`
- `SprintHandler::validate -> 0x70A890`
- `SneakHandler::validate -> 0x70A860`

补丁原则：

- 原 validate 先执行
- 只有原结果为 `false` 时才兜底
- 只对白名单动作生效：
  - `Activate`
  - `Sprint`
  - `Sneak`
- 只接受对应的 raw keyboard-native 事件：
  - `Activate -> DIK_E`
  - `Sprint -> DIK_LMENU`
  - `Sneak -> DIK_LCONTROL`

这一步的目的不是最终优雅解，而是先验证 second-family 的主根因是否真的是 gameplay validate miss。

实测结果：

- 直接对 `0x70A5B0 / 0x70A890 / 0x70A860` 这 3 个 helper 本体做 `write_branch<5>` 会导致运行期闪退。
- 因此这一版已经回退，不作为当前正式方案。
- 结论是：`gameplay validate miss` 这个方向仍然成立，但挂点不能这么粗暴，后续需要改成更保守的安装方式。

当前更保守的新方向：

- 不再直挂 validate helper 本体
- 改为通过 shared owner 的 gameplay root（当前日志里对应 `Parent[2] / sub_140704DE0`）枚举 child validate，并沿用现有 shadow-vtable 方式做 scoped override
- 这样能避开本体 branch hook 的调用约定/重定位风险，同时更贴近 second-family 的真实 miss 点

如果这版有效，后面再决定是否往前收成更优雅的 descriptor/routing patch。
