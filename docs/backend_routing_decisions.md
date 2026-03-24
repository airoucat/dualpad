# Backend Routing Decisions

本文只记录当前仍然有效的 backend ownership / routing 结论，不再保留已经被当前主线取代的旧分流设想。

## 当前正式输出面

当前正式只保留两条输出线：

- 原生手柄线：
  `AuthoritativePollState -> XInputStateBridge -> Skyrim Poll`
- Mod 键盘事件线：
  `KeyboardHelperBackend -> KeyboardNativeBridge -> dinput8 proxy -> third-party mod`

这意味着：

- 当前正式主线是 `virtual gamepad current-state`。
- 不是 direct `ButtonEvent(userEvent)` 拼接主线。
- 也不是 `keyboard-native` 主线。

补充：

- `AuthoritativePollState` 的正式语义是“虚拟 XInput 手柄硬件状态”。
- Skyrim 原生 user event 尽量由游戏自己的 `producer / handler` 从这份硬件状态推导。

## 当前 backend ownership

### 1. `NativeButtonCommitBackend`

当前默认数字主线 backend。

负责：

- 已确认能安全 materialize 为标准手柄 current-state 的数字动作。
- 例如：
  - `Jump / Activate / Sprint / Sneak / Shout / TogglePOV`
  - `Menu Confirm / Cancel / Up / Down / Left / Right`
  - `Book PreviousPage / NextPage`
  - 其它已经接入主表、并拥有明确 native hardware identity 的数字动作

特点：

- 消费 `FrameActionPlan`。
- 通过 `PollCommitCoordinator` 维护 Poll 可见性。
- 输出的是标准手柄硬件位，不直接构造 `BSInputEvent`。

### 2. `AxisProjection`

当前默认模拟量发布层。

负责：

- `Move` / `Look` 摇杆
- `LT / RT` 扳机字节
- 少量上下文专属的摇杆 / 扳机硬件位映射

特点：

- 只做模拟量与 native pad-state ownership。
- 不在这里重做 Skyrim 原生战斗或 UI handler 语义。
- 对扳机而言，当前正式输出是 `LT / RT` 硬件值，`Attack / Block` 等原生战斗语义继续交给 Skyrim 自己的 `AttackBlockHandler` 推导。

### 3. `UnmanagedDigitalPublisher`

当前保留的 raw digital 补写层。

负责：

- 将未被 action 接管、但当前帧真实存在的 raw digital 事实补写进 `AuthoritativePollState`。

规则：

- 它不是平行 backend，也不是默认数字主线。
- 它只补写统一 poll contract，不再承担模拟量中转或 dispatcher 旁路写入。

### 4. 插件动作执行

相关模块：

- `src/input/ActionDispatcher.*`
- `src/input/ActionExecutor.*`

负责：

- `Screenshot`
- `Multi-Screenshot`
- 其它明确属于插件内部逻辑的动作

这些动作不走原生手柄 current-state 序列化，而是作为 plugin action 明确执行。

### 5. `ModEvent` 槽位

`ModEvent1-24` 当前被视为正式公开逻辑槽位，而不是独立 transport backend。

当前定位：

- 面向 DualPad 自己 MCM 的稳定逻辑名。
- 对外通过固定虚拟键池与第三方 mod 的 MCM / hotkey 系统对接。
- 当前只支持 `Pulse`。

规则：

- `ModEventN -> 固定虚拟键` 必须稳定，不作为用户可改 ABI。
- 用户可配置的是“哪个手柄输入触发 `ModEventN`”。
- 用户不可配置 `ModEventN` 底下具体是哪一个虚拟键。

### 6. `KeyboardHelperBackend`

当前正式 helper backend。

负责：

- `ModEvent1-24`
- `VirtualKey.*`
- `FKey.*`

特点：

- 统一走 `KeyboardNativeBridge + dinput8` 的 simulated keyboard route。
- 不再作为 Skyrim PC 原生控制事件主线。
- 需要 `dinput8.dll` 代理在游戏目录可用；未来 MCM 可用 `ShouldExposeModEventConfiguration()` 判断是否显示相关配置。

## 当前已撤出正式支持面的动作

下面这些当前不再作为正式默认能力推进：

- `OpenInventory / OpenMagic / OpenMap / OpenSkills`
  - 所属 `MenuOpenHandler` 家族在当前 mod 栈下不稳定。
- `OpenFavorites`
  - 当前不纳入 combo-native 正式支持面。
- `QuickSave / QuickLoad`
  - 当前产品优先级不高。
- `Console`
  - 当前不纳入 combo-native 正式支持面。

## combo-native 的当前定位

当前只对少量 keyboard-exclusive native user event 维护固定 combo ABI，并通过 `ControlMapOverlay` 在运行时覆盖 gamepad controlmap：

- `Pause`
- `NativeScreenshot`
- `Hotkey3-8`（已接入，待补测）

当前结论：

- `Pause / NativeScreenshot` 已验证可用。
- `OpenInventory / OpenMagic / OpenSkills / OpenMap` 已从这条路线撤出。
- 组合键动作当前采用“第一拍直接 materialize 完整组合”。

## 设计原则

- planner 决定动作合同，backend 不应重新解释 routing。
- 原生线优先 materialize 标准手柄硬件位 / 轴，再交给 Skyrim 自己解释。
- mod 线单独走 keyboard helper，不与原生线混用。