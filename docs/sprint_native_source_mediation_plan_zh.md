# Sprint 原生来源中介计划

## 结论先说

当前 `Sprint` 的问题，已经基本可以排除为：

- `PollCommitCoordinator` 丢了 held
- `NativeButtonCommitBackend` 没把 synthetic sprint 写进 XInput poll
- `GameplayOwner` 仍在粗暴来回切

更接近真实根因的是：

- 我们自己的 synthetic gamepad sprint 这条链已经能持续保持 `down`
- 但 **原生键盘 Sprint 仍在绕过当前 owner / contributor 模型直接驱动游戏**
- Skyrim 侧 `SprintHandler` 属于 `HeldStateHandler` 家族，不是一个“跨设备随便 OR 一下 held”就能自然接管的动作

所以后续正式方案不该再继续堆：

- owner 阈值
- lease
- held OR 小补丁

而应该改成：

- `Sprint` 走 **SingleEmitterHold**
- 再加一层 **native keyboard mediation**

---

## 1. 现状复盘

### 1.1 插件内部这条链已经基本成立

从当前代码和日志看，下面这条链路已经能把 synthetic gamepad sprint 保持住：

- [C:\Users\xuany\.codex\worktrees\237f\dualPad\src\input\backend\PollCommitCoordinator.cpp](C:\Users\xuany\.codex\worktrees\237f\dualPad\src\input\backend\PollCommitCoordinator.cpp)
- [C:\Users\xuany\.codex\worktrees\237f\dualPad\src\input\backend\NativeButtonCommitBackend.cpp](C:\Users\xuany\.codex\worktrees\237f\dualPad\src\input\backend\NativeButtonCommitBackend.cpp)
- [C:\Users\xuany\.codex\worktrees\237f\dualPad\src\input\injection\UpstreamGamepadHook.cpp](C:\Users\xuany\.codex\worktrees\237f\dualPad\src\input\injection\UpstreamGamepadHook.cpp)

日志里在混合输入场景下仍能看到：

- `gpContributor=true`
- `effectiveHeld=true`
- `actionDown=true`
- `state=HoldDownVisible`

这说明：

- synthetic gamepad sprint 的 held 没有在插件内部丢失
- poll 层也没有提前 release

### 1.2 当前真正绕不过去的是 native keyboard Sprint

我们现在记录了键盘/鼠标上的 sprint 事实：

- [C:\Users\xuany\.codex\worktrees\237f\dualPad\src\input\InputModalityTracker.cpp](C:\Users\xuany\.codex\worktrees\237f\dualPad\src\input\InputModalityTracker.cpp)

也把这些事实同步进了 `Sprint` slot contributor：

- [C:\Users\xuany\.codex\worktrees\237f\dualPad\src\input\backend\NativeButtonCommitBackend.cpp](C:\Users\xuany\.codex\worktrees\237f\dualPad\src\input\backend\NativeButtonCommitBackend.cpp)

但这层同步并不会阻止：

- 原生键盘 `Sprint` 事件继续直接喂给游戏

当前代码里也没有现成机制能做到“只拦住 Sprint 的原生键盘事件”：

- [C:\Users\xuany\.codex\worktrees\237f\dualPad\src\input\injection\SourceBlockCoordinator.cpp](C:\Users\xuany\.codex\worktrees\237f\dualPad\src\input\injection\SourceBlockCoordinator.cpp)
  - 只阻止我们自己的 pad snapshot / synthetic source
- [C:\Users\xuany\.codex\worktrees\237f\dualPad\src\input\backend\KeyboardHelperBackend.cpp](C:\Users\xuany\.codex\worktrees\237f\dualPad\src\input\backend\KeyboardHelperBackend.cpp)
  - 目前只面向 helper key pool，不面向 gameplay 原生键位
- [C:\Users\xuany\.codex\worktrees\237f\dualPad\src\input\injection\SeInputEventQueueAccess.h](C:\Users\xuany\.codex\worktrees\237f\dualPad\src\input\injection\SeInputEventQueueAccess.h)
  - 说明我们有潜在的 queue-level 入口，但还没落实现

