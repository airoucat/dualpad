# Gameplay 持续态数字动作与光标交接计划

本文只收两类当前已明确暴露、但不适合继续用临时补丁硬扛的问题：

1. `Sprint` 这类 **持续态数字动作** 的跨设备接管不自然
2. gameplay 中 **引擎平台表现 / HUD 图标表现 / 光标位置交接** 仍然耦合过深

这份文档的目标不是直接给出最终实现，而是基于：

- 当前项目真实代码链
- CommonLib 暴露的游戏类结构
- IDA MCP 反编译出的游戏实际函数路径

把“问题应修在哪一层、为什么不该修在别的层”先钉清。

---

## 1. 当前代码现状

### 1.1 `Sprint` 当前走的是真正的持续态链

项目内当前 `Sprint` 的代码事实：

- [../src/input/backend/NativeActionDescriptor.cpp](../src/input/backend/NativeActionDescriptor.cpp)
  - `Game.Sprint`
  - `ActionOutputContract::Hold`
  - `ActionLifecyclePolicy::HoldOwner`
- [../src/input/backend/ActionLifecycleCoordinator.cpp](../src/input/backend/ActionLifecycleCoordinator.cpp)
  - `Hold` 合同会生成 `Press / Hold / Release`
- [../src/input/backend/NativeButtonCommitBackend.cpp](../src/input/backend/NativeButtonCommitBackend.cpp)
  - `HoldOwner` 会翻译成 `PollCommitMode::Hold`
- [../src/input/backend/PollCommitCoordinator.cpp](../src/input/backend/PollCommitCoordinator.cpp)
  - `QueueHoldSet / QueueHoldClear`
  - `desiredHeld`
  - `HoldDownVisible`

这说明当前运行时并不是把 `Sprint` 当成脉冲动作，而是已经按“持续按住”去 materialize。

### 1.2 当前 `DigitalOwner` 仍是 family-level 仲裁

当前项目里，keyboard/mouse 对 gameplay digital 的事实收集，仍然在：

- [../src/input/InputModalityTracker.cpp](../src/input/InputModalityTracker.cpp)
  - `_gameplayKeyboardDigitalDownMask`
  - `_gameplayMouseDigitalDownMask`

而 synthetic gamepad digital 的“当前帧有意义动作”来自：

- [../src/input/injection/GameplayOwnershipCoordinator.cpp](../src/input/injection/GameplayOwnershipCoordinator.cpp)
  - `HasMeaningfulGamepadDigitalAction(framePlan)`

当前 `DigitalOwner` 的做法是：

- 如果 KBM digital 事实活跃，就把 `DigitalOwner` 切到 `KeyboardMouse`
- 否则如果当前帧有 meaningful gamepad digital，就切到 `Gamepad`
- 再由 [../src/input/backend/NativeButtonCommitBackend.cpp](../src/input/backend/NativeButtonCommitBackend.cpp)
  对 gameplay digital 做 family-level suppression

这对 `Jump / Activate` 这类脉冲动作还能成立，但对 `Sprint` 这种持续态不自然。

### 1.3 gameplay 表现当前仍部分绑在引擎 `IsUsingGamepad()`

当前 gameplay 下：

- [../src/input/InputModalityTracker.cpp](../src/input/InputModalityTracker.cpp)
  - `IsUsingGamepad()` 会看：
    - `GameplayOwnershipCoordinator::GetPublishedLookOwner()`
    - `_gameplayOwner`

也就是说，当前 gameplay 图标/平台表现仍然部分借引擎侧 `IsUsingGamepad` hook 输出。

这也是为什么：

- mouse-look 与 stick-look 冲突时，容易把引擎 presentation 一并带着抖
- gameplay 中切回鼠标后，图标/光标会出现“先闪到新位置，再跳回旧位置附近”的体验问题

---

## 2. 结合游戏实际链路的判断

### 2.1 菜单平台是单一结论，不适合多 owner 并存

IDA MCP 反编译到：

- `0x140ECD970`

对应函数会：

- 检查 movie 是否支持 `_root.SetPlatform`
- 调 `_root.SetPlatform`
- 再更新菜单平台相关状态

