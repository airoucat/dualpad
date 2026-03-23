# Backend Routing Decisions

本文只记录当前仍然有效的 backend ownership / routing 结论，不再保留已经被旧实验推翻的分流设想。

## 当前运行时结论

当前默认运行时桥接是：

- 在 `BSWin32GamepadDevice::Poll` 的内部 `XInputGetState` call-site 上桥接 synthetic state
- 让 Skyrim 继续用自己的 producer 语义从 current-state 推导原生 gamepad/user event

因此当前主线仍然是：

- `virtual gamepad current-state`
- 不是 direct `ButtonEvent(userEvent)` 拼接
- 也不是 keyboard-native 主线

补充：

- 当前 `AuthoritativePollState` 的正式口径是“虚拟 XInput 手柄硬件状态”
- Skyrim 原生 user event 继续由游戏自己的 `producer / handler` 从这份硬件状态推导

## 当前 backend ownership

### 1. NativeButtonCommitBackend

当前默认数字主线 backend。

负责：

- 已确认能安全 materialize 为 gamepad current-state 的数字动作
- `Jump / Activate / Sprint / Sneak / Shout / TogglePOV`
- `Menu Confirm / Cancel / Up / Down / Left / Right`
- `Book PreviousPage / NextPage`

特点：

- 消费 `FrameActionPlan`
- 通过 `PollCommitCoordinator` 维护 Poll 可见性
- 最终写入的是标准手柄按钮位，不直接构造 `BSInputEvent`

### 2. Native analog publish

当前默认模拟量主线不是独立 backend 类，而是 `PadEventSnapshotProcessor` 对 `FrameActionPlan` 里的 `NativeState` 动作做统一发布。

负责：

- Move stick
- Look stick
- Trigger analog state

特点：

- 只做模拟量与 native pad-state ownership
- 不应重新扩张成“所有数字动作的家”
- 对扳机，正式输出是 `LT/RT` 硬件字节；`Attack / Block` 等原生战斗语义继续由 Skyrim 自己的 `AttackBlockHandler` 推导

### 3. Unmanaged raw digital publish

`CompatibilityFallback` 已退出当前运行时代码里的 backend 枚举与分发路径，现仅作为历史概念保留。

当前实际保留的是 `PadEventSnapshotProcessor` 内部的 unmanaged raw digital publish 步骤。

负责：

- 历史兼容路径残留
- 向统一 `AuthoritativePollState` 补写 unmanaged raw digital state

规则：

- 不能回流成默认数字主线
- 当前没有正式公开动作再依赖这条路径
- 只能作为内部过渡/兜底层存在
- 当前只根据 `SyntheticPadFrame` 的 raw digital edge 结果补写统一 poll state，不再接受 dispatcher 的旁路 pulse/state 写入

### 4. Plugin action dispatch

负责插件内部动作：

- Screenshot
- Multi-screenshot
- ToggleHUD

这些动作当前不是通过 gamepad current-state 主线表达，而是由 `ActionDispatcher -> ActionExecutor` 明确作为 plugin action 处理。

### 5. ModEvent slots

当前把 `ModEvent1-24` 视为正式公开逻辑槽位，而不是独立 transport backend。

当前定位：

- 面向我方 MCM 的稳定逻辑名
- 对外通过固定虚拟键池与其他 mod 的 MCM / hotkey 系统对接
- 当前只支持 `Pulse`
- 不和 Skyrim control routing 混用

规则：

- `ModEvent1-24 -> 固定虚拟键` 映射必须稳定，不能做用户可配 ABI
- 玩家可配置的是“哪个手柄输入触发 `ModEventN`”，不是“`ModEventN` 底下具体是哪一个虚拟键”
- `Hold / Toggle / Repeat / 命名式 Mod action` 留作后续扩展
- 已补代理检测状态，供未来 MCM 使用：
  - `HasProxyDllInGameRoot()`
  - `HasActiveBridgeConsumer()`
  - `ShouldExposeModEventConfiguration()`
  - `IsModEventTransportReady()`

### 6. KeyboardHelperBackend

正式模块名采用 `KeyboardHelperBackend`。

当前代码实现与职责口径已统一为 `KeyboardHelperBackend`。

当前不是 PC 原生控制事件主线。

当前定位：

- helper / low-latency simulated keyboard output
- `VirtualKey.*`
- `FKey.*`
- 固定虚拟键池
- mod/helper key output

