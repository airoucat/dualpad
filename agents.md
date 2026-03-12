我说的“更优秀实现”，核心不是把现在这条 `keyboard-native` 线再打磨得更像真键盘，而是换一个更贴近 Skyrim 动作语义的分层方式。

简单说：

- `Route B` 继续只管模拟量
- `Jump` 暂时留在你已经打通的 `keyboard-native`
- `Sprint / Activate / Sneak` 不再主要依赖“伪造键盘来源”，而是直接消费你已经有的按帧生命周期状态，然后走更直接的原生动作注入

**先把现状翻成白话**
你现在其实已经有两样很重要的东西了。

第一样，是“这一帧手柄到底发生了什么”的快照归纳器。[SyntheticStateReducer.cpp](c:/Users/xuany/Documents/dualPad/src/input/injection/SyntheticStateReducer.cpp#L73) 已经在按帧计算：

- `pressed`
- `released`
- `held`
- `heldSeconds`
- `pulse`

这说明“按下了没有、按了多久、这一帧是不是刚松开”这些信息，代码里其实已经有了。

第二样，是“把这些生命周期送去后端”的入口。[PadEventSnapshotProcessor.cpp](c:/Users/xuany/Documents/dualPad/src/input/injection/PadEventSnapshotProcessor.cpp#L200) 的 `UpdateActiveButtonActions()` 每帧都会把动作转成：

- `down=true, heldSeconds=...`
- 或 `down=false, heldSeconds=...`

再交给 `DispatchButtonState()`。

所以，问题已经不太是“前面有没有正确识别出 Sprint/Hold”，而是“最后一公里怎么告诉游戏”。

**为什么我觉得现在这条线不够优**
关键卡点在这里：[ActionDispatcher.cpp](c:/Users/xuany/Documents/dualPad/src/input/ActionDispatcher.cpp#L27) 现在把 `Sprint / Jump / Activate / Sneak / Shout` 都优先路由到 `KeyboardNativeBackend`。

但 [KeyboardNativeBackend.cpp](c:/Users/xuany/Documents/dualPad/src/input/backend/KeyboardNativeBackend.cpp#L2469) 这段 `QueueAction()` 有两个非常关键的特征：

- 一上来就 `(void)heldSeconds;`
- 如果这个动作已经处于 active，它会直接 `return true`

翻成白话就是：

1. 它根本没把“长按了 0.3 秒 / 0.8 秒”这类信息往下传  
2. 它更像“把某个 scancode 置为按住/松开”，而不是“持续向游戏表达这个动作当前处于 held 语义”

这对 `Jump` 还可能够用，因为 `Jump` 已经有你验证过的那条特殊成功路线。  
但对 `Sprint / Activate / Sneak` 这种本来就更依赖 process/时序语义的动作，这条线天然就更脆。

也就是说，现在你前半段已经能说清“玩家在长按 Sprint”，但最后送进 `keyboard-native` 后，后端只是在维护一个“这个键目前算不算按下”的引用计数。  
如果 Skyrim 的这几个 handler 更在意：

- 持续 held 过程
- holdTime
- 最小按下窗口
- toggle 的边沿语义

那这条线就会越来越难修。

**为什么我认为更好的方向是 Native semantic**
你仓库里其实已经有更接近目标的东西了。

[NativeInputInjector.cpp](c:/Users/xuany/Documents/dualPad/src/input/injection/NativeInputInjector.cpp#L404) 的 `SubmitFrame()` 已经是在按帧提交按钮/轴状态。  
而且它不是只发一个瞬时 pulse，它会把：

- `value`
- `heldSeconds`

一起往下走。  
比如数字按钮那段在 [NativeInputInjector.cpp](c:/Users/xuany/Documents/dualPad/src/input/injection/NativeInputInjector.cpp#L510) 已经是标准的：

- 初次按下时发 press
- 持有期间保持 down
- 松开时发 release，并带 release 时的 heldSeconds

再往前一步，[NativeInputPreControlMapHook.cpp](c:/Users/xuany/Documents/dualPad/src/input/injection/NativeInputPreControlMapHook.cpp#L114) 已经有 pre-ControlMap 注入点。  
这条线的思路不是“假装有个键盘键来了”，而是“在 Skyrim 自己消费输入前，把我们构造好的原生输入事件塞进去”。

这比继续在 `dinput / keyboard provenance` 上死磕，更像是在跟游戏说：

- 现在 `Sprint` 确实按下了
- 它已经 held 了多久
- 现在它松开了

这正是你现在最缺的那一层。

**具体到这 4 个动作，我建议这样分**
`Sprint`
: 把它定义成真正的 `Held` 动作。首帧 press，后续每帧保持 down，直到真实 release。不要再主要靠窄 pulse 或“晚一点松开”凑语义。

`Activate`
: 把它定义成 `ShortPressWithMinDownWindow`。本质还是短按，但至少保证一个稳定的最小按下窗口，比如 35 到 60ms，而不是过快 release。

`Sneak`
: 单独定义成 `Toggle` 或 `EdgeTriggeredToggle`。它和 Sprint 不是一类，不该硬套 held 逻辑，只需要稳定的“按下边沿触发一次”加防抖。

`Jump`
: 先别动结构，继续留在当前成功路线。它已经是“特殊家族”，没必要为了统一而把已成功路径打散。

**我说的“更优实现”长什么样**
我建议在现有架构上再加一个很薄的“动作生命周期策略层”，而不是继续把语义散在 backend 里。

可以理解成一张表：

- `Jump -> KeyboardNative / PressRelease`
- `Sprint -> NativeSemantic / HoldOwner`
- `Activate -> NativeSemantic / MinDownWindow`
- `Sneak -> NativeSemantic / Toggle`
- 其他菜单或插件动作再各自归类

这样每帧流程就会变成：

1. `SyntheticStateReducer` 产出这一帧按钮生命周期
2. 一个 `ActionLifecyclePolicy` 决定“这个动作该按什么语义发”
3. 对应 backend 执行
4. debug 日志同时打印“输入帧状态 + 生命周期决策 + 实际注入结果”

这样做的好处是：

- 后端 ownership 更清晰
- `Sprint` 不会再被当成“只是一个键按住”
- `Activate` 的最小按下窗口能集中管理
- `Sneak` 的 toggle 不会和 held 混在一起
- `Jump` 可以继续吃你现在已经验证成功的特殊路线
- 后续扩展新动作时，只是加策略，不是重挖 routing

**一句话总结**
你现在前半段已经能正确理解 DualSense 的“按下/长按/松开”，真正该升级的是“怎么把这个语义告诉 Skyrim”。  
所以更优方案不是继续深挖 second-family 的键盘来源，而是把 `Sprint / Activate / Sneak` 提升成“生命周期驱动的原生动作注入”，只把 `Jump` 留在现有 keyboard-native 成功路径。

如果你愿意，我下一条可以直接把这个方案画成一张“当前链路 vs 新链路”的结构图，再顺手给出最小落地改法，尽量不大拆你现在的基线。

---
“生命周期驱动的原生动作注入”，可以拆成两句话理解：

- “生命周期驱动” = 不再看到一次 `ButtonPress` 就立刻 `pulse` 一下，而是把一个动作当成完整状态机来维护：`Press -> Hold -> Release`，外加 `最小按下窗口 / 防抖 / Toggle`
- “原生动作注入” = 不再主要依赖 [KeyboardNativeBackend.cpp](c:/Users/xuany/Documents/dualPad/src/input/backend/KeyboardNativeBackend.cpp) 那套 scancode/provenance 线路，而是走 [NativeInputInjector.cpp](c:/Users/xuany/Documents/dualPad/src/input/injection/NativeInputInjector.cpp) 创建 `RE::ButtonEvent`，再由 [NativeInputPreControlMapHook.cpp](c:/Users/xuany/Documents/dualPad/src/input/injection/NativeInputPreControlMapHook.cpp) 在 `ControlMap` 消费前塞进游戏输入链。

**它在你现在代码里的实现基础**
你其实已经有 70% 了。

- [SyntheticPadFrame.h](c:/Users/xuany/Documents/dualPad/src/input/injection/SyntheticPadFrame.h) 已经定义了每帧按钮生命周期字段：`down / pressed / released / held / heldSeconds`
- [SyntheticStateReducer.cpp](c:/Users/xuany/Documents/dualPad/src/input/injection/SyntheticStateReducer.cpp) 已经按帧把 `PadEventSnapshot` 归约成这些状态
- [PadEventSnapshotProcessor.cpp](c:/Users/xuany/Documents/dualPad/src/input/injection/PadEventSnapshotProcessor.cpp) 已经有 `_activeButtonActions`，并且每帧在 `UpdateActiveButtonActions()` 里处理生命周期
- [NativeInputInjector.cpp](c:/Users/xuany/Documents/dualPad/src/input/injection/NativeInputInjector.cpp) 已经能把 `actionId -> userEvent/idCode -> staged ButtonEvent`
- [NativeInputPreControlMapHook.cpp](c:/Users/xuany/Documents/dualPad/src/input/injection/NativeInputPreControlMapHook.cpp) 已经能在主线程、`ControlMap` 前注入

所以真正要补的，不是整套新系统，而是中间这一层：

`SyntheticButtonState -> ActionLifecyclePolicy -> NativeInputInjector`

**这层具体长什么样**
建议新增一个很薄的策略表，例如新建 `ActionLifecyclePolicy.h/.cpp`：

```cpp
enum class LifecycleMode : std::uint8_t
{
    HoldOwner,       // Sprint
    MinDownWindow,   // Activate
    TogglePulse      // Sneak(toggle)
};

struct LifecyclePolicy
{
    LifecycleMode mode;
    std::uint32_t minDownUs{ 0 };
    std::uint32_t debounceUs{ 0 };
};
```

然后把 [PadEventSnapshotProcessor.h](c:/Users/xuany/Documents/dualPad/src/input/injection/PadEventSnapshotProcessor.h) 里的 `ActiveButtonAction` 扩成一个真正的小状态机，比如：

```cpp
struct ActiveButtonAction
{
    bool active{ false };
    std::string actionId{};
    LifecycleMode mode{ LifecycleMode::HoldOwner };

    bool emittedDown{ false };
    std::uint64_t logicalPressedAtUs{ 0 };
    std::uint64_t releaseNotBeforeUs{ 0 };
    std::uint64_t debounceUntilUs{ 0 };
};
```

这里的关键不是“物理键当前是否按着”，而是“我们准备发给游戏的逻辑动作当前是否应当按着”。

**每帧怎么跑**
[PadEventSnapshotProcessor.cpp](c:/Users/xuany/Documents/dualPad/src/input/injection/PadEventSnapshotProcessor.cpp) 的 `Process()` 里已经有主流程，新的版本只是在 `UpdateActiveButtonActions()` 里多一步“算逻辑状态”：

1. 从 `frame.buttons[bitIndex]` 读出源按钮的 `pressed / down / released / heldSeconds`
2. 根据 `actionId` 取 `LifecyclePolicy`
3. 算出这一帧的 `effectiveDown` 和 `effectiveHeldSeconds`
4. 调用 `_nativeInjector.QueueButtonAction(actionId, effectiveDown, effectiveHeldSeconds)`
5. 由 [NativeInputInjector.cpp](c:/Users/xuany/Documents/dualPad/src/input/injection/NativeInputInjector.cpp) `StageButtonEvent()`
6. 由 [NativeInputPreControlMapHook.cpp](c:/Users/xuany/Documents/dualPad/src/input/injection/NativeInputPreControlMapHook.cpp) 在 `Poll -> ControlMap` 前注入

也就是说，游戏看到的不再是“我们临时伪造了一个键盘键”，而是“这一帧确实收到一个原生 `ButtonEvent`，它当前是按下还是释放、持有多久”。

**Sprint 怎么实现**
`Sprint` 最简单，几乎是现成的。

规则：

- 源按钮 `pressed` 时：发 `down=true, heldSeconds=0`
- 源按钮持续 `down` 时：每帧继续发 `down=true, heldSeconds=真实累计时间`
- 源按钮 `released` 时：发 `down=false, heldSeconds=总持有时间`

伪代码：

```cpp
if (button.down) {
    const float held = button.pressed ? 0.0f : button.heldSeconds;
    _nativeInjector.QueueButtonAction(actions::Sprint, true, held);
} else if (slot.emittedDown || button.released) {
    _nativeInjector.QueueButtonAction(actions::Sprint, false, button.heldSeconds);
    slot.active = false;
}
```

这就是“held owner”。  
它和现在最大的区别是：不再依赖 [KeyboardNativeBackend.cpp](c:/Users/xuany/Documents/dualPad/src/input/backend/KeyboardNativeBackend.cpp) 那种 scancode 引用计数；而是直接把“当前仍在 sprint hold”这个事实按帧告诉游戏。

**Activate 怎么实现**
`Activate` 不适合纯 held，也不适合超窄 pulse。  
它最稳的是“短按，但保证最小按下窗口”。

规则：

- 首次 `pressed`：立刻发 `down=true`
- 同时记录 `releaseNotBeforeUs = now + 40000` 之类
- 如果玩家很快松手，但还没到这个时间点，逻辑上仍然保持 `down=true`
- 到了窗口末尾再发 `down=false`

伪代码：

```cpp
if (button.pressed) {
    slot.logicalPressedAtUs = now;
    slot.releaseNotBeforeUs = now + 40000; // 40ms
    slot.emittedDown = true;
    _nativeInjector.QueueButtonAction(actions::Activate, true, 0.0f);
    return;
}

const bool keepDown = button.down || (slot.emittedDown && now < slot.releaseNotBeforeUs);
if (keepDown) {
    const float held = std::max(0.0f, float(now - slot.logicalPressedAtUs) / 1000000.0f);
    _nativeInjector.QueueButtonAction(actions::Activate, true, held);
} else if (slot.emittedDown) {
    const float held = float(now - slot.logicalPressedAtUs) / 1000000.0f;
    _nativeInjector.QueueButtonAction(actions::Activate, false, held);
    slot.active = false;
}
```

重点是：这个窗口是用 `frame.sourceTimestampUs` 算的，不是 `Sleep()`，所以它是按帧确定性的。

**Sneak 怎么实现**
`Sneak` 要分两种。

- 如果你想按“切换潜行”处理：用 `TogglePulse`
- 如果游戏/配置是“按住潜行”：直接复用 `Sprint` 的 `HoldOwner`

更稳的默认实现通常是 toggle：

- 只在 `pressed` 边沿触发一次
- 发一个短的 native `press`
- 保持一个最小窗口
- 自动发 `release`
- 在 `debounceUs` 内忽略重复边沿

伪代码：

```cpp
if (button.pressed && now >= slot.debounceUntilUs) {
    slot.logicalPressedAtUs = now;
    slot.releaseNotBeforeUs = now + 30000; // 30ms
    slot.debounceUntilUs = now + 120000;   // 120ms
    slot.emittedDown = true;
    _nativeInjector.QueueButtonAction(actions::Sneak, true, 0.0f);
    return;
}

if (slot.emittedDown && now >= slot.releaseNotBeforeUs) {
    const float held = float(now - slot.logicalPressedAtUs) / 1000000.0f;
    _nativeInjector.QueueButtonAction(actions::Sneak, false, held);
    slot.active = false;
}
```

如果后面你确认 `Sneak` 在当前玩法里更适合 hold，那就把它的策略从 `TogglePulse` 改成 `HoldOwner`，不用改整个注入链。

**为什么这比 keyboard-native 更稳**
因为现在这条 native 路线天然满足你想要的几点：

- 它吃的是按帧生命周期，不是单次事件
- 它能携带 `heldSeconds`
- 注入发生在主线程、`ControlMap` 前
- [NativeInputInjector.cpp](c:/Users/xuany/Documents/dualPad/src/input/injection/NativeInputInjector.cpp) 的 `StageButtonEvent()` 还会合并同一轮 `Poll` 内的重复状态，不会疯狂堆事件
- 它避开了 second-family 那些 “validate 过了但 keyboard process 不一定稳定吃” 的 provenance 问题

**最小改法**
如果走最小侵入版本，我会这么改：

- 在 [ActionDispatcher.cpp](c:/Users/xuany/Documents/dualPad/src/input/ActionDispatcher.cpp) 里把 `Sprint / Activate / Sneak` 从 `ShouldPreferKeyboardNative()` 移出去
- 同时把 `Sprint` 从 `ShouldPreferCompatibilityState()` 移出去
- 新增 `ActionLifecyclePolicy`
- 扩展 [PadEventSnapshotProcessor.h](c:/Users/xuany/Documents/dualPad/src/input/injection/PadEventSnapshotProcessor.h) 的 `ActiveButtonAction`
- 在 [PadEventSnapshotProcessor.cpp](c:/Users/xuany/Documents/dualPad/src/input/injection/PadEventSnapshotProcessor.cpp) 的 `UpdateActiveButtonActions()` 里按策略机计算 `effectiveDown`
- `Jump` 继续留在 [KeyboardNativeBackend.cpp](c:/Users/xuany/Documents/dualPad/src/input/backend/KeyboardNativeBackend.cpp) 的成功路线，不动结构

如果你愿意，我下一条可以直接把这三种状态机画成“逐帧时间线图”，比如 `t0 按下 -> t1 保持 -> t2 松开`，你会一眼看懂为什么 `Sprint`、`Activate`、`Sneak` 不能共用同一种发射方式。