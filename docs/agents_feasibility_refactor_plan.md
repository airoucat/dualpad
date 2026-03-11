# agents.md 可行性分析与重构方案

基于 2026-03-07 当前仓库工作树的实际代码，对 `agents.md` 中的目标进行可行性评估，并给出与现状对齐的重构方案。

## 1. 结论

`agents.md` 的总体方向是对的，但不能按“映射层已经完备、注入层只差接线”来执行。当前项目更接近下面这个状态：

- 映射层已经具备“单次 HID 包处理生成一个 `PadEventBuffer`”的雏形。
- 这还不是完整的“游戏帧级原子快照消费链路”。
- 现有注入层本质上仍是 `XInput IAT hook + SyntheticPadState` 的兼容桥，不是 Skyrim 原生输入注入层。
- `Combo`、多修饰键、事件释放语义、主线程注入、安全快照、Mod 虚拟键池这几项都还没有真正落地。

因此，`agents.md` 里最可行的做法不是“直接开始做最终注入层”，而是先补齐事件契约和主线程注入边界，再逐步替换掉当前兼容桥。

## 2. 按条目评估可行性

### 2.1 映射层是否已经输出帧级原子事件快照

结论：`部分成立`，但严格说只是“每个 HID 输入包生成一个固定缓冲事件列表”，不是完整的“注入层原子快照协议”。

已成立的部分：

- `PadEventGenerator::Generate()` 会在一次调用内清空并填充同一个 `PadEventBuffer`，顺序固定为 `Button -> Axis -> Tap/Hold -> Combo -> Touchpad`。
  - 参考 `src/input/mapping/PadEventGenerator.cpp:27`
- `HidReader` 每次读到一个包，会创建局部 `PadEventBuffer events{}`，同步调用 `Generate()`，再同步调用 `DispatchPadEvents()`。
  - 参考 `src/input/HidReader.cpp:147`
- `PadEventBuffer` 是固定容量数组，不存在异步追加。
  - 参考 `src/input/mapping/PadEvent.h:181`

不成立或不完整的部分：

- `ComboEvaluator` 目前是空实现，`Combo` 事件实际上没有生成。
  - 参考 `src/input/mapping/ComboEvaluator.cpp:10`
- `TriggerMapper` 会忽略 `ButtonRelease` 和 `TouchpadRelease`，所以事件流不能表达完整的按键生命周期。
  - 参考 `src/input/mapping/TriggerMapper.cpp:61`
- `PadEventBuffer` 溢出后只记警告并丢事件，和“不丢帧、不吞键”目标冲突。
  - 参考 `src/input/mapping/PadEventGenerator.cpp:37`
  - 参考 `src/input/mapping/PadEvent.h:197`
- 现在的“帧”是 HID 包帧，不是 Skyrim 主线程输入帧。`HidReader` 在线程中处理输入，游戏侧消费在另一个时序里发生。
  - 参考 `src/input/HidReader.cpp:100`
- 轴状态没有通过统一事件快照消费到最终注入层，而是仍然走 `compatFrame -> SyntheticPadState::SetAxis()` 的旧桥。
  - 参考 `src/input/HidReader.cpp:156`

补充判断：

- 事件顺序是确定的，但“组合键顺序正确”并没有保证。当前只有 `modifierMask`，没有显式的按下顺序历史。
- `TouchpadMapper` 只使用 `touch1`，没有消费 `touch2`，因此 `PadState` 中的双触点能力并未进入映射输出。
  - 参考 `src/input/mapping/TouchpadMapper.cpp:168`

### 2.2 注入层现状是否满足 agents.md 的目标

结论：`不满足`。当前实现是兼容桥，不是完整注入层。

当前实际路径：

1. `HidReader` 线程读取 DualSense 包。
2. 生成 `PadEventBuffer`。
3. 立刻在同一线程里逐条 `Resolve -> Execute`。
4. 同时把部分按钮/轴写入 `SyntheticPadState`。
5. `IATHook` 拦截 `XInputGetState()`，把 `SyntheticPadState` 映射成 XInput 手柄状态。