---

## 2. 结合游戏实际，为什么 `Sprint` 不能继续按普通 Hold 做

### 2.1 游戏侧 poll 本身没问题

结合 IDA 里之前已经核过的函数：

- `BSWin32GamepadDevice::Poll` `0x140C1AB40`
- 按钮 current-state 分发 `0x140C190F0`

只要 synthetic gamepad 的 `Sprint` 位仍然保持 `down`，游戏的 gamepad poll 链就还会把这颗键当作活跃输入处理。

所以问题不在：

- synthetic XInput current-state 没交到游戏

### 2.2 问题在 `SprintHandler` 是 handler-backed held action

CommonLib 头文件已经表明：

- [C:\Users\xuany\.codex\worktrees\237f\dualPad\lib\commonlibsse-ng\include\RE\S\SprintHandler.h](C:\Users\xuany\.codex\worktrees\237f\dualPad\lib\commonlibsse-ng\include\RE\S\SprintHandler.h)
- [C:\Users\xuany\.codex\worktrees\237f\dualPad\lib\commonlibsse-ng\include\RE\H\HeldStateHandler.h](C:\Users\xuany\.codex\worktrees\237f\dualPad\lib\commonlibsse-ng\include\RE\H\HeldStateHandler.h)

也就是说，对游戏来说：

- keyboard sprint
- gamepad sprint

虽然高层语义都叫 `Sprint`，但内部并不等价于一个“统一 held 真值”。

更像是：

- **谁在驱动 handler**
- **什么时候 release**
- **什么时候重新 arm / re-edge**

这些都很关键。

---

## 3. 正式方案：SingleEmitterHold

### 3.1 内部仍然保留多来源 contributor

内部继续维护：

- `heldByGamepad`
- `heldByKeyboardMouse`

这部分仍然有价值，因为它表达的是“意图”。

### 3.2 但对游戏只暴露一个 active emitter

新增正式概念：

```cpp
enum class SprintEmitterSource : std::uint8_t
{
    None,
    Gamepad,
    KeyboardMouse
};
```

运行时同时维护：

- `contributors`
- `activeEmitter`
- `pendingHandoff`

也就是：

- 内部允许多来源并存
- 对引擎永远只允许一个 source 真正驱动 `Sprint`

### 3.3 这和简单 OR 的区别

简单 OR 是：

- 只要任一来源 held，就继续让 synthetic sprint 维持住

SingleEmitterHold 是：

- 只要任一来源 held，就说明“还想继续 sprint”
- 但 **究竟由哪条设备路径驱动游戏**，必须单独决策

这才匹配 `HeldStateHandler` 语义。

---

## 4. 正式 handoff 规则

### 4.1 Gamepad -> KeyboardMouse

场景：

- 当前 gamepad emitter 正在驱动 sprint
- keyboard sprint 按下

正式规则：

1. keyboard 成为 `pending takeover`
2. 只要 keyboard 还 held，就把 `activeEmitter` 切到 `KeyboardMouse`
3. 对 synthetic gamepad sprint 做一次干净 release
4. 之后不再继续让 synthetic gamepad 路径维持 sprint

重点：

- 不是让 keyboard 和 gamepad 同时驱动
- 而是显式完成发射源切换

### 4.2 KeyboardMouse -> Gamepad

场景：

- keyboard emitter 正在驱动 sprint
- gamepad contributor 仍 held
- keyboard 释放

正式规则：

1. 切换 `activeEmitter` 到 `Gamepad`
2. 不只是恢复“held=true”
3. 而是对 synthetic gamepad sprint 做一次 **handoff re-edge**
   - 必要时先 release gap
   - 再 fresh down

目的：

- 让 `SprintHandler` 看到“重新接管”的明确边沿
- 而不是插件自己假设只要 held 继续为真就自然恢复

---

## 5. 还缺的一层：native keyboard mediation

只有 `SingleEmitterHold` 还不够。

因为现在最根本的问题是：

