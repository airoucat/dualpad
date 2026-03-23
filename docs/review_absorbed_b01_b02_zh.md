# B01/B02 已吸收审核项清单

这份清单用于记录第三方 Claude 审核后，当前仓库已经吸收、已记录待后续处理、或明确不准备处理的 `B01 / B02` 项目。

状态约定：

- `已吸收`：已经落实到代码/配置/注释
- `已记录待后续`：当前只做 TODO / 记录，未改运行时契约
- `已确认不改`：当前结论是无实际风险或优先级过低，暂不修改

---

## B01 上下文与绑定基础

### F1 `SkillMenu` 上下文缺少 vanilla 原生证据

- 状态：`已吸收`
- 处理方式：
  - 在 [InputContext.h](/c:/Users/xuany/Documents/dualPad/src/input/InputContext.h) 把 `SkillMenu` 标注为“项目预留 / modded UI alias”
  - 在 [InputContext.cpp](/c:/Users/xuany/Documents/dualPad/src/input/InputContext.cpp) 的菜单映射里补同样说明
  - 在 [BindingConfig.cpp](/c:/Users/xuany/Documents/dualPad/src/input/BindingConfig.cpp) 的 `StringToContext()` 里补同样说明
  - 在两份绑定文件里把 `SkillMenu` 明确写成“项目预留，vanilla 通常不会独立触发”
    - [DualPadBindings.ini](/c:/Users/xuany/Documents/dualPad/config/DualPadBindings.ini)
    - [DualPadBindings.ini](/G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPadBindings.ini)
- 说明：
  - 当前保留该上下文是为了兼容未来 modded UI，而不是宣称它是 vanilla 已证实菜单

### F2 `PadBits` 语义别名初始值错误

- 状态：`已吸收`
- 处理方式：
  - 在 [PadProfile.h](/c:/Users/xuany/Documents/dualPad/src/input/PadProfile.h) 修正明显错误的 helper alias：
    - `jump -> triangle`
    - `sneak -> l3`
    - `togglePOV -> r3`
  - 同时加注释，明确这些只是 historical helper alias，不是权威 controlmap 真相

### F3 `SleepWaitMenu` 死匹配

- 状态：`已吸收`
- 处理方式：
  - 在 [InputContext.cpp](/c:/Users/xuany/Documents/dualPad/src/input/InputContext.cpp) 中把 `"SleepWaitMenu"` 修正为 `"Sleep/Wait Menu"`
  - 两份绑定文件的注释也同步改为 `Sleep/Wait Menu`

### F4 `TriggerHash` modifier 哈希质量差

- 状态：`已确认不改`
- 原因：
  - 当前绑定规模很小
  - 正确性不受影响，主要只是潜在哈希碰撞
  - 目前没有实际性能瓶颈证据

### F5 `IsMenuOwnedContext` 中 `Console` 检查冗余

- 状态：`已确认不改`
- 原因：
  - 这是低风险冗余，不影响行为
  - 当前不值得为这类纯样式问题扰动相关逻辑

### 审核外顺手收正：FN 组合约束

- 状态：`已吸收`
- 来源：
  - 用户进一步澄清规则：应拒绝任意 `FN + 面键`，而不是仅拒绝“双 FN + 面键”
- 处理方式：
  - 在 [BindingConfig.cpp](/c:/Users/xuany/Documents/dualPad/src/input/BindingConfig.cpp) 把 `ContainsTwoFnWithFace()` 收正为“任意 `FN + Face` 拒绝”
  - 两份绑定文件注释同步改正

---

## B02 HID / 协议 / 状态归一化

### F1 USB 触摸数据与电池/状态偏移重叠

- 状态：`已吸收`
- 处理方式：
  - 在 [DualSenseUsbInputParser.cpp](/c:/Users/xuany/Documents/dualPad/src/input/protocol/DualSenseUsbInputParser.cpp) 中，先确定触摸布局
  - 当 legacy 触摸布局命中时，跳过对应的电池解析，避免把 legacy 触摸字节误读成电池状态

### F2 Legacy 触摸 fallback 不做 plausibility 检查

- 状态：`已吸收`
- 处理方式：
  - 在 [DualSenseUsbInputParser.cpp](/c:/Users/xuany/Documents/dualPad/src/input/protocol/DualSenseUsbInputParser.cpp) 中给 legacy fallback 也补上 `IsPlausibleTouchPoint()` 检查

### F3 硬件序列号被忽略，无法检测传输层丢包

- 状态：`已记录待后续`
- 处理方式：
  - 在 [DualSenseDevice.cpp](/c:/Users/xuany/Documents/dualPad/src/input/hid/DualSenseDevice.cpp) 当前软件 `sequence` 赋值处补了 TODO
  - 明确后续升级时再接入 USB byte 7 / BT31 byte 8 的硬件计数做 gap 观测
- 当前未做：
  - 未修改 `RawInputPacket`
  - 未修改 `PadState`
  - 未修改 snapshot 契约

### F4 `RawInputPacket.data` 是指向成员 buffer 的裸指针

- 状态：`已吸收`
- 处理方式：
  - 在 [DualSenseProtocolTypes.h](/c:/Users/xuany/Documents/dualPad/src/input/protocol/DualSenseProtocolTypes.h) 给 `RawInputPacket.data` 增加生命周期注释
  - 明确它只是借用 `DualSenseDevice` 内部读缓冲，不能跨下一次 `ReadPacket()` 持有

### F5 BT `0x01` 不解析 Edge 扩展键

- 状态：`已确认不改`
- 原因：
  - 当前审核已将其定性为 `ParseCompleteness::Partial` 的已知限制
  - 现阶段没有更强的外部协议或硬件验证依据，不适合拍脑袋扩展

---

## 相关澄清

### `context epoch` 逻辑

- 状态：`未改行为，仅澄清`
- 说明：
  - 当前 `ShouldAdvanceContextEpoch()` 的语义是“只有 ownership 真正跨域时才推进 epoch”
  - `Gameplay -> Sneaking` 这类 gameplay 域内子状态切换不会推进 epoch，避免误杀在途 `hold/toggle`
  - 当前 `Combat` 仍是枚举预留，并非 `DetectGameplayContext()` 已实际返回的 runtime 上下文

---

## 编译状态

以下吸收项落地后已重新编译通过：

- `xmake -y`

部署结果：

- `G:\\skyrim_mod_develop\\mods\\dualPad\\SKSE\\Plugins\\DualPad.dll`
- `G:\\g\\SkyrimSE\\dinput8.dll`

---

## 下一步建议

- 可以继续把第三方审核推进到 `B03`
- `B02` 中“硬件序列号 gap 检测”保持为后续升级点，不建议在当前阶段顺手扩改协议契约