关键证据：

- `ActionExecutor` 当前直接从映射结果执行动作。
  - 参考 `src/input/HidReader.cpp:48`
- 现有注入路径是 `InstallXInputIATHook()`。
  - 参考 `src/main.cpp:47`
  - 参考 `src/input/IATHook.cpp:223`
- `SyntheticPadState` 只维护：
  - `heldDown`
  - `pulseDown`
  - 6 个轴值
  - 没有 `Pressed/Released/Held/Tap/Hold/Combo` 生命周期状态机
  - 参考 `src/input/SyntheticPadState.h:22`

这意味着：

- 目前没有独立的“事件快照消费层”。
- 目前没有原生 `ButtonEvent / ThumbstickEvent` 注入后端。
- 目前也没有 ModEvent 注入后端，只是把部分动作脉冲成手柄位。

### 2.3 Skyrim 原生输入注入是否可行

结论：`可行`，并且当前仓库依赖已经具备所需入口。

仓库内 `commonlibsse-ng` 已包含：

- `RE::BSInputEventQueue::AddButtonEvent`
  - 参考 `lib/commonlibsse-ng/include/RE/B/BSInputEventQueue.h:30`
- `RE::BSInputEventQueue::AddThumbstickEvent`
  - 参考 `lib/commonlibsse-ng/include/RE/B/BSInputEventQueue.h:48`
- `RE::ButtonEvent::Create`
  - 参考 `lib/commonlibsse-ng/include/RE/B/ButtonEvent.h:35`
- `RE::ThumbstickEvent`
  - 参考 `lib/commonlibsse-ng/include/RE/T/ThumbstickEvent.h:7`
- `RE::ControlMap`
  - 参考 `lib/commonlibsse-ng/include/RE/C/ControlMap.h:84`
- `RE::BSInputDeviceManager::PollInputDevices()`，并且注释明确说明它会处理输入队列。
  - 参考 `lib/commonlibsse-ng/src/RE/B/BSInputDeviceManager.cpp:151`

因此，`agents.md` 里“通过 ControlMap / InputEventQueue / BSInputDeviceManager 注入”在技术上是可行的，且优先级应高于继续扩展 XInput 兼容层。

### 2.4 多线程安全是否成立

结论：`当前不成立`。

主要问题：

- `HidReader` 在线程里直接读 `ContextManager::GetCurrentContext()`。
  - 参考 `src/input/HidReader.cpp:79`
- `ContextManager` 没有任何锁或原子保护，但会被 UI 事件、战斗事件、轮询线程和 HID 线程并发访问。
  - 参考 `src/input/InputContext.h:127`
  - 参考 `src/input/InputContext.cpp:16`
  - 参考 `src/input/ContextEventSink.cpp:63`
- `ActionExecutor` 在 HID 线程里直接访问 `RE::UI`、`RE::UIMessageQueue`、`RE::PlayerCamera` 等游戏对象，这不应继续保留。
  - 参考 `src/input/ActionExecutor.cpp:60`

结论很明确：游戏相关 API 调用必须收口到主线程，HID 线程只能产出快照或队列。

### 2.5 Debug 能力是否满足

结论：`部分成立`。

已有：

- 映射事件日志开关存在，调试版下可通过环境变量 `DUALPAD_LOG_MAPPING_EVENTS` 打开。
  - 参考 `src/input/mapping/EventDebugLogger.cpp:28`

缺少：

- 注入层快照日志
- `SyntheticPadState` 生命周期日志
- 每帧“输入快照 -> 状态更新 -> 注入结果”完整链路日志
- 运行时 INI 开关

### 2.6 INI 自定义键位和自由组合键是否可行

结论：`当前只支持非常有限的子集`。

目前 `BindingConfig::ParseTrigger()` 的能力：

- `Button:Name`
- `Button:Modifier+Name`
- `Combo:A+B`
- `Hold:Name`
- `Tap:Name`
- `Axis:Name`
- `Gesture:Name`

但实际限制是：

