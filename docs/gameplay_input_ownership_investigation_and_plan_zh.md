# Gameplay 输入所有权调查与方案

本文只讨论一件事：

- 为什么 `InputModalityTracker` 的 UI owner 状态机不能直接扩展成 gameplay 全局 owner
- 基于当前代码与 IDA 反编译结果，gameplay 输入所有权应该放在哪一层实现

不重复展开：

- SWF 动态图标
- 菜单 `_root.SetPlatform` 细节
- 旧 `keyboard-native` 历史路线

---

## 1. 当前项目里的真实运行链

当前 gameplay 注入主线已经比较明确：

```text
HidReader
  -> PadState
  -> PadEventGenerator
  -> PadEventSnapshotDispatcher
  -> PadEventSnapshotProcessor
      -> SyntheticStateReducer
      -> BindingResolver / FrameActionPlanner
      -> NativeButtonCommitBackend
      -> AxisProjection / UnmanagedDigitalPublisher
      -> AuthoritativePollState
  -> UpstreamGamepadHook
      -> XInputStateBridge
      -> Skyrim Poll producer
```

直接相关代码：

- [HidReader.cpp](/C:/Users/xuany/.codex/worktrees/237f/dualPad/src/input/HidReader.cpp)
- [PadEventSnapshotDispatcher.cpp](/C:/Users/xuany/.codex/worktrees/237f/dualPad/src/input/injection/PadEventSnapshotDispatcher.cpp)
- [PadEventSnapshotProcessor.cpp](/C:/Users/xuany/.codex/worktrees/237f/dualPad/src/input/injection/PadEventSnapshotProcessor.cpp)
- [SyntheticStateReducer.cpp](/C:/Users/xuany/.codex/worktrees/237f/dualPad/src/input/injection/SyntheticStateReducer.cpp)
- [AuthoritativePollState.h](/C:/Users/xuany/.codex/worktrees/237f/dualPad/src/input/AuthoritativePollState.h)
- [NativeButtonCommitBackend.cpp](/C:/Users/xuany/.codex/worktrees/237f/dualPad/src/input/backend/NativeButtonCommitBackend.cpp)
- [InputFramePump.cpp](/C:/Users/xuany/.codex/worktrees/237f/dualPad/src/input/InputFramePump.cpp)

而 `InputModalityTracker` 当前控制的是：

- `IsUsingGamepad`
- `GamepadControlsCursor`
- `BSPCGamepadDeviceHandler::IsEnabled`
- `menu->RefreshPlatform()`

相关代码：

- [InputModalityTracker.cpp](/C:/Users/xuany/.codex/worktrees/237f/dualPad/src/input/InputModalityTracker.cpp)
- [InputModalityTracker.h](/C:/Users/xuany/.codex/worktrees/237f/dualPad/src/input/InputModalityTracker.h)

这说明当前项目里其实已经天然存在两条不同层级的链：

1. **UI / presentation 链**
2. **gameplay hardware materialization 链**

这两条链之前被同一个 `_usingGamepad` 布尔值混在一起，后来又被 owner 状态机过度统一，才会把 gameplay 也带坏。

---

## 2. 结合 IDA 看到的游戏实际

### 2.1 菜单平台最终只吃单一平台结论

IDA 里 `0x140ECD970` 这条函数会：

- 检查 movie 是否支持 `_root.SetPlatform`
- 调 `_root.SetPlatform`
- 再更新菜单自己的平台相关状态位

这点非常重要，因为它说明：

- 菜单最终只接受一个单一平台结论
- UI 层要的不是“多 owner 并存”
- 而是“最终这帧显示成 Gamepad 还是 KeyboardMouse”

也就是说，`InputModalityTracker` 的 owner 状态机适合用于：

- UI 平台表现
- 光标归属
- 菜单导航归属

但它的最终输出依然必须是单一 `PresentationOwner`。

### 2.2 gameplay 真实链是 `BSWin32GamepadDevice::Poll -> XInputGetState`

IDA 里 `0x140C1AB40` 这条函数会：

- 调 `XInputGetState`
- 遍历按钮位并调用按钮更新逻辑
- 归一化触发器
- 归一化左右摇杆
- 再把 stick 活动提交成后续 producer 可见的状态

这说明 gameplay 侧关心的是：

- 按钮 current-state
- 摇杆轴 current-state
- 扳机 current-state

而不是 UI 那种“当前平台是谁”。

换句话说：

- gameplay 真实入口是**硬件态 materialization**
- UI 真实入口是**presentation mode**

这就是两个不同的问题。

### 2.3 当前 jitter 根因为什么会出现

