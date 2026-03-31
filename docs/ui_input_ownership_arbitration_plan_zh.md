# UI 输入所有权仲裁方案（结合项目现状与游戏实际）

本文只讨论：

- 键鼠 / 手柄在 UI 中抢输入、抢图标、抢平台显示的问题
- 不涉及注入层动作生命周期、SyntheticPadState、native controlmap 注入重构

目标：

- 让菜单 UI、动态图标、平台皮肤切换稳定
- 避免“轻微鼠标移动就把手柄 UI 抢走”
- 兼容未来更多页面与 `DualPad_GetActionGlyphToken` 动态图标链

---

## 1. 当前项目里的真实情况

当前运行时链路里，和这个问题直接相关的是：

- [InputModalityTracker.cpp](/C:/Users/xuany/.codex/worktrees/237f/dualPad/src/input/InputModalityTracker.cpp)
- [InputModalityTracker.h](/C:/Users/xuany/.codex/worktrees/237f/dualPad/src/input/InputModalityTracker.h)
- [ContextEventSink.cpp](/C:/Users/xuany/.codex/worktrees/237f/dualPad/src/input/ContextEventSink.cpp)
- [ScaleformGlyphBridge.cpp](/C:/Users/xuany/.codex/worktrees/237f/dualPad/src/input/glyph/ScaleformGlyphBridge.cpp)
- [KeyboardHelperBackend.cpp](/C:/Users/xuany/.codex/worktrees/237f/dualPad/src/input/backend/KeyboardHelperBackend.cpp)

当前 `InputModalityTracker` 做的事本质上是：

- 放开游戏原本对 KBM / Gamepad 同时输入的限制
- hook `IsUsingGamepad`
- hook `GamepadControlsCursor`
- hook `BSPCGamepadDeviceHandler::IsEnabled`
- 收输入事件，然后把当前平台模式切成：
  - `gamepad`
  - `keyboard/mouse`

当前额外补丁：

- 忽略 `KeyboardHelperBackend` 自己发出的 synthetic keyboard 事件
- 在菜单里，最近如果有 gamepad 输入，会给一个短暂 gamepad lease
- 最新补丁还把 `mouse move` 在菜单中的抢占性降低了

这条路可以缓解问题，但它本质还是：

**谁来事件，就切谁**

所以它的局限也很清楚：

- 它不是一个“所有权状态机”
- 它还是在做“事件驱动切换”
- 规则随着补丁越来越碎

---

## 2. 从游戏实际反编译看到的关键事实

### 2.1 菜单平台刷新最终是单一平台状态

IDA 里有一条非常关键的菜单链：

- `sub_140ECD970`

这个函数会：

1. 判断当前是否“使用 gamepad 平台”
2. 调菜单的 `_root.SetPlatform(...)`
3. 更新菜单自己的平台状态位

这说明对游戏菜单系统来说，最终需要的是一个**单一平台结论**：

- 当前按 gamepad 展示
- 或当前按 keyboard/mouse 展示

也就是说，不管我们内部怎么做仲裁，最终喂给游戏的仍然必须是：

- 一个统一的 presentation owner

这和我们当前 hook：

- `IsUsingGamepad`
- `GamepadControlsCursor`

的思路是一致的。

### 2.2 游戏确实会直接调用 `_root.SetPlatform`

IDA 里 `sub_140ECD970` 明确检查并调用：

- `_root.SetPlatform`

这进一步证明：

- 当前 `RefreshMenus()->menu->RefreshPlatform()` 这条方向是对的
- 菜单平台切换不是“某个局部按钮”问题，而是菜单级平台状态

### 2.3 游戏自己的 control name 表仍然是单套语义

IDA 里 `sub_140C17FB0` 是 control / user event 名字表构建函数，里面可以看到：

- `Accept`
- `Cancel`
- `Up`
- `Down`
- `Left`
- `Right`
- `Left Stick`
- `ToggleCursor`
- `XButton`
- `YButton`
- `PlayerPosition`
- `LocalMap`

这说明游戏本体在 UI 层并没有“多所有权并存”的概念。