- 只支持 `一个 modifier + 一个主键`
- `Combo` 只是解析成 `TriggerType::Combo`，并没有真正的 `ComboEvaluator`
- 没有实现“严格禁止两个 FN 键与面键组合”

参考：

- `src/input/BindingConfig.cpp:186`
- `src/input/mapping/ComboEvaluator.cpp:10`

### 2.7 Mod 虚拟按键池是否可直接按 agents.md 使用

结论：`需要修正`。

本地资源实际情况：

- `keyboard_english.txt` 中存在 `F13/F14/F15`
  - 参考 `g:/skyrim_mod/bsa-interface/interface/controls/pc/keyboard_english.txt:90`
- 本地文件中没有出现 `F16-F20`
- `controlmap.txt` 中没有占用 `F13-F15`
  - 已搜索 `G:/skyrim_mod/bsa-interface/interface/controls/pc/controlmap.txt`

所以当前仓库对应的实际建议应改成：

- 主推荐：`F13/F14/F15`
- 备用池：`DIK_KANA / DIK_ABNT_C1 / DIK_CONVERT / DIK_NOCONVERT / DIK_ABNT_C2 / NumPadEqual / PrintSrc / L-Windows / R-Windows / Apps ...`

如果要保留 `ModEvent1-ModEvent8`，不能再写成“主推荐 F13-F20”，因为本地资源并不支持这一点。

## 3. 需要修正的 agents.md 结论

建议把 `agents.md` 的核心判断改成下面这种更贴近现状的版本：

- 当前映射层输出的是“每个 HID 包的同步事件列表”，不是已经完全完成的“注入层原子快照协议”。
- 当前注入层仍然是 `XInput compatibility bridge`，不是 Skyrim 原生输入事件注入。
- `Combo` 尚未实现。
- `SyntheticPadState` 还不是生命周期状态机，只是兼容层状态缓存。
- `F13-F20` 需要修正为 `F13-F15 + 备用 DIK 池`。
- 所有游戏侧对象访问必须迁移到主线程。

## 4. 推荐重构目标

把现有链路重构成四段：

1. `PadState Capture`
2. `PadEvent Snapshot`
3. `Synthetic State Reduction`
4. `Main-thread Injection Backend`

对应职责：

- HID 线程只负责读取设备、标准化 `PadState`、生成 `PadEventSnapshot`
- 注入状态机只负责把一帧快照归约成 `SyntheticPadFrame`
- 主线程后端负责：
  - Native controlmap 注入
  - 自定义动作调用
  - Mod 虚拟按键注入

## 5. 推荐新接口

### 5.1 帧快照契约

建议新增一个独立于映射层实现的注入输入契约：

```cpp
struct PadEventSnapshot
{
    std::uint64_t sourceTimestampUs;
    std::uint64_t sequence;
    PadState currentState;
    PadEventBuffer events;
    bool overflowed;
};
```

目的：

- 明确“这是一整帧输入”
- 让注入层同时看到 `PadState` 和 `PadEvent`
- 提供 `sequence` 做跨线程一致性检查

### 5.2 Synthetic 状态机

建议不要继续沿用当前 `SyntheticPadState` 的原子位图语义，而是拆成：

```cpp
struct VirtualButtonState
{
    bool down;
    bool pressed;
    bool released;
    bool held;
    bool tapTriggered;
    bool holdTriggered;
    float heldSeconds;
    std::uint64_t pressedAtUs;
    std::uint64_t releasedAtUs;
};

struct SyntheticPadFrame
{
    std::uint64_t sequence;
    InputContext context;
    std::array<VirtualButtonState, kVirtualButtonCount> buttons;
    AxisState move;
    AxisState look;
    AxisState leftTrigger;
    AxisState rightTrigger;
    PendingNativeEvents nativeEvents;
    PendingCustomEvents customEvents;
    PendingModEvents modEvents;
};
```

重点：

- `pressed/released/held` 必须是每帧重算结果
- `tap/hold/combo` 是语义事件，不应再依赖 `PulseButton(50ms)` 这种兼容桥
- 轴要有“当前值 + 变化标记”