我们之前把 `GameplayOwner` 直接接进：

- `IsUsingGamepad`
- `GamepadControlsCursor`

本质上等于让 gameplay 的设备争夺结果反过来影响 UI/platform hook 结论。

而 gameplay 真正的冲突其实不是“平台表现”冲突，而是：

- mouse 正在提供 look
- synthetic gamepad 右摇杆也在提供 look

也就是**同一 gameplay 通道被双写**。

这不是 UI owner 能优雅解决的问题。

---

## 3. 为什么 UI owner 不能直接扩展成 gameplay owner

### 3.1 UI owner 解决的是“谁主导显示与导航”

`InputModalityTracker` 现在更适合解决：

- 菜单图标显示跟谁走
- `RefreshPlatform()` 什么时候触发
- 鼠标轻微移动要不要抢回菜单
- 地图/日志/主菜单这类页面的 owner 策略

也就是：

- `PresentationOwner`
- `NavigationOwner`
- `CursorOwner`
- `PointerIntent`

### 3.2 gameplay 需要的是“每个输入通道谁有写权限”

gameplay 里真正冲突的是：

- `lookX / lookY`
- `moveX / moveY`
- `leftTrigger / rightTrigger`
- 部分数字按钮组

这不是一个总 owner 能稳妥表达的。

如果仍然试图用一个整体 `GameplayOwner` 解决，结果会变成：

- 鼠标动一下，整套 owner 切走
- 摇杆动一下，整套 owner 又切回
- 但实际冲突可能只发生在 `Look` 这一条轴

这就是之前“鼠标 + 摇杆一起看视角乱晃”的根因。

### 3.3 因此 gameplay 必须做 per-channel ownership

更合适的 gameplay 所有权应该至少拆成：

- `LookOwner`
- `MoveOwner`
- `CombatOwner`
- `DigitalOwner`

其中优先级最高的是：

- `LookOwner`

因为当前实际最明显的问题就是 mouse look 和 gamepad look 同时写。

---

## 4. gameplay 所有权应该放在哪

### 4.1 不应该继续塞进 `InputModalityTracker`

原因很简单：

- `InputModalityTracker` 的 hook 目标是 UI / cursor / menu platform
- 它离 gameplay 最终提交点太远
- 它能看见原始 mouse/gamepad 事件，但不能直接控制最终哪条 analog/digital 真正写进游戏

继续往这里堆 gameplay 规则，只会再次让 UI 和 gameplay 互相污染。

### 4.2 最合适的位置是 `PadEventSnapshotProcessor` 之后、`AuthoritativePollState` 之前

当前最好的落点是：

- [PadEventSnapshotProcessor.cpp](/C:/Users/xuany/.codex/worktrees/237f/dualPad/src/input/injection/PadEventSnapshotProcessor.cpp)

因为这里刚好同时拥有：

- 一帧完整 `SyntheticPadFrame`
- 解析后的 `FrameActionPlan`
- 当前 `InputContext`
- 最终要发布到 `AuthoritativePollState` 的 analog/digital 数据

这意味着 gameplay owner 可以在这里做真正有效的 gating：

- 允许哪些 analog 通道写入
- 屏蔽哪些 analog 通道
- 允许哪些 digital action materialize
- 保证 owner 只影响 gameplay 注入，不碰 UI 平台 hook

### 4.3 推荐新组件

建议新增：

- `src/input/injection/GameplayOwnershipCoordinator.h`
- `src/input/injection/GameplayOwnershipCoordinator.cpp`

它不替代 `InputModalityTracker`，而是消费：

- `InputModalityTracker` 提供的“最近设备事实”
- `SyntheticPadFrame`
- `InputContext`

然后输出：

- `OwnedAnalogState`
- `OwnedDigitalMask`

---

## 5. 推荐的 gameplay owner 结构

### Phase A：最小落地

先只做：

- `LookOwner`

状态：

- `Mouse`
- `Gamepad`

规则：

- mouse move 进入 gameplay 时，提升 `LookOwner = Mouse`
- 右摇杆超过阈值时，提升 `LookOwner = Gamepad`
- sticky 时间窗口内保持 owner，不频繁抖动
- 最终在 `PublishAnalogState(...)` 前，对 `lookX / lookY` 做 gating

这样可以直接解决当前最明显的问题：

- 鼠标看视角和摇杆看视角同时写入

### Phase B：扩展 analog owner

再加：

- `MoveOwner`
- `CombatOwner`

其中：

- `MoveOwner` 控 `moveX / moveY`
- `CombatOwner` 控 `LT / RT` 以及必要时关联动作

这一步主要是为未来避免：

