# Sprint 混合来源接管问题调查与计划

## 结论先说

当前 `Sprint` 的问题，已经不是：

- `PollCommitCoordinator` 丢了 held
- `NativeButtonCommitBackend` 没把 `Sprint` 写进 synthetic XInput
- `GameplayOwner` 还在粗暴来回切

而是：

**我们内部已经把 `Sprint` 维持住了，但游戏对“同一个持续动作跨设备接管”的语义，并不等价于内部的 held OR 聚合。**

也就是说，继续在 owner 阈值、lease、或普通 held 聚合上打补丁，已经不是正确方向。

---

## 1. 这次日志已经证明了什么

### 1.1 poll 层的 synthetic sprint 没掉

最新日志里，`Sprint` 在混合来源场景下持续保持：

- `actionDown=true`
- `state=HoldDownVisible`
- `gpContributor=true`
- `kbmContributor=true`

即使在键盘释放之后，只要手柄 contributor 还在，日志仍然显示：

- `effectiveHeld=true`
- `actionDown=true`
- `gpContributor=true`
- `kbmContributor=false`

这说明：

- `PollCommitCoordinator` 的 held contributor 机制已经在工作
- `NativeButtonCommitBackend` 没有把 `Sprint` 提前 release
- `AuthoritativePollState` 侧拿到的 `Sprint` down 事实也没丢

### 1.2 键盘单独 sprint 没有走 synthetic 路径

日志里也能看到：

- `kbmHeld=true`
- `gpContributor=false`
- `kbmContributor=false`
- `effectiveHeld=false`
- `actionDown=false`

这说明当前实现仍然遵守了“键盘单独 sprint 不启动 synthetic sprint”的规则。

所以现在用户看到的中断，不是“键盘把 synthetic sprint 又起了一份”，也不是“我们把 keyboard sprint 错当成 gamepad sprint 发出去了”。

### 1.3 因此，剩余问题一定在 poll 之后

当前这条链已经被日志基本钉死：

```text
Pad / KBM facts
  -> PollCommitCoordinator
  -> NativeButtonCommitBackend
  -> AuthoritativePollState
  -> XInputStateBridge
  -> UpstreamGamepadHook (Poll 内部 XInputGetState)
```

在这个链上，`Sprint` 处于 held 的事实已经持续存在。

所以剩余问题只能在：

- 游戏对 gamepad/keyboard 的后续消费语义
- 同一动作跨设备交接时的 handler 逻辑

---

## 2. 结合游戏实际，这说明什么

### 2.1 `BSWin32GamepadDevice::Poll` 还在持续提交 gamepad 按钮状态

IDA 里 `0x140C1AB40` 的 `BSWin32GamepadDevice::Poll` 明确做了这些事：

1. 保存上一帧 XInput state
2. 调 `XInputGetState`
3. 对每个按钮调用 `sub_140C190F0`
4. 对左右 trigger / stick 做归一化

而 `sub_140C190F0` 的关键语义是：

- 只要 `previousDown || currentDown` 为真，就会继续把该按钮活动写进输入队列

也就是说：

**如果 synthetic gamepad 的 `Sprint` 位一直保持 down，游戏的 gamepad poll 侧并没有“自然丢失”这颗键。**

### 2.2 `SprintHandler` 是 `HeldStateHandler`

CommonLib 头文件已经给出了一个很关键的事实：

- `/C:/Users/xuany/.codex/worktrees/237f/dualPad/lib/commonlibsse-ng/include/RE/S/SprintHandler.h`
  - `RE::SprintHandler : public RE::HeldStateHandler`
- `/C:/Users/xuany/.codex/worktrees/237f/dualPad/lib/commonlibsse-ng/include/RE/H/HeldStateHandler.h`
  - `heldStateActive`
  - `triggerReleaseEvent`

这说明游戏原生的 `Sprint` 本来就不是“抽象的全局 held 布尔”，而是带有 **handler 内部状态** 的持续动作。

这非常重要，因为它意味着：

- keyboard 的 `Sprint` press/release
- gamepad 的 `Sprint` current-state/button activity

虽然表面都叫 “Sprint”，但在游戏内部并不一定能被当成“同一个 held 事实”无缝合并。

---

## 3. 为什么当前 OR 聚合方向不够

我们现在的思路是：

- 记录 `heldByGamepad`
- 记录 `heldByKeyboardMouse`
- 最终 `effectiveHeld = OR(...)`

这个逻辑对“插件内部 held 状态”是成立的。

但从上面的日志和游戏侧语义看，问题在于：

**游戏内部更像是按“哪条设备路径在驱动这个 handler”来维持 sprint，而不是只看一个跨设备统一 OR 结果。**

这就会产生一个典型现象：

1. gamepad sprint 已经在 held
2. keyboard sprint press/release 参与进来
3. 游戏内部的 `SprintHandler` 被 keyboard 路径影响
4. 即使 synthetic gamepad sprint 继续 held，也未必能在 handler 语义上立刻“接回来”
5. 因为对 handler 来说，缺的是 **重新接管时的 edge / handoff**，不是 held 布尔本身