### 5.3 注入后端接口

```cpp
class IGameInputInjector
{
public:
    virtual ~IGameInputInjector() = default;
    virtual void SubmitFrame(const SyntheticPadFrame& frame) = 0;
};
```

分成三个后端：

- `NativeInputInjector`
  - 负责 `ButtonEvent / ThumbstickEvent / BSInputEventQueue`
- `CustomActionSink`
  - 负责截图、连拍、自定义插件功能
- `ModEventSink`
  - 负责把 ModEvent1-8 映射到虚拟键池

## 6. 推荐执行流程

建议把“每帧规则”改成下面这种可实现版本：

1. HID 线程读取一个输入包，得到 `PadState current`
2. 映射层同步生成 `PadEventSnapshot`
3. 将 `PadEventSnapshot` 写入无锁双缓冲或固定环形缓冲
4. 主线程在固定注入点取出最新快照
5. `SyntheticStateReducer` 根据快照更新 `SyntheticPadFrame`
6. 做上下文校验
7. 路由到：
   - `NativeInputInjector`
   - `CustomActionSink`
   - `ModEventSink`
8. 输出调试日志

关键点：

- HID 线程不再直接操作 `RE::UI` 或 `RE::ControlMap`
- 注入发生在主线程
- 映射快照和注入状态机之间有清晰边界

## 7. 推荐模块拆分

建议新增目录 `src/input/injection/`，按下面拆：

- `PadEventSnapshot.h`
- `SyntheticStateReducer.h/.cpp`
- `SyntheticPadFrame.h`
- `InjectionRouter.h/.cpp`
- `NativeInputInjector.h/.cpp`
- `CustomActionSink.h/.cpp`
- `ModEventSink.h/.cpp`
- `VirtualKeyPool.h/.cpp`
- `InjectionDebugLogger.h/.cpp`

现有文件建议处理方式：

- `HidReader.cpp`
  - 保留 HID 读取和 `PadEventGenerator`
  - 去掉直接 `ActionExecutor` 执行
- `SyntheticPadState.*`
  - 降级为临时兼容桥，后续删除
- `IATHook.cpp`
  - 第一阶段保留作为 fallback
  - 第二阶段逐步退出主路径
- `ActionExecutor.cpp`
  - 拆成 `CustomActionSink` 和历史兼容工具

## 8. 分阶段重构方案

### 阶段 0：先修正契约，不碰最终注入

目标：

- 不再把当前代码误判成“完整注入层”
- 为下一阶段建立稳定接口

工作项：

- 新增 `PadEventSnapshot`
- 给快照增加 `sequence`
- 在 `HidReader` 中只产出快照，不再直接执行动作
- 增加 `overflowed` 的硬告警统计

验收标准：

- 能打印出“单包输入 -> 单快照”的完整日志
- 不再在 HID 线程中触碰 Skyrim UI 对象

### 阶段 1：补齐映射层缺口

目标：

- 让“快照”真正足够驱动注入层

工作项：

- 实现 `ComboEvaluator`
- 明确 `Combo` 语义：
  - 是 chord
  - 还是 ordered combo
- 如果需要顺序正确，补充 `press order` 记录
- 决定是否把 `ButtonRelease` 纳入可解析触发器
- 扩展 `ParseTrigger()`，支持多个 modifiers
- 加入“禁止两个 FN 键 + 面键”校验

验收标准：

- `Combo/Hold/Tap/Touchpad` 全部能在日志中稳定复现
- 配置非法组合会在加载阶段报错

### 阶段 2：引入 Synthetic 状态机

目标：

- 用正式状态机替代 `PulseButton + heldDown`

工作项：

- 新增 `SyntheticStateReducer`
- 输出 `pressed/released/held/tap/hold/combo`
- 对 Move/Look/Trigger 轴输出统一状态
- 加入 `SyntheticPadFrame` 调试日志

验收标准：

- 单击、长按、组合键、滑动都能看到稳定生命周期
- 不再依赖固定 50ms pulse 模拟瞬时键