这说明 UI / menu presentation 最终只吃一个单一平台结论，这和 [gameplay_input_ownership_investigation_and_plan_zh.md](gameplay_input_ownership_investigation_and_plan_zh.md) 里“UI owner 状态机适合管 presentation/cursor/navigation”是吻合的。

### 2.2 gameplay 真正吃的是手柄 current-state，不是 UI owner

IDA MCP 反编译到：

- `0x140C1AB40`

这条函数会：

- 调 `XInputGetState`
- 逐个处理按钮位
- 归一化 trigger
- 归一化左右摇杆
- 再把 stick activity 提交给后续 producer

这说明 gameplay 真实关心的是：

- button current-state
- trigger current-state
- thumbstick current-state

而不是“当前 UI 觉得谁主导平台表现”。

### 2.3 `Sprint` 在游戏侧本来就是 held-state 语义

CommonLib 里：

- [../lib/commonlibsse-ng/include/RE/S/SprintHandler.h](../lib/commonlibsse-ng/include/RE/S/SprintHandler.h)
  - `SprintHandler : public HeldStateHandler`
- [../lib/commonlibsse-ng/include/RE/H/HeldStateHandler.h](../lib/commonlibsse-ng/include/RE/H/HeldStateHandler.h)
  - `heldStateActive`
  - `triggerReleaseEvent`

这说明游戏原生对 `Sprint` 的语义就是 held-state，而不是单次 owner pulse。

因此，当前项目把 `Sprint` 放进 `DigitalOwner` family-level 仲裁，只能勉强止血，不会是最终正确模型。

### 2.4 光标问题更像是位置交接，不是 owner 方向错误

CommonLib 暴露了：

- [../lib/commonlibsse-ng/include/RE/M/MenuCursor.h](../lib/commonlibsse-ng/include/RE/M/MenuCursor.h)
  - `cursorPosX`
  - `cursorPosY`
- [../lib/commonlibsse-ng/include/RE/G/GFxMovieView.h](../lib/commonlibsse-ng/include/RE/G/GFxMovieView.h)
  - `NotifyMouseState(...)`

IDA MCP 反编译到：

- `0x140ED2F90`

这条函数会在两条路径间切换：

1. 鼠标路径
   - `GetCursorPos`
   - 转前台窗口 client 坐标
2. 手柄光标路径
   - 用 `fGamepadCursorSpeed:Interface`
   - 基于内部保存的 cursor position 做积分

这说明游戏自己就维护着一套菜单光标状态。

如果 `CursorOwner` 只切 owner、不同步坐标，就很容易出现：

- 一帧先吃到真实鼠标当前位置
- 下一帧又被内部 `MenuCursor.cursorPosX/Y` 拉回旧位置附近

也就是当前实机看到的“闪一下再跳回去”。

---

## 3. 问题一：`Sprint` 不该继续走 `DigitalOwner`

### 3.1 当前问题本质

当前 `DigitalOwner` 模型假设：

- 同一 family 只应该有一个 owner 生效

但 `Sprint` 的真实语义更接近：

- 只要任一有效来源仍在要求 held，就应该继续 held

这和脉冲动作不同。

因此现在看到的现象：

- 手柄先冲刺，再按键盘，会中断
- 键盘先冲刺，再按手柄，松键盘后又停

本质上不是阈值不对，而是模型不对。

### 3.2 正式方案

把 gameplay digital 分成两类：

1. `PulseDigital`
   - `Jump`
   - `Activate`
   - 继续走 owner 仲裁，防双触发
2. `SustainedDigital`
   - `Sprint`
   - 以后其他 hold/repeat 再逐步纳入
   - 改成 source aggregation，不再问“谁是 owner”

### 3.3 建议落点

最自然的落点不是 UI tracker，而是当前已经掌管 hold materialization 的 poll commit 层：

- [../src/input/backend/PollCommitCoordinator.cpp](../src/input/backend/PollCommitCoordinator.cpp)
- [../src/input/backend/NativeButtonCommitBackend.cpp](../src/input/backend/NativeButtonCommitBackend.cpp)

因为这里已经有：

- `PollCommitMode::Hold`
- `desiredHeld`
- `QueueHoldSet / QueueHoldClear`
- token / state / release gap

真正缺的是：