规则：

- 不再默认承担 `Jump / Activate / Confirm / Sprint` 这一类 Skyrim 原生动作
- 不应再次被误当成默认数字主线
- 当前也是 `ModEvent` 的实际 materialization backend
- 当前所有 keyboard helper 输出统一走 `dinput8` 代理桥接的 simulated keyboard path
- `BSWin32KeyboardDevice::Poll` 本地原生 hook 只保留为未来 RE 方向，不再属于当前运行时 routing
- `Wait / Journal` 已回到 `NativeButtonCommitBackend` 的 current-state 主线
- `Game.Attack / Game.Block` 旧 compatibility action 路径已撤出正式支持面
- `Pause / NativeScreenshot / Hotkey3-8` 当前不再走 `KeyboardHelper`，而是作为独立原生 action 身份接到 `NativeButtonCommit -> AuthoritativePollState`；这批动作由 DualPad 在运行时覆盖 gamepad `ControlMap` 到固定 combo ABI，且默认不占用现有手柄键位
- `OpenInventory / OpenMagic / OpenMap / OpenSkills` 当前已撤出正式支持面；它们所属的 `MenuOpenHandler` 家族在当前 mod 栈下不稳定，不再继续作为 combo-native 默认能力推进
- 当前 combo-native action 的 materialization 规则是“第一拍直接下完整组合”；此前尝试过 staged prelude，但会破坏 `Pause / NativeScreenshot` 这类首拍型 handler，因此当前不再使用
- `OpenFavorites` 当前仍撤出正式支持面，不走 `KeyboardHelper`，也不默认回退到 UI message

## 当前 routing 原则

### Planner 优先

先做：

- context resolve
- trigger resolve
- combo/tap/hold 解释
- ownership 决策

再做：

- backend 选择
- poll commit
- plugin dispatch

### 一个 stateful lifecycle 只能有一个 owner

例如：

- `Sprint` 一次 hold 只能归一个 backend
- `MenuDown` 一次 repeat 不能在不同 backend 间跳
- `Sneak` 一次 toggle 不能中途换 owner

### Mixed output 是允许的

同一帧可以合法同时存在：

- analog current-state
- digital current-state
- plugin action
- mod event

但不能让同一个动作生命周期在 backend 间跳转。

### Hardware-state first

- 对手柄原生线，插件侧优先 materialize 标准 XInput 硬件位/轴
- 不再把 `Game.Attack / Game.Block` 这类原生 family 当成插件侧独立 transport 目标
- 同一原则后续可继续推广到其它具备明确标准手柄硬件身份的原生按钮
- 这里的“统一”只指统一到硬件状态层：
  - 数字键、摇杆、扳机在 Skyrim 原生里仍由不同 producer / handler 家族消费
  - 插件不应把它们强行压成一套自定义 gameplay 语义状态机

### InputModalityTracker

- `InputModalityTracker` 只负责输入模式层，不负责动作 routing。
- 参考 AutoInputSwitch 的做法，允许键盘/鼠标与手柄同时生效，并驱动 `IsUsingGamepad / GamepadControlsCursor / remap-mode` 的平台判断。
- `KeyboardHelperBackend` 自发的 simulated keyboard 事件必须被 modality tracker 忽略，避免 helper 输出把平台误切到 KBM。
- 因此 “mixed input coexistence” 和 “keyboard helper transport” 是两层不同职责，不应混为同一问题。

## 当前项目侧近似映射

下面这些当前仍是“项目侧可用近似”，不是 vanilla 名字级一比一映射：

- `Console.Execute -> MenuConfirm`
- `ConsoleHistoryUp -> MenuScrollUp`
- `ConsoleHistoryDown -> MenuScrollDown`

保留原因：

- 当前 current-state materialization 可用
- 功能上正确
- 但语义文档里必须明确它们不是已证实的原生名字同构

## 不再继续的方向

- 不再把 keyboard-native 当默认数字主线扩张
- 不再回到旧 consumer-side native button splice
- 不再恢复 `XInputGetState` IAT 输入 fallback
- 不再把 compatibility digital fallback 当平行数字主线

## 当前仍开放的未来路线

- 对少数顺序敏感 UI 场景，未来可研究 `direct native event`
- 对更上游的 native-state 边界，继续做逆向验证
- 对 mod/helper 输出，继续保留 `KeyboardHelperBackend` 作为窄职责 backend

