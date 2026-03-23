# ModEvent 与 KeyboardHelperBackend 开发约定

本文记录当前 `ModEvent` 适配的正式收口方案。

目标不是再做一条独立的 mod transport，而是把 `ModEvent` 定义成稳定逻辑槽位，再通过固定虚拟键池与外部 mod 的 MCM / hotkey 系统对接。

## 目标场景

目标用户流程是：

1. 玩家在 DualPad 自己的 MCM 里，把某个手柄输入绑定到 `ModEventN`
2. 玩家进入其他 mod 的 MCM 热键绑定界面
3. 玩家按刚才那个手柄输入
4. DualPad 运行时把 `ModEventN` materialize 成固定虚拟键
5. 对方 mod 记录该虚拟键
6. 以后玩家再次按该手柄输入时，对方 mod 会像收到了真实键盘热键一样响应

这要求 `ModEvent` 具备稳定 ABI，而不是临时动作名字。

## 正式命名

- 正式模块名采用 `KeyboardHelperBackend`
- `ModEvent` 是逻辑动作类别，不是独立 transport backend
- 当前真实交付链路是：

`PadEvent/Trigger`
-> `BindingResolver`
-> `FrameActionPlan`
-> `PlannedBackend::ModEvent`
-> `ActionDispatcher`
-> `ModEventN -> 固定虚拟键`
-> `KeyboardHelperBackend`
-> `KeyboardNativeBridge / simulated keyboard route`

仓库里旧的 `ModEventBackend` 只视为历史 stub，不代表当前设计方向。

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

括号中的值是当前 simulated keyboard bridge 实际发出的 raw DirectInput/DIK 十进制键码。

选择原则：

- 必须出现在 PC `keyboard_english.txt`
- 不能被当前 `controlmap.txt` 默认占用
- 优先选稀有、稳定、尽量不与常规输入冲突的键

说明：

- `PrintSrc` 虽然在 `keyboard_english.txt` 里存在，但 `controlmap.txt` 已把它用于 `Screenshot`，因此不纳入 mod 键池
- 当前对外只认 raw DIK / DirectInput 键码，不再维护额外兼容码口径

## 合同约束

当前版本：

- `ModEvent1-24` 是正式公开逻辑槽位
- `ModEventN -> 虚拟键` 映射必须稳定
- 玩家可配置的是“哪个手柄输入触发 `ModEventN`”
- 玩家不可配置 `ModEventN` 底下具体是哪一个虚拟键
- 当前 `ModEvent` 只支持 `Pulse`

当前不做：

- 不做独立 `ModEventBackend` transport
- 不做用户可改的槽位 ABI
- 不做 `Hold / Toggle / Repeat`
- 不做命名式 `Mod.* -> 动态槽位` 注册表

## 为什么当前只做 Pulse

当前目标首先是兼容外部 mod 的 MCM / hotkey bind 场景。

这个场景里，对方通常需要的是：

- 一次明确的 key press
- 一个稳定的键位 identity

因此 `Pulse` 是当前最合适、成本最低、兼容性最好的合同。

如果以后某些 mod 明确需要持续按住语义，再单独扩展更合适。

## 与 MCM 的关系

DualPad 自己的 MCM 应把 `ModEvent1-24` 作为公开槽位展示，而不是直接暴露底层虚拟键名。

推荐展示方式：

- `ModEvent1 (F13 / 100)`
- `ModEvent2 (F14 / 101)`
- `ModEvent3 (F15 / 102)`
- `ModEvent4 (KANA / 112)`

这样玩家能同时理解：

- 这是 DualPad 的逻辑槽位
- 它背后对应的是哪一个稳定虚拟键

## 当前已知边界

- 外部 MCM 绑定时，仍可能遇到菜单导航键与绑定动作同时触发的问题
- 少数 UI / 文本输入环境下，helper key 是否应该继续发出，后续仍需按上下文收细
- 当前 keyboard helper 正式支持面只保留 `ModEvent / VirtualKey / FKey / 固定虚拟键池`
- `Wait / Journal` 已回到手柄原生 current-state 路由
- `OpenInventory / OpenMagic / OpenMap / OpenSkills / Pause / NativeScreenshot / Hotkey3-8` 当前已转为 `controlmap combo profile` 前提下的原生 action surface，不再属于 `KeyboardHelperBackend` 支持面
- `OpenFavorites` 当前仍不属于正式支持面

## 后续扩展记录

下面这些内容记为未来扩展项，但不在当前版本实现：

- `ModEvent` 的 `Hold / Toggle / Repeat` 合同
- 更多 `ModEvent` 槽位
- 更大范围的 mod 键位类型扩展
- 命名式 `Mod.*` 动作到固定槽位的注册表
- 外部 MCM 绑定辅助模式
- 冲突检测与可视化诊断

扩展原则：

- 不能破坏 `ModEvent1-24` 现有 ABI
- 不能把 `KeyboardHelperBackend` 再次扩成默认数字主线
- 新键位类型必须先经过 `keyboard_english.txt + controlmap.txt` 双重核对