它仍然是：

- 一套 control/user event 语义
- 再配一个当前平台表现

所以我们自己的仲裁层也应该输出单一 presentation 结果，而不是试图同时让两套 UI 平台都处于激活态。

### 2.4 菜单与 cursor 语义并不完全等价

IDA 里的字符串与调用链还暴露出几个信号：

- `ToggleCursor`
- `CursorMenu`
- `fGamepadCursorSpeed`
- `fMouseCursorSpeed`
- `fMapLocalGamepadPanSpeed`
- `fMapLocalMousePanSpeed`

这说明：

- 游戏里“是否手柄平台”
- “是否由鼠标/光标主导”

虽然相关，但不是完全同一件事。

因此我们内部设计时，不应该只保留一个粗糙的 `_usingGamepad` 布尔量。

---

## 3. 为什么当前方案不够优雅

当前问题的根因不是某一个补丁点，而是模型本身过于扁平：

- 一个 `_usingGamepad`
- 所有 UI 平台判断全靠它
- keyboard / mouse / mouse move / wheel / gamepad 都在抢同一个开关

这样的问题是：

1. `mouse move` 过强  
轻微移动鼠标就可能把手柄 UI 抢回去。

2. 菜单与 gameplay 混在一起  
UI 表现切换会被游戏内普通输入污染。

3. glyph / platform / cursor 没分层  
动态图标真正关心的是 presentation owner，不一定等于 gameplay owner。

4. map / cursor / inventory 3D 这类特殊菜单没有策略差异  
它们天然比主菜单更需要鼠标 / 光标，但当前模型里没有上下文策略。

---

## 4. 更完善的方案：输入所有权仲裁层

### 4.1 核心思路

把当前 `InputModalityTracker` 从：

- “输入设备模态切换器”

升级成：

- “UI 输入所有权仲裁层”

内部至少拆成 3 个概念：

1. `PresentationOwner`
2. `NavigationOwner`
3. `PointerIntent`

最终真正喂给游戏的，仍然是：

- 单一 `PresentationOwner`

但内部不再靠原始事件直接抢那一个布尔值。

### 4.2 三个内部状态

#### A. PresentationOwner

决定：

- `IsUsingGamepad`
- `GamepadControlsCursor`
- `menu->RefreshPlatform()`
- UI 图标和平台皮肤

候选值：

- `Gamepad`
- `KeyboardMouse`

#### B. NavigationOwner

决定当前菜单导航是谁在主导：

- 手柄方向键 / 摇杆
- 键盘方向键 / 回车 / ESC

这个状态用来防止：

- 菜单里刚用手柄导航，鼠标轻微移动就抢走

#### C. PointerIntent

描述当前是否真的有“鼠标意图”，而不是无意义噪声：

- `None`
- `HoverOnly`
- `PointerActive`

建议判定：

- `mouse move`：先记为 `HoverOnly`
- `mouse click / wheel`：升为 `PointerActive`
- 超过位移阈值并持续一定时间的 `mouse move`，也可升为 `PointerActive`

---

## 5. 建议的规则

### 5.1 菜单上下文

#### 强信号

以下输入应立即切到 `KeyboardMouse`：

- keyboard button
- char input
- mouse button
- mouse wheel

以下输入应立即切到 `Gamepad`：

- gamepad button
- gamepad dpad
- gamepad thumbstick navigation

#### 弱信号

以下输入不应直接抢所有权：

- `mouse move`

它只更新 `PointerIntent = HoverOnly`，除非满足额外条件：

- 当前没有 gamepad sticky lease
- 且累计位移超过阈值
- 且持续时间超过阈值

这样可以避免：

- 手柄导航时，鼠标轻微抖动抢 UI

### 5.2 上下文差异化

建议按 `InputContext` 做策略表，而不是全菜单一刀切。

#### 严格 gamepad-sticky 菜单

适合：

- `MainMenu`
- `DialogueMenu`
- `MessageBoxMenu`
- `TutorialMenu`
- `Sleep/Wait`

策略：

