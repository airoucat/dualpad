# ControlMap Gamepad 事件清单

更新日期：2026-03-24

本文按 vanilla SSE 1.5.97 `controlmap.txt` 的上下文展开所有 gamepad 列不为 `0xff` 的原生事件。

唯一原则：

- 每个原生事件都保留独立身份。
- 即使最终 materialize 到同一个 gamepad 硬件位，也不能因为“默认按键相同”就合并成同一个事件。

例如：

- `Activate`
- `Accept`
- `Click`

它们都可能最终落在 `0x1000`，但仍然是不同的原生 user event。

## 常用 gamepad 位速记

- `0x0001` = D-pad Up
- `0x0002` = D-pad Down
- `0x0004` = D-pad Left
- `0x0008` = D-pad Right
- `0x0009` = LTrigger
- `0x000a` = RTrigger
- `0x000b` = Left Stick
- `0x000c` = Right Stick
- `0x0010` = Options / Start
- `0x0020` = Create / Back
- `0x0040` = L3
- `0x0080` = R3
- `0x0100` = L1 / LB
- `0x0200` = R1 / RB
- `0x1000` = Cross / A
- `0x2000` = Circle / B
- `0x4000` = Square / X
- `0x8000` = Triangle / Y

## Main Gameplay

- `Move -> 0x000b`
- `Look -> 0x000c`
- `Left Attack/Block -> 0x0009`
- `Right Attack/Block -> 0x000a`
- `Activate -> 0x1000`
- `Ready Weapon -> 0x4000`
- `Tween Menu -> 0x2000`
- `Toggle POV -> 0x0080`
- `Jump -> 0x8000`
- `Sprint -> 0x0100`
- `Shout -> 0x0200`
- `Sneak -> 0x0040`
- `Favorites -> 0x0001,0x0002`
- `Hotkey1 -> 0x0004`
- `Hotkey2 -> 0x0008`
- `Wait -> 0x0020`
- `Journal -> 0x0010`

无 gamepad 位但属 PC 独占原生事件：

- `Hotkey3-8`
- `QuickSave / QuickLoad`
- `Pause`
- `Screenshot / Multi-Screenshot`
- `Console`
- `CameraPath`
- `Quick Inventory / Quick Magic / Quick Stats / Quick Map`

## Menu Mode

- `Accept -> 0x1000`
- `Cancel -> 0x2000`
- `Up -> 0x0001`
- `Down -> 0x0002`
- `Left -> 0x0004`
- `Right -> 0x0008`
- `Left Stick -> 0x000b`
- `DownloadAll -> 0x8000`

## Console

- `PickPrevious -> 0x0002`
- `PickNext -> 0x0001`
- `NextFocus -> 0x0200`
- `PreviousFocus -> 0x0100`

## Item Menus

- `LeftEquip -> Left Attack/Block family`
- `RightEquip -> Right Attack/Block family`
- `Item Zoom -> 0x0080`
- `Rotate -> 0x000c`
- `XButton -> 0x4000`
- `YButton -> 0x8000`

## Inventory

- `ChargeItem -> Shout family / 0x0200`

## Favorites Menu

- `Up -> 0x0001`
- `Down -> 0x0002`
- `Accept -> 0x1000`
- `Cancel -> 0x2000`
- `Left Stick -> 0x000b`

## Map Menu

- `Cancel -> 0x2000`
- `Look -> 0x000c`
- `Zoom In -> 0x000a`
- `Zoom Out -> 0x0009`
- `Click -> 0x1000`
- `PlayerPosition -> 0x8000`
- `LocalMap -> 0x4000`
- `Journal -> 0x0004`
- `Cursor -> 0x000b`

注意：

- `MapLookMode` 与 `LocalMapMoveMode` 在原版 controlmap 的 gamepad 列是 `0xff`。
- 它们虽然是原生事件，但不是通过标准 gamepad 位直接表达。

## Stats

- `Rotate -> 0x000b`

## Cursor

- `Cursor -> 0x000c`
- `Click -> 0x1000`

## Book

- `PrevPage -> 0x0004`
- `NextPage -> 0x0008`

## Debug Overlay

- `NextFocus -> 0x0200`
- `PreviousFocus -> 0x0100`
- `Up -> 0x0001`
- `Down -> 0x0002`
- `Left -> 0x0004`
- `Right -> 0x0008`
- `ToggleMinimize -> 0x0020`
- `ToggleMove -> 0x0080`
- `LTrigger -> 0x0009`
- `RTrigger -> 0x000a`
- `B -> 0x2000`
- `Y -> 0x8000`
- `X -> 0x4000`

## Journal

- `XButton -> 0x4000`
- `YButton -> 0x8000`
- `TabSwitch -> 0x0009,0x000a`

## TFC Mode

- `CameraZUp -> 0x000a`
- `CameraZDown -> 0x0009`
- `WorldZUp -> 0x0200`
- `WorldZDown -> 0x0100`
- `LockToZPlane -> 0x4000`

## Debug Map-like Mode

- `Look -> 0x000c`
- `Zoom In -> 0x000a`
- `Zoom Out -> 0x0009`
- `Move -> 0x000b`

## Lockpicking

- `RotatePick -> 0x000b`
- `RotateLock -> 0x000c`
- `DebugMode -> 0x4000`
- `Cancel -> 0x2000`

## Favor

- `Cancel -> 0x2000`

## 当前项目使用规则

- 这份母表描述的是“原生事件身份”，不是“DualPad 当前全部已接入的 action surface”。
- 当前项目对原生事件的策略是：
  - 先保留事件身份。
  - 再决定是否将其接入当前 action 面。
  - 绝不因为默认是同一个硬件位，就把多个原生事件压成一个通用 `Menu.*` 名字。