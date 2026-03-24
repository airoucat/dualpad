# ModEvent 与 KeyboardHelperBackend 约定

本文记录当前 `ModEvent` 适配的正式收口方案。

目标不是再做一条独立的 mod transport，而是把 `ModEvent` 定义成稳定逻辑槽位，再通过固定虚拟键池与外部 mod 的 MCM / hotkey 系统对接。

## 当前正式链路

```text
PadEvent / Trigger
-> BindingResolver
-> FrameActionPlan
-> PlannedBackend::ModEvent
-> ActionDispatcher
-> ModEventN -> 固定虚拟键
-> KeyboardHelperBackend
-> KeyboardNativeBridge / simulated keyboard route
```

说明：

- `ModEvent` 是逻辑动作类别，不是独立 transport backend。
- 仓库里旧的 `ModEventBackend` 只视为历史 stub，不代表当前设计方向。

## 目标场景

当前预设用户流程是：

1. 玩家在 DualPad 自己的 MCM 里，把某个手柄输入绑定到 `ModEventN`。
2. 玩家进入其它 mod 的 MCM / hotkey 绑定界面。
3. 玩家按对应手柄输入。
4. DualPad 将 `ModEventN` materialize 成固定虚拟键。
5. 对方 mod 记录这个虚拟键。
6. 以后再次按该手柄输入时，对方 mod 会像收到真实热键一样响应。

因此 `ModEvent` 必须是稳定 ABI，而不是临时动作名。

## 固定槽位 ABI

当前公开槽位扩充为 `ModEvent1-24`：

1. `ModEvent1 -> VirtualKey.DIK_F13` (`100`)
2. `ModEvent2 -> VirtualKey.DIK_F14` (`101`)
3. `ModEvent3 -> VirtualKey.DIK_F15` (`102`)
4. `ModEvent4 -> VirtualKey.DIK_KANA` (`112`)
5. `ModEvent5 -> VirtualKey.DIK_ABNT_C1` (`115`)
6. `ModEvent6 -> VirtualKey.DIK_CONVERT` (`121`)
7. `ModEvent7 -> VirtualKey.DIK_NOCONVERT` (`123`)
8. `ModEvent8 -> VirtualKey.DIK_ABNT_C2` (`126`)
9. `ModEvent9 -> VirtualKey.NumPadEqual` (`141`)
10. `ModEvent10 -> VirtualKey.L-Windows` (`219`)
11. `ModEvent11 -> VirtualKey.R-Windows` (`220`)
12. `ModEvent12 -> VirtualKey.Apps` (`221`)
13. `ModEvent13 -> VirtualKey.Power` (`222`)
14. `ModEvent14 -> VirtualKey.Sleep` (`223`)
15. `ModEvent15 -> VirtualKey.Wake` (`227`)
16. `ModEvent16 -> VirtualKey.WebSearch` (`229`)
17. `ModEvent17 -> VirtualKey.WebFavorites` (`230`)
18. `ModEvent18 -> VirtualKey.WebRefresh` (`231`)
19. `ModEvent19 -> VirtualKey.WebStop` (`232`)
20. `ModEvent20 -> VirtualKey.WebForward` (`233`)
21. `ModEvent21 -> VirtualKey.WebBack` (`234`)
22. `ModEvent22 -> VirtualKey.MyComputer` (`235`)
23. `ModEvent23 -> VirtualKey.Mail` (`236`)
24. `ModEvent24 -> VirtualKey.MediaSelect` (`237`)

括号中的值是当前 simulated keyboard bridge 实际发出的 raw DirectInput / DIK 十进制键码。

## 选择原则

当前键池遵循：

- 必须出现在 PC `keyboard_english.txt`。
- 不应被当前 `controlmap.txt` 默认占用。
- 尽量选择稀有、稳定、不易与常规输入冲突的键。

说明：

- `PrintSrc` 虽然存在于 `keyboard_english.txt`，但 `controlmap.txt` 已用它做 `Screenshot`，因此不纳入 ModEvent 键池。
- 当前对外只认 raw DIK / DirectInput 键码，不再维护额外兼容码口径。

## 当前合同

当前版本：

- `ModEvent1-24` 是正式公开逻辑槽位。
- `ModEventN -> 虚拟键` 映射必须稳定。
- 玩家可配置的是“哪个手柄输入触发 `ModEventN`”。
- 玩家不可配置 `ModEventN` 底下具体对应哪一个虚拟键。
- 当前 `ModEvent` 只支持 `Pulse`。

当前不做：

- 不做独立 `ModEventBackend` transport。
- 不做用户可改的槽位 ABI。
- 不做 `Hold / Toggle / Repeat`。
- 不做命名式 `Mod.* -> 动态槽位` 注册表。

## KeyboardHelperBackend 当前定位

`KeyboardHelperBackend` 当前是正式 helper backend，负责：

- `ModEvent1-24`
- `VirtualKey.*`
- `FKey.*`

它当前统一走：

- `KeyboardNativeBridge`
- `dinput8.dll` 代理
- simulated keyboard route

它不再承担 Skyrim PC 原生控制事件主线。

## 运行时检测接口

当前已补充给未来 MCM 使用的状态接口：

- `HasProxyDllInGameRoot()`
- `HasActiveBridgeConsumer()`
- `ShouldExposeModEventConfiguration()`
- `IsModEventTransportReady()`

未来 MCM 应基于 `ShouldExposeModEventConfiguration()` 决定是否显示 `ModEvent` 相关内容。