- 键盘 WASD 和左摇杆混写
- 鼠标/键盘战斗和 synthetic trigger 冲突

### Phase C：数字按钮 owner

最后再看是否需要：

- `DigitalOwner`

它不一定要是单一 owner，也可以是按 action family 分组：

- `CombatDigitalOwner`
- `UtilityDigitalOwner`
- `MenuLikeDigitalOwner`

这一层只有在实机发现数字动作也会明显互相抢写时再推进。

---

## 6. `InputModalityTracker` 后续应该保留什么

`InputModalityTracker` 仍然有价值，但应明确缩成：

### 保留

- `PresentationOwner`
- `NavigationOwner`
- `CursorOwner`
- `PointerIntent`
- 菜单上下文 owner 策略
- `GameplayOwner` 作为“最近设备事实”
- 进菜单时从 gameplay 继承最近设备

### 不再做

- 直接决定 gameplay look/move/trigger 是否提交
- 用 hook 返回值直接表达 gameplay 通道 owner

也就是说：

- `GameplayOwner` 继续保留
- 但它是**菜单入口继承源**
- 不是 gameplay 最终仲裁器

---

## 7. 这轮调查后的结论

### 已确认

1. UI 平台最终只吃单一 `SetPlatform` 结论。
2. gameplay 实际入口是 `BSWin32GamepadDevice::Poll -> XInputGetState -> button/stick/trigger current-state`。
3. 当前 jitter 是 gameplay 通道双写，不是 UI 表现切换本身的问题。
4. 因此 UI owner 状态机不能直接扩展成 gameplay 全局 owner。

### 推荐方案

1. `InputModalityTracker` 继续只负责 UI / menu owner。
2. gameplay 另做 `GameplayOwnershipCoordinator`。
3. gameplay owner 放在 `PadEventSnapshotProcessor` 之后、`AuthoritativePollState` 之前。
4. 先只实现 `LookOwner`，把最明显的 mouse + right-stick 冲突收掉。

---

## 8. 下一步实施顺序

1. 先保留当前已经稳定的修正：
   - gameplay 不再让 `GameplayOwner` 直接驱动 `IsUsingGamepad / GamepadControlsCursor`
2. 新增 `GameplayOwnershipCoordinator` 设计骨架
3. 第一阶段只接 `LookOwner`
4. 在 `PadEventSnapshotProcessor::FinishFramePlanning(...)` 前做 analog gating
5. 稳定后再扩到 `MoveOwner / CombatOwner`

### 当前实现状态

- `LookOwner`：已接入 `PadEventSnapshotProcessor -> GameplayOwnershipCoordinator -> PublishAnalogState(...)`
- `MoveOwner`：已接入，当前使用
  - 当前帧 `leftStick`
  - gameplay keyboard move 通道事实（基于 `ControlMap::GetMappedKey(Forward/Back/Strafe Left/Strafe Right)`）
- `CombatOwner`：已接入，当前使用
  - 当前帧 triggers
  - gameplay keyboard/mouse combat 通道事实（基于 `ControlMap::GetMappedKey(Left Attack/Block, Right Attack/Block)`）

也就是说，当前代码已经从 `Phase A` 推进到 `Phase B` 的最小可用版，但还没有进入 `DigitalOwner`。

这样能最大程度减少回归面，也最符合当前代码和游戏实际链路。

---

## 9. 已记录但延后的问题

### 9.1 缺失 press 合成在跨上下文 coalesce 时会误重放旧键

当前 `PadEventSnapshotProcessor` 里有一条“缺失 press 合成”逻辑：

- 某一帧 `SyntheticStateReducer` 看出某个键是新 down
- `pressedMask` 里有它
- 但 `snapshot.events` 里没有对应 `ButtonPress`
- processor 会现场补一个 `PadEventType::ButtonPress`
- 再按正常路径走 `BindingResolver -> FrameActionPlanner -> ActionDispatcher`

这个机制本身是合理的，因为它能修“上游 edge 丢了，但这次按键仍然真实发生了”的情况。

问题出在：

- `PadEventSnapshotDispatcher` 发生 coalesce 且伴随 context mismatch 时
- 旧上下文里的物理按键可能被合并到新上下文那一帧
- 如果这时仍然按“缺失 press 合成”补边，就会把旧键在新上下文里重放一遍

目前实机已经观察到的副作用是：

- `Options / Pause / Journal` 这类打开菜单的键
- 在菜单刚打开时又被补出一次 press
- 结果表现成菜单一闪而过、打开后立刻关闭

### 9.2 当前临时止血做法

当前代码里的临时处理是：

- 在菜单上下文里禁用“缺失 press 合成”