### 阶段 3：落地 Skyrim 原生输入注入

目标：

- 用 `BSInputEventQueue` 替代主路径 XInput 兼容桥

工作项：

- 新增 `NativeInputInjector`
- 将按钮映射成 `ButtonEvent`
- 将左右摇杆映射成 `ThumbstickEvent`
- 通过 `ControlMap` 校验当前上下文是否允许对应用户事件
- 优先把 Gameplay/Menu/Map/Inventory/Lockpicking/Book/Stats 这些 agents.md 列出的事件做成显式表驱动

推荐注入点：

- 优先围绕 `BSInputDeviceManager::PollInputDevices()` 的主线程时机注入

验收标准：

- 核心 controlmap 动作不再依赖 `XInputGetState` hook
- 轴和按钮都走原生输入队列

### 阶段 4：补齐 ModEvent 和兼容收尾

目标：

- 完成插件事件和 Mod 预留接口

工作项：

- 实现 `VirtualKeyPool`
- 默认池改为 `F13-F15 + 备用 DIK`
- 暴露 `ModEvent1-8` 配置层
- 保留 `IATHook` 作为 fallback/迁移开关

验收标准：

- Mod 事件不与 controlmap 默认键冲突
- 自定义动作和 Mod 虚拟键可以并存

## 9. 具体设计建议

### 9.1 关于 Combo

如果目标是“组合键顺序正确”，不要只用 `modifierMask`。

至少需要：

- `pressOrder` 或 `firstPressedAtUs`
- `comboWindowUs`
- `maxFnKeyCount`

否则当前模型只能表达“某键按下时还有哪些键处于按住状态”，不能表达严格顺序。

### 9.2 关于 Hold

当前 `TapHoldEvaluator` 只会发一次 `Hold` 事件，不会持续发。

这是合理的，但注入层必须自己维护：

- `down`
- `held`
- `heldSeconds`

不能把“收到过一次 Hold 事件”当成完整长按状态。

### 9.3 关于轴

当前 `AxisEvaluator` 是阈值差分事件。

这适合做语义触发，不适合直接作为注入层唯一轴来源。注入层应同时读取当前 `PadState` 中的轴值，避免小幅度连续变化因为阈值而不更新。

### 9.4 关于上下文

`ContextManager` 需要重新设计并发模型：

- 最简单的做法是把上下文判定也收口到主线程
- HID 线程不读上下文
- 上下文校验放在 `InjectionRouter` 阶段做

## 10. 建议优先级

推荐按下面顺序实施，而不是一次性全改：

1. 去掉 HID 线程里的游戏对象调用
2. 建立 `PadEventSnapshot`
3. 实现 `ComboEvaluator` 和多 modifier 配置
4. 建立 `SyntheticStateReducer`
5. 上主线程 `NativeInputInjector`
6. 最后再处理 Mod 虚拟键池和 fallback 清理

## 12. 过渡补丁记录

当前实现里有一个 `PadEventSnapshotDispatcher` 的快照合并补丁：

- 目的：避免在兼容期因为主线程消费点不足而把 HID 包快照队列冲爆
- 性质：过渡方案，不属于正式版最终架构
- 删除时机：
  - 当输入消费切到稳定的主线程帧钩子
  - 或切到正式 `NativeInputInjector` 主路径后

届时应删除或重写：

- `src/input/injection/PadEventSnapshotDispatcher.*` 中的 coalescing 逻辑
- 与 `firstSequence/coalesced` 相关的过渡型诊断字段

## 11. 最终判断

如果按当前 `agents.md` 原文直接推进，风险点有四个：

- 把“每 HID 包事件列表”误当成“完整原子注入快照”
- 把“XInput 兼容桥”误当成“原生注入层”
- 低估 `Combo`、释放语义、主线程约束的实现成本
- 采用与本地资源不符的 `F13-F20` 规划

如果按本文方案推进，则项目可以稳定地从“兼容桥”过渡到“真正的 Skyrim 原生输入注入层”，并且不会推翻已有的 HID、协议、基础映射工作。