- `desiredHeld` 目前只有一个布尔值
- 没有按 source 分开存

### 3.4 计划中的结构

先只针对 `Sprint` 做第一阶段：

- `heldByGamepad`
- `heldByKeyboard`
- `heldByMouse`

最终：

- `desiredHeld = heldByGamepad || heldByKeyboard || heldByMouse`

这样：

- 手柄先按、键盘后按，不会断
- 键盘先按、手柄后按，松键盘也不会断
- 不再需要依赖 `DigitalOwner` 的 family-level 切换

### 3.5 实施顺序

1. 把 `Sprint` 从 `DigitalOwner` 的正式治理范围里移出
2. 设计 `SustainedDigitalAggregator`
3. 先只接 `Game.Sprint`
4. 稳定后再评估是否扩到其他 `HoldOwner / RepeatOwner`

---

## 4. 问题二：gameplay 平台表现与光标交接需要分层

### 4.1 当前问题本质

这里其实混了两层：

1. gameplay 引擎侧平台表现
   - `IsUsingGamepad`
   - `GamepadControlsCursor`
2. DualPad 自己的 glyph / HUD / presentation 表现

当前它们还绑得太紧，所以：

- 为了保持 gameplay 手柄图标动态切换，会碰引擎 presentation
- 为了止住 mouse-look / stick-look 冲突，又不得不改 gameplay `IsUsingGamepad()` 路径

这不是长久方案。

### 4.2 正式方案

把 gameplay 里的表现再拆成两层：

1. **Engine gameplay presentation**
   - 尽量保守
   - 不用于驱动动态 glyph
   - 只在确实需要兼容游戏内部逻辑时才切换
2. **DualPad glyph presentation**
   - 用我们自己的 gameplay/presentation 事实驱动
   - 不再依赖引擎 `IsUsingGamepad()` 频繁来回切

也就是说：

- 引擎 presentation 为稳定服务
- DualPad glyph presentation 为动态图标服务

### 4.3 光标问题的正式方案

这条不该在 SWF 修，也不该继续靠 owner 阈值碰运气。

应该在 C++ 做明确的 `Cursor handoff`：

#### `Gamepad -> KeyboardMouse`

1. 读取真实鼠标当前位置
2. 转成当前前台 UI/client 坐标
3. 同步到 `MenuCursor.cursorPosX/Y`
4. 必要时对当前 `uiMovie` 调 `NotifyMouseState(...)`

#### `KeyboardMouse -> Gamepad`

1. 读取当前 `MenuCursor.cursorPosX/Y`
2. 作为 gamepad cursor 的起始位置
3. 避免手柄路径再次从旧缓存位置积分

### 4.4 建议落点

这条更适合继续挂在：

- [../src/input/InputModalityTracker.cpp](../src/input/InputModalityTracker.cpp)

但不是继续改 owner 规则，而是给：

- `SetCursorOwner(...)`

补一层真正的坐标同步。

这样：

- owner 决策继续在 tracker
- 坐标 handoff 在切换点完成
- SWF 不需要知道这件事

---

## 5. 两个问题的优先级

当前建议顺序：

1. 先把 ownership 主线继续按既定计划收完
2. 然后优先做 `Sprint` 的 `SustainedDigitalAggregator`
3. 再做 gameplay glyph presentation 与 engine presentation 解耦
4. 最后做 `Cursor handoff / position synchronization`

原因是：

- `Sprint` 是当前 gameplay 体验里最明显的逻辑错误
- 光标闪位是体验瑕疵，但已经能归类到明确的 handoff 问题，不必和 owner 主线再混改

---

## 6. 当前结论

### 已确认

1. `Sprint` 的正确模型不是 owner，而是 sustained source aggregation。
2. gameplay 的动态图标表现不应长期继续依赖引擎 `IsUsingGamepad()` 来回切换。
3. 光标闪一下再跳回旧位置，根因更像是 `CursorOwner` 切换时没有做位置同步。
4. 这两个问题都不该继续用“调阈值”解决。

### 后续实施原则

1. `PulseDigital` 和 `SustainedDigital` 正式分开。
2. gameplay engine presentation 和 glyph presentation 正式分开。
3. cursor 问题在 C++ handoff 修，不在 SWF 修。