- 原生键盘 Sprint 仍然在插件仲裁之外直接进入游戏

所以正式方案必须补 **native keyboard mediation**。

### 5.1 最有希望的落点

#### 方案 A：Sprint-only queue mediation

落点：

- [C:\Users\xuany\.codex\worktrees\237f\dualPad\src\input\injection\SeInputEventQueueAccess.h](C:\Users\xuany\.codex\worktrees\237f\dualPad\src\input\injection\SeInputEventQueueAccess.h)

方向：

- 在进入 `BSInputEventQueue` 后，对 keyboard `Sprint` 的 `ButtonEvent` 做定向过滤/放行
- 只处理中途 handoff 这一个动作，不扩大到其它键

优点：

- 最贴近游戏原生输入路径
- 不需要把 gameplay 原生键位塞进 helper backend

风险：

- 需要精确识别 `Sprint` 对应 scancode / user event
- 要确保不误伤其它 keyboard 输入

#### 方案 B：Sprint-only keyboard replay

落点：

- [C:\Users\xuany\.codex\worktrees\237f\dualPad\src\input\backend\KeyboardHelperBackend.cpp](C:\Users\xuany\.codex\worktrees\237f\dualPad\src\input\backend\KeyboardHelperBackend.cpp)
- [C:\Users\xuany\.codex\worktrees\237f\dualPad\src\input\backend\KeyboardNativeBridge.cpp](C:\Users\xuany\.codex\worktrees\237f\dualPad\src\input\backend\KeyboardNativeBridge.cpp)

方向：

- 不再依赖用户真实键盘的 sprint 输入直接进游戏
- 插件在 handoff 时，自己重放对应 gameplay sprint 的 native keyboard scancode

优点：

- handoff 更可控

缺点：

- 当前 helper backend 只支持 helper key pool，不支持 gameplay 原生键
- 这条需要先扩 backend 合同

### 5.2 当前更推荐的先后顺序

先做：

- 方案 A：Sprint-only queue mediation

理由：

- 现有代码里已经有 queue 访问基础
- 改动面更窄
- 更符合“只修 Sprint，不扩大战线”

---

## 6. Sticky emitter 规则

为了避免来回抖动，`Sprint` 的 emitter 切换应是 sticky 的：

- 当前 emitter 只要还 held，就继续保持 ownership
- 另一来源即使也开始 held，只进入 standby
- 只有 active emitter release，才允许 standby source takeover

这会带来更符合直觉的行为：

- 手柄先冲刺，键盘按下不打断
- 键盘先冲刺，手柄按下先 standby
- 键盘释放后，手柄 takeover 并 re-edge

这也是当前用户预期最像的一组规则。

---

## 7. 为什么这次不继续直接改代码

因为当前问题已经不是：

- contributor mask 算错
- held OR 没生效
- `GameplayOwner` 阈值不对

而是：

- 我们还没有真正控制 native keyboard sprint 的游戏入口

在这之前继续改：

- owner
- lease
- 持续态聚合

只会继续得到“插件内部看起来都对，但游戏里还是断”的结果。

---

## 8. 下一步实施顺序

1. 明确 `Sprint` 对应的 native keyboard scancode / queue 入口
2. 先做 `SprintEmitterSource + sticky emitter`
3. 再落 Sprint-only queue mediation
4. 完成 `KeyboardMouse -> Gamepad` 的 handoff re-edge
5. 实机验证三类场景：
   - 只键盘 sprint
   - 先手柄后键盘
   - 先键盘后手柄再松键盘

---

## 9. 其它 hold 动作是否也适用

不能一刀切。

当前更像会进入这类模型的，是：

- `Sprint`

下一批值得单独评估的是：

- `Shout`

但前提仍然是：

- 先确认它在游戏里是否也属于类似 `HeldStateHandler` / 单发射源语义

所以后续推荐分类应是：

- `PulseOwner`
- `ToggleOwner`
- `SingleEmitterHold`
- `SingleEmitterRepeat`

而不是把所有 `HoldOwner` 全都自动并进 `SingleEmitterHold`。
