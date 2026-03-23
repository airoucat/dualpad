# DualPad ControlMap Combo Profile

更新日期：2026-03-23

## 目的

这份文档说明 DualPad 当前为 PC 键盘独占原生事件维护的 gamepad `ControlMap` overlay。

原则很简单：

- 映射层负责：`物理手柄输入 -> 抽象 actionId`
- gamepad `ControlMap` overlay 负责：`抽象原生 actionId -> 固定 gamepad combo ABI`

也就是说，物理按键绑定可以按上下文自由变化，但同一个原生 action 在同一套 profile 下必须 materialize 成同一个固定组合。

## overlay 路径

仓库内源文件：

- `config/controlmap_profiles/DualPadNativeCombo/Interface/Controls/PC/controlmap.txt`

运行时部署路径：

- `Data/SKSE/Plugins/DualPadControlMap.txt`

开发环境部署文件：

- `G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins/DualPadControlMap.txt`

运行时策略是：

- 游戏先按原样加载 vanilla / custom `ControlMap`
- DualPad 在 `kDataLoaded` 后读取自己的 `DualPadControlMap.txt`
- 只重建 `RE::ControlMap` 里的 gamepad 映射数组
- 键盘、鼠标映射不改

这意味着：

- 玩家或其它 mod 的 gamepad remap 不再属于兼容目标
- `ControlMap_Custom.txt` 对 DualPad 接管的 gamepad 事件不再生效
- 这符合 DualPad 当前“以默认/自维护 controlmap 为 ABI”的设计预期

## 当前固定 combo ABI

- `Game.Pause -> Pause -> Circle + Triangle`
- `Game.NativeScreenshot -> Screenshot -> Circle + R1`
- `Game.Hotkey3 -> Hotkey3 -> L1 + Create`
- `Game.Hotkey4 -> Hotkey4 -> L1 + DpadUp`
- `Game.Hotkey5 -> Hotkey5 -> L1 + DpadLeft`
- `Game.Hotkey6 -> Hotkey6 -> L1 + DpadDown`
- `Game.Hotkey7 -> Hotkey7 -> L1 + DpadRight`
- `Game.Hotkey8 -> Hotkey8 -> L1 + R1`

## 当前验证状态

- `Pause`
  - 已实机验证可用。
- `NativeScreenshot`
  - 已实机验证可用。
- `Hotkey3-8`
  - 当前仍保留为可绑定候选，但还没有完成实机验证。

## 当前 materialization 规则

- combo-native 事件当前采用“第一拍直接下完整组合”。
- 之前尝试过 staged prelude（先下前导键、下一拍补齐组合），但会破坏 `Pause / Screenshot` 这类首拍型 handler，因此当前不再使用。

## 当前刻意不包含的事件

- `Game.OpenFavorites`
  - 当前不纳入这套 combo-native 正式支持面。
- `Game.OpenInventory / Game.OpenMagic / Game.OpenSkills / Game.OpenMap`
  - 当前已从正式支持面撤出。
  - 这四个动作所属的 `MenuOpenHandler` 家族在当前 mod 栈下不稳定，不再继续作为默认 combo-native 能力推进。
- `Console`
  - 这轮按现决策不纳入。
- `QuickSave / QuickLoad`
  - 有组合方案，但当前产品优先级不高，先不收进正式 profile。

## 维护规则

- 这份 overlay 源文件必须和代码里的 `NativeButtonCommitBackend::ToVirtualPadBit()` 保持一致。
- 如果修改某个原生 action 的 combo ABI，必须同时更新：
  - `NativeButtonCommitBackend`
  - 这份 `DualPadControlMap.txt` 源文件
  - README / routing 文档
- 默认绑定文件可以不占用这些 action；只要用户手动绑定到这些 action，并且运行时 overlay 存在，就应能工作。