- `mouse move` 永远不抢
- 必须 `click / wheel / keyboard` 才切 KBM

#### 中性菜单

适合：

- `JournalMenu`
- `ContainerMenu`
- `BarterMenu`
- `BookMenu`
- `FavoritesMenu`

策略：

- `mouse move` 只有超过阈值才抢
- `click / wheel / keyboard` 立即抢

#### pointer-first 菜单

适合：

- `MapMenu`
- `CursorMenu`
- 某些 inventory 3D inspect / rotate 视图

策略：

- `mouse move` 阈值更低
- `wheel` / `click` 立即切 KBM
- 但 gamepad 导航后仍可重新抢回

---

## 6. 和当前项目的对接方式

### 6.1 `ContextEventSink`

继续用：

- [ContextEventSink.cpp](/C:/Users/xuany/.codex/worktrees/237f/dualPad/src/input/ContextEventSink.cpp)

职责：

- 菜单开关
- combat / gameplay 切换

新增用途：

- 进入菜单时初始化 `NavigationOwner`
- 离开菜单时清空 `PointerIntent`

### 6.2 `InputModalityTracker`

仍保留这个入口文件，但内部职责升级：

- 不再只是 `_usingGamepad`
- 增加：
  - `_presentationOwner`
  - `_navigationOwner`
  - `_pointerIntent`
  - `_mouseMoveAccumulator`
  - `_mouseMoveWindowExpiresAt`

然后：

- `IsUsingGamepadHook()` 返回 `_presentationOwner == Gamepad`
- `GamepadControlsCursor` 也只吃 `PresentationOwner`

### 6.3 `KeyboardHelperBackend`

继续保留：

- [KeyboardHelperBackend.cpp](/C:/Users/xuany/.codex/worktrees/237f/dualPad/src/input/backend/KeyboardHelperBackend.cpp)

但它的角色仍然只是：

- synthetic keyboard suppression

它不再负责“帮助决定谁拥有 UI”。

### 6.4 `ScaleformGlyphBridge`

动态图标显示真正应该跟：

- `PresentationOwner`

而不是 raw last input。

这意味着后面如果做更完整的 widget / sprite 方案：

- 也应该从这个 owner 读当前平台表现，而不是重复发明一套模态判断。

---

## 7. 推荐的实现顺序

### Phase 1：低风险替换当前逻辑

目标：

- 保留现有 hook 点
- 只把菜单里的 `mouse move` 从强信号降成弱信号
- 引入 `PresentationOwner` / `PointerIntent`

这一步应该就能明显缓解当前“抢输入”问题。

### Phase 2：上下文策略表

按 `InputContext` 增加：

- strict gamepad-sticky
- neutral
- pointer-first

这样 `MapMenu` 不会和 `MainMenu` 同一规则。

### Phase 3：拆出 Cursor policy

到这一步再决定是否把：

- `GamepadControlsCursor`
- cursor owner

完全从 `PresentationOwner` 里拆出来。

只有当 `MapMenu` / `CursorMenu` 实际测试证明需要更细控制时，再做这步。

---

## 8. 最终建议

最推荐的方向不是继续给当前 `_usingGamepad` 打补丁，而是：

**把它升级成“UI 输入所有权仲裁层”，最终仍输出单一平台结论，但内部按 owner / intent / context 做判定。**

这样做的好处：

- 和游戏实际菜单平台链一致
- 和我们当前 `RefreshPlatform` / glyph bridge 兼容
- 规则比“谁来事件就切谁”更稳定
- 能扩展到主菜单之外
- 也更适合后面继续做 widget / sprite 动态图标系统

---

## 9. 当前代码上的保守过渡规则

在完整重构前，当前最值得保留的临时经验是：

- synthetic keyboard 继续抑制
- 菜单里最近有 gamepad 输入时，给 gamepad sticky lease
- `mouse move` 不应直接抢回平台
- 只有 `click / wheel / keyboard` 这类更明确的 KBM 信号才立刻切回

这条规则可以作为最终仲裁层的过渡实现，而不是最终形态本身。
