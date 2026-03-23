# ControlMap Gamepad 事件清单

更新时间：2026-03-22

## 目的

这份清单直接按 `G:/skyrim_mod/bsa-interface/interface/controls/pc/controlmap.txt` 的上下文分类，
列出所有 **gamepad 列不为 `0xFF`** 的原生事件。

这里的原则只有一条：

- **每个 controlmap 原生事件都保留独立身份**
- **即使默认映射到同一个 gamepad 按钮，也不能因为“物理位相同”而合并成同一个事件**

例如：

- `Activate`
- `Accept`
- `Click`

它们都可能 materialize 到 `0x1000`，但仍然是三个不同的原生事件。

## 常见 gamepad 位速记

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

- `LeftEquip -> !0,Left Attack/Block`
- `RightEquip -> !0,Right Attack/Block`
- `Item Zoom -> 0x0080`
- `Rotate -> 0x000c`
- `XButton -> 0x4000`
- `YButton -> 0x8000`

## Inventory

- `ChargeItem -> !0,Shout`

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
- `Cursor -> 0x000b`
- `PlayerPosition -> 0x8000`
- `LocalMap -> 0x4000`
- `Journal -> 0x0004`

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

## Debug Map Menu-like Mode

- `Look -> 0x000c`
- `Zoom In -> 0x000a`
- `Zoom Out -> 0x0009`
- `Move -> 0x000b`

## Lockpicking

- `RotatePick -> 0x000b`
- `RotateLock -> 0x000c`
- `DebugMode -> 0x4000`
- `Cancel -> 0x2000`

## Creations Menu

- `Accept -> 0x1000`
- `Cancel -> 0x2000`
- `Up -> 0x0001`
- `Down -> 0x0002`
- `Left -> 0x0004`
- `Right -> 0x0008`
- `Options -> 0x0010`
- `Left Stick -> 0x000b`
- `LoadOrderAndDelete -> 0x8000`
- `CategorySideBar -> 0x0009`
- `LikeUnlike -> 0x0200`
- `SearchEdit -> 0x0100`
- `Filter -> 0x000a`
- `PurchaseCredits -> 0x4000`

## Favor

- `Cancel -> 0x2000`

## 当前代码收口原则

后续 action surface 与默认绑定应按下面的规则继续推进：

- 先按 `controlmap` 上下文抽事件名
- 再决定哪些事件进入项目 action surface
- 即使多个事件最终 materialize 到同一个按钮位，也保留独立 action/native code 身份
- 不再因为“默认按钮一样”就把它们压扁成统一的 `Menu.*` 或其它 generic helper 名

## 当前已经按独立身份抽出的代表项

- `Book.Close / Book.PreviousPage / Book.NextPage`
- `Map.Click / Map.OpenJournal / Map.PlayerPosition / Map.LocalMap`
- `Dialogue.PreviousOption / Dialogue.NextOption`
- `Favorites.PreviousItem / Favorites.NextItem`
- `Console.Execute / Console.HistoryUp / Console.HistoryDown`

## 当前仍未完全展开的原生事件

下面这些已经在 `controlmap` 中确认有独立 gamepad 事件，但当前项目还没有全部展开成正式 action surface：

- `Ready Weapon`
- `Tween Menu`
- `Favorites`
- `Hotkey1 / Hotkey2`
- `DownloadAll`
- `PickPrevious / PickNext`
- `NextFocus / PreviousFocus`
- `LeftEquip / RightEquip`
- `Item Zoom`
- `Rotate`
- `XButton / YButton`
- `ChargeItem`
- `Zoom In / Zoom Out`
- `Cursor`
- `TabSwitch`
- `CameraZUp / CameraZDown`
- `WorldZUp / WorldZDown`
- `LockToZPlane`
- `RotatePick / RotateLock`
- `LoadOrderAndDelete`
- `CategorySideBar`
- `LikeUnlike`
- `SearchEdit`
- `Filter`
- `PurchaseCredits`

它们后续如果接入，也应该继续按“上下文专属原生事件”建模，而不是并回 generic `Menu.*`。
