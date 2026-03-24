# DualPad ControlMap Combo Profile

更新日期：2026-03-24

## 目的

这份文档说明 DualPad 当前为少量 keyboard-exclusive 原生事件维护的 gamepad `ControlMap` overlay。

原则很简单：

- 映射层负责：`物理手柄输入 -> 抽象 actionId`
- gamepad `ControlMap` overlay 负责：`抽象原生 actionId -> 固定 gamepad combo ABI`

也就是说，玩家绑定什么物理键位可以自由变化，但同一个原生 action 在同一份 DualPad profile 下必须 materialize 成稳定的固定组合。

## 文件位置

仓库源文件：

- `config/controlmap_profiles/DualPadNativeCombo/Interface/Controls/PC/controlmap.txt`

运行时部署文件：

- `Data/SKSE/Plugins/DualPadControlMap.txt`
- 开发环境当前路径：`G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPadControlMap.txt`

## 运行时策略

当前策略是：

1. 游戏先按原样加载 vanilla `ControlMap`。
2. DualPad 在 `kDataLoaded` 后读取 `DualPadControlMap.txt`。
3. 直接调用游戏自己的 parser / rebuild 链重载 gamepad `ControlMap`。
4. 故意跳过 `ControlMap_Custom.txt`。

这意味着：

- DualPad 接管的 gamepad ABI 不再以玩家 remap 或其它 controlmap mod 为兼容目标。
- 键盘 / 鼠标映射不在这次 overlay 的职责范围内。
- 当前行为符合项目“以默认 controlmap 或 DualPad 自维护 controlmap 为 ABI”的预期。

## 当前固定 combo ABI

### 已验证

- `Game.Pause -> Pause -> Circle + Triangle`
- `Game.NativeScreenshot -> Screenshot -> Circle + R1`

### 已接入、待补测

- `Game.Hotkey3 -> Hotkey3 -> L1 + Create`
- `Game.Hotkey4 -> Hotkey4 -> L1 + DpadUp`
- `Game.Hotkey5 -> Hotkey5 -> L1 + DpadLeft`
- `Game.Hotkey6 -> Hotkey6 -> L1 + DpadDown`
- `Game.Hotkey7 -> Hotkey7 -> L1 + DpadRight`
- `Game.Hotkey8 -> Hotkey8 -> L1 + R1`

## 当前 materialization 规则

- combo-native 动作当前采用“第一拍直接下完整组合”。
- 之前尝试过 staged prelude，但会破坏 `Pause / Screenshot` 这类首拍型 handler，因此当前不再使用。

## 当前不纳入这条 profile 的动作

- `Game.OpenInventory / Game.OpenMagic / Game.OpenSkills / Game.OpenMap`
  - 所属 `MenuOpenHandler` 家族在当前 mod 栈下不稳定，已撤出正式支持面。
- `Game.OpenFavorites`
  - 当前不纳入这份 combo-native profile。
- `Game.Console`
  - 当前不纳入。
- `Game.QuickSave / Game.QuickLoad`
  - 产品优先级较低，暂不推进。

## 维护规则

- 这份 overlay 必须和运行时代码中的 combo ABI 保持一致。
- 如果修改某个原生 action 的 combo，必须同时更新：
  - `NativeButtonCommitBackend`
  - `DualPadControlMap.txt` 源文件
  - README / routing 文档
- 默认绑定可以不占用这些 action；只要玩家手动把某个输入绑到这些 action 上，并且 overlay 已加载，它们就应能生效。