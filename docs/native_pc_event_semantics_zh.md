# PC 原生事件语义追踪

更新日期：2026-03-24

本文只保留当前设计仍然依赖的原生语义结论，不再复述已经退役的旧实验路线。

## 目标

- 搞清楚 Skyrim SE 1.5.97 原生 PC / gamepad user event 的 producer / handler 家族。
- 明确哪些事件适合交给游戏自己的 current-state 链处理。
- 明确哪些 keyboard-exclusive native event 只能作为少量补充能力存在。

## 当前最重要的几条结论

### 1. 当前原生手柄线的正式语义边界

DualPad 当前正式策略是：

- 向游戏 materialize 标准虚拟 XInput 手柄硬件状态。
- 尽量不在插件里重写 Skyrim 原生 producer / handler 语义。

这意味着：

- 标准按钮、摇杆、扳机都优先以硬件位 / 轴形式进入 `AuthoritativePollState`。
- Skyrim 自己的 handler 再从这些硬件事实推导 `Jump / Sprint / AttackBlock / Menu` 等语义。

### 2. 数字按键、摇杆、扳机本来就不是同一类 producer

通过 IDA / MCP 当前已确认：

- 数字键：是 button-style producer，天然带 first-edge / held / repeat 相关语义。
- 摇杆：是二维向量 producer，核心是 `x/y` 连续值。
- 扳机：是 `current/previous` 归一化值 producer，最终进入 `AttackBlockHandler` 家族。

因此：

- 它们可以统一到同一份 `AuthoritativePollState`。
- 但不应在插件里被强行压成同一种语义状态机。

### 3. 扳机原生链路

当前已确认的扳机原生链路是：

`BSWin32GamepadDevice::Poll`
-> trigger threshold / normalize
-> trigger producer
-> `AttackBlockHandler`

对当前实现的含义：

- DualPad 正式职责是稳定输出 `LT / RT` 硬件字节。
- `Attack / Block / Dual Attack / ForceRelease` 继续交给 Skyrim 自己推导。
- 当前不再把 `Game.Attack / Game.Block` 作为插件内部重建的一套平行动作语义。

### 4. keyboard-exclusive native event 不等于“模拟键盘一定稳定”

当前已经验证：

- `Pause / NativeScreenshot` 适合作为 combo-native 能力接入。
- `OpenInventory / OpenMagic / OpenSkills / OpenMap` 所属 `MenuOpenHandler` 家族在当前 mod 栈下不稳定，已经撤出正式支持面。

所以当前口径是：

- 少量高价值、离散、可验证的 keyboard-exclusive native event，可以通过 `ControlMapOverlay` 进入正式面。
- 其余 PC 独占原生事件不继续硬推。

## 当前关注的 handler 家族

### `AttackBlockHandler`

- 负责战斗相关扳机语义。
- 当前设计要求是“交给游戏自己处理”，不是在插件里重写。

### `MenuOpenHandler`

涉及：

- `Quick Inventory`
- `Quick Magic`
- `Quick Stats`
- `Quick Map`
- 以及相关 UI 打开类语义

当前结论：

- 在当前栈下不稳定。
- `OpenInventory / OpenMagic / OpenSkills / OpenMap` 已撤出正式支持面。

### `FavoritesHandler`

- `Favorites` 是独立原生事件。
- 它不应再和 `OpenFavorites` 这类打开类候选动作混写。
- 当前正式保留的是 `Game.Favorites` 原生身份。

### `QuickSaveLoadHandler`

- 当前已知 `QuickLoad` 的原生链可达。
- 但当前产品优先级不高，未纳入正式默认能力。

## 与当前代码的设计对应

当前代码应继续遵守：

- 原生线：硬件状态优先。
- mod 线：虚拟键盘优先。
- `ControlMapOverlay` 只为少量 keyboard-exclusive native event 提供稳定 gamepad ABI，不反过来接管整条原生输入主线。