所以当前方案的问题不是：

- held 聚合完全没价值

而是：

- held 聚合只能描述“高层目标状态”
- **不能直接决定“该把哪条设备路径暴露给游戏”**

---

## 4. 更正式的方案：SingleEmitterHold

### 4.1 核心思想

对 `Sprint` 这种 native gameplay sustained action，不再采用：

- “多个来源一起直接对游戏生效”

而采用：

- “内部允许多个 contributor 并存”
- “对游戏只暴露一个 active emitter source”

也就是：

```text
contributors:
  gamepad = held / not held
  keyboard = held / not held
  mouse = held / not held

resolved desired intent:
  anyHeld = OR(contributors)

engine-facing emitter:
  activeSource = Gamepad | KeyboardMouse | None
```

### 4.2 为什么这比简单 OR 更对

这样做的关键价值是：

- 我们内部仍然保留“只要任一来源在 held，就想继续 sprint”的目标
- 但对游戏不再同时喂两条设备路径
- 真正切换来源时，可以显式做 handoff

这更符合 `HeldStateHandler` 这种“有内部状态机”的原生消费方式。

---

## 5. `Sprint` 的 handoff 规则

### 5.1 Gamepad -> KeyboardMouse

场景：

- 当前 synthetic gamepad sprint 正在驱动
- keyboard sprint 按下，希望键盘接管

计划：

1. 将 active emitter 切为 `KeyboardMouse`
2. 对 synthetic gamepad sprint 做一次干净 release
3. 不再同时继续向游戏暴露 synthetic gamepad sprint
4. 让原生 keyboard sprint 独占这条动作

目的：

- 避免两条路径同时驱动同一个 `HeldStateHandler`

### 5.2 KeyboardMouse -> Gamepad

场景：

- keyboard sprint 正在驱动
- gamepad sprint 其实仍在 held，希望手柄接回

计划：

1. 识别到 `activeSource` 将从 `KeyboardMouse` 切回 `Gamepad`
2. 不只是恢复 “gamepad contributor = true”
3. 而是明确给 synthetic gamepad sprint 做一次重新起始：
   - 必要的 release gap
   - 然后 fresh press/down
4. 让游戏侧 handler 获得真正的 handoff edge

目的：

- 不是继续“假装一直 held”
- 而是显式重新 arm gamepad 这条 sprint 路径

---

## 6. 这意味着代码层要怎么改

### 6.1 不再把 `Sprint` 当成普通 `HoldAggregate`

当前做法里，`Sprint` 的 held contributor 已经进了：

- `/C:/Users/xuany/.codex/worktrees/237f/dualPad/src/input/backend/PollCommitCoordinator.cpp`
- `/C:/Users/xuany/.codex/worktrees/237f/dualPad/src/input/backend/NativeButtonCommitBackend.cpp`

但接下来不能只是在这里继续叠更多 OR 特判。

### 6.2 要给 `Sprint` 单独引入 “active emitter source”

建议新增一个更明确的状态，最自然的位置仍然在 poll/backend 层：

- `SprintEmitterSource::None`
- `SprintEmitterSource::Gamepad`
- `SprintEmitterSource::KeyboardMouse`

并维护：

- `contributors`：谁还在 held
- `activeEmitter`：当前真正暴露给游戏的是谁
- `pendingHandoff`：是否需要重新建 edge

### 6.3 `DigitalOwner` 之后要重新分型

这次问题也说明，后续 digital 不能只粗分成：

- pulse
- hold

更合理的是：

- `PulseOwner`
  - `Jump`
  - `Activate`
- `ToggleOwner`
  - `Sneak`
- `SingleEmitterHold`
  - `Sprint`
  - 后面再评估哪些 native held action 也属于这类
- `SingleEmitterRepeat`
  - 菜单 repeat / 其它 repeat 类动作，后面再评估

也就是说：

**`Sprint` 的正式解法不是 “HoldAggregate”，而是 “SingleEmitterHold + contributor tracking + source handoff re-edge”。**

---

## 7. 推荐实施顺序

1. 先停止继续用 `DigitalOwner`/lease/阈值修 `Sprint`
2. 在 backend/poll 层给 `Sprint` 引入 `active emitter source`
3. 保留 contributor facts，但对游戏只暴露单一 source
4. 完成 `KeyboardMouse -> Gamepad` 的 re-edge handoff
5. 再实机验证三种情况：
   - 只键盘 sprint
   - 先手柄再键盘
   - 先键盘再手柄再松键盘
6. 稳定后，再评估 `Shout`、其它 repeat/hold action 是否要纳入同一模型

---

## 8. 当前不在这次实现里处理的

这次计划先不继续动：

- cursor handoff / 鼠标位置同步
- SWF 当前页 presentation refresh
- `missing press synthesis` 的 provenance-aware 重构

这些都已经另有文档记录，和 `Sprint` 这条应拆开推进。