它的优点是：

- 低风险
- 立即止住了“打开菜单后又被自己关掉”的问题

但它不是最终解，因为：

- 真正的菜单内 edge 丢失也会被一并禁掉
- 它修的是场景，不是根因

### 9.3 更优雅的正式修法

等本轮 gameplay ownership 主计划落完后，这里应替换成 provenance-aware 的合成规则：

1. 在 [PadEventSnapshot.h](/C:/Users/xuany/.codex/worktrees/237f/dualPad/src/input/injection/PadEventSnapshot.h) 里补充 coalesce 来源元数据
   - 例如 `crossContextCoalesced`
   - 或 `firstContext / firstContextEpoch`
2. 在 [PadEventSnapshotDispatcher.cpp](/C:/Users/xuany/.codex/worktrees/237f/dualPad/src/input/injection/PadEventSnapshotDispatcher.cpp) 的 `CoalescePendingLocked()` 中保留这些 provenance 信息
3. 在 [PadEventSnapshotProcessor.cpp](/C:/Users/xuany/.codex/worktrees/237f/dualPad/src/input/injection/PadEventSnapshotProcessor.cpp) 中把“缺失 press 合成”改成：
   - 仅允许在**同上下文连续 snapshot** 中发生
   - 对跨上下文 coalesce 的 snapshot 禁止补边

这样可以同时满足：

- 真正丢边时仍能恢复 `ButtonPress`
- 不会把旧上下文里的打开菜单键在新菜单里重放

这项工作已经记录为 **主计划之后再收的技术债**，当前不继续扩大改动面。

## 10. 当前已实现状态（2026-03-30）

本轮已落地的 gameplay ownership 范围如下：

- `LookOwner`
  - 在 [GameplayOwnershipCoordinator.cpp](/C:/Users/xuany/.codex/worktrees/237f/dualPad/src/input/injection/GameplayOwnershipCoordinator.cpp) 中按 `mouse look` 与 `right-stick look` 做通道级 owner 仲裁
  - 最终在 `PublishAnalogState(...)` 之前只对 `lookX / lookY` 做 gating
- `MoveOwner`
  - 依据 [InputModalityTracker.cpp](/C:/Users/xuany/.codex/worktrees/237f/dualPad/src/input/InputModalityTracker.cpp) 中按 controlmap 读取到的 `Forward / Back / Strafe Left / Strafe Right` 键盘事实
  - 在 `GameplayOwnershipCoordinator` 中只对 `moveX / moveY` 做 gating
- `CombatOwner`
  - 依据键盘/鼠标战斗键事实
  - 当前只对 synthetic trigger 轴做 gating
- `DigitalOwner`（Phase A）
  - 当前只覆盖现有 `gateAware` gameplay digital family
  - 也就是 [FrameActionPlanner.cpp](/C:/Users/xuany/.codex/worktrees/237f/dualPad/src/input/backend/FrameActionPlanner.cpp) 和 [ActionLifecycleCoordinator.cpp](/C:/Users/xuany/.codex/worktrees/237f/dualPad/src/input/backend/ActionLifecycleCoordinator.cpp) 里已经标成 `gateAware` 的动作
  - 目前实际范围是：`Game.Jump / Game.Activate / Game.Sprint`
  - `InputModalityTracker` 会记录这些动作对应的 KBM down 事实
  - [GameplayOwnershipCoordinator.cpp](/C:/Users/xuany/.codex/worktrees/237f/dualPad/src/input/injection/GameplayOwnershipCoordinator.cpp) 会发布 `DigitalOwner`
  - [NativeButtonCommitBackend.cpp](/C:/Users/xuany/.codex/worktrees/237f/dualPad/src/input/backend/NativeButtonCommitBackend.cpp) 会在 `DigitalOwner = KeyboardMouse` 时抑制新的 synthetic gameplay digital 提交
  - 当 `DigitalOwner` 从 `Gamepad` 切到 `KeyboardMouse` 时，会通过 [PollCommitCoordinator.cpp](/C:/Users/xuany/.codex/worktrees/237f/dualPad/src/input/backend/PollCommitCoordinator.cpp) 对现有 `gateAware` gameplay slot 做 `ForceCancel`

当前明确还未完成的部分：

- `CombatOwner` 还没有仲裁 KBM combat 的完整数字通道，只先处理了 synthetic trigger
- `DigitalOwner` 还没有扩到更宽的 gameplay digital family
- `DigitalOwner` 也还没有细分成多个 digital family
- `MoveOwner / CombatOwner / DigitalOwner` 的 sticky / hysteresis 还未细化成最终手感版本
