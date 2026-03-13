# 统一动作生命周期模型（ButtonEvent / Keyboard / Mod 兼容版）

更新时间：2026-03-13

## 目的

这份文档定义一套后端无关的统一动作生命周期模型，用来让以下几条路线共享同一层动作语义，而不是各自重复实现一套 tap/hold/repeat/toggle 逻辑：

- `ButtonEvent` 路线
- 低延迟键盘输出路线
- Mod 虚拟按键路线
- 插件内部动作路线

当前新的前提是：

- PC 原生事件主线准备回到 `ButtonEvent`
- 键盘路线不会再承担 Skyrim 原生 PC 事件主线
- 但键盘路线仍然保留，并且仍然需要：
  - 低延迟
  - 稳定
  - 兼容性好
  - 主要服务于 `ModEvent / VirtualKey / F13-F20 / 辅助键盘输出`

因此，真正需要保留和统一的不是“KeyboardNativeBackend 的具体交付细节”，而是“动作生命周期模型”本身。

## 设计目标

- 让 planner 先决定“动作语义”，backend 只负责“如何发出去”
- 让 `ButtonEvent` 与 keyboard-mod backend 消费同一套 contract
- 保留当前自由映射、上下文切换、combo/hold/tap 的能力
- 明确一个 stateful action 在整个生命周期里只能有一个 owning backend
- 避免不同 backend 各自私有地解释 `Pulse / Hold / Toggle / Repeat`
- 允许未来继续增加新 backend，而不重写整套生命周期逻辑

## 非目标

- 本文不规定最终 `ButtonEvent` 注入点选在哪个 callsite
- 本文不恢复旧的 consumer-side queue surgery 方案
- 本文不要求 keyboard backend 再承担 `Jump / Sprint / Activate / MenuConfirm` 这类 PC 原生事件
- 本文不展开游戏侧 `transition/readiness` 修正细节

## 一句话模型

统一链路应该是：

`输入触发器 -> 上下文 -> actionId -> contract -> 生命周期状态机 -> FrameActionPlan -> 后端发射`

关键点是：

- `contract` 属于 `actionId`
- `backend` 只消费 `FrameActionPlan`
- 生命周期状态机在 backend 之上

## 核心原则

### 1. contract 属于目标动作，不属于输入按键

不能写成：

- `Cross` 天生是 `Pulse`
- `L3` 天生是 `Hold`

必须写成：

- `Cross` 在当前上下文下解析成哪个 `actionId`
- 该 `actionId` 的 contract 是什么

例如：

- `Cross -> Game.Jump -> Pulse`
- `Cross -> Menu.Accept -> Pulse`
- `L3 -> Game.Sprint -> Hold`
- `Circle -> Game.Sneak -> Toggle`
- `TouchpadUp -> ModEvent1 -> Pulse`

### 2. 一个 stateful action 生命周期只能有一个 owning backend

例如：

- `Sprint` 一次按住不能在一半切到别的 backend
- `MenuDown` 一次长按不能前半段走 `ButtonEvent` repeat，后半段切 keyboard repeat
- `ModEvent1` 的一次 hold 也不能在 keyboard / plugin 之间漂移

### 3. planner 决定语义，backend 决定 materialization

planner 负责：

- actionId
- contract
- context
- source edge / hold time / value
- 拥有权与生命周期

backend 负责：

- 把计划翻译成 `ButtonEvent`
- 或翻译成键盘虚拟键 / scancode
- 或调用插件接口
- 或发出 mod 事件

backend 不应各自再推导：

- 这是不是 hold
- 这是不是 toggle
- 什么时候应该 repeat

### 4. keyboard backend 继续存在，但职责收窄

新的职责边界建议如下：

- `ButtonEventBackend`
  - Skyrim 原生 PC 控制事件主线
  - gameplay/menu/inventory/book/lockpicking 等原生 controlmap 行为
- `KeyboardBackend`
  - `ModEvent / VirtualKey / F13-F20`
  - 保留低延迟键盘输出能力
  - 不再默认承担 Skyrim 原生 PC 事件
- `NativeStateBackend`
  - 模拟量与原生 gamepad state
- `PluginActionBackend`
  - 截图、多截图、插件内部功能

## 推荐的共享数据模型

### 1. ActionRoutingDecision

当前 [ActionBackendPolicy.h](/c:/Users/xuany/Documents/dualPad/src/input/backend/ActionBackendPolicy.h) 里的方向是对的，建议继续保留：

- `backend`
- `kind`
- `contract`
- `nativeCode`
- `ownsLifecycle`

建议它只表达“该 action 应走哪个 backend、属于什么 contract”，不要继续夹带 keyboard 私有实现细节。

### 2. PlannedAction

当前 [FrameActionPlan.h](/c:/Users/xuany/Documents/dualPad/src/input/backend/FrameActionPlan.h) 已经很接近统一层的数据结构，建议继续保留为后端无关 plan：

- `backend`
- `kind`
- `phase`
- `context`
- `actionId`
- `contract`
- `sourceCode`
- `outputCode`
- `modifierMask`
- `timestampUs`
- `valueX`
- `valueY`
- `heldSeconds`

这里最重要的 3 个字段是：

- `actionId`
- `contract`
- `phase`

它们的关系是：

- `contract` 表示该动作整体生命周期规则
- `phase` 表示当前帧要交给 backend 的具体阶段

### 3. 生命周期状态记录

建议增加一层明确的 backend-neutral 生命周期状态，不要把它藏在某个 backend 私有表里。

推荐结构可以是：

```cpp
struct ActionLifecycleKey
{
    std::string actionId;
    std::uint32_t sourceCode;
    InputContext context;
};

struct ActionLifecycleState
{
    PlannedBackend owningBackend;
    ActionOutputContract contract;
    bool sourceDown;
    bool logicalActive;
    bool deliveredActive;
    bool rearmReady;
    float heldSeconds;
    float nextRepeatAtSeconds;
    std::uint32_t pendingPulseCount;
    std::uint64_t generation;
};
```

字段含义建议固定为：

- `sourceDown`
  - 输入源当前是否还按着
- `logicalActive`
  - 动作语义层当前是否处于 active
- `deliveredActive`
  - backend 当前是否已经被要求保持 active
- `rearmReady`
  - 用于 `Toggle / Tap` 这类按下边缘重入保护
- `heldSeconds`
  - 当前源按住时长
- `nextRepeatAtSeconds`
  - `Repeat` 下次触发时间点
- `pendingPulseCount`
  - 尚未交付的离散 pulse 数

## contract 的统一语义

### Pulse

适用动作：

- `Jump`
- `Activate`
- `Menu.Accept`
- `Menu.Cancel`
- `Hotkey1`
- `ModEvent1`

统一语义：

- 只在 `source down edge` 上生成一次动作事务
- 持续按住不重复发
- `source release` 仅用于结束源状态，不再额外生成第二个动作

backend materialization 允许不同，但语义必须一致：

- `ButtonEventBackend` 可以发一组 press/release 事件
- `KeyboardBackend` 可以发一组键盘 down/up
- `PluginActionBackend` 可以直接调用一次函数

但它们都只能消费“同一个 pulse 事务”。

### Hold

适用动作：

- `Sprint`
- `Block`
- `Aim`
- 某些 mod hold 动作

统一语义：

- `source down edge` 时进入 active
- `source 仍 down` 时保持 active
- `source up edge` 或 context 失效时退出 active

backend materialization：

- `ButtonEventBackend`：press on acquire, release on drop
- `KeyboardBackend`：key down on acquire, key up on drop

### Toggle

适用动作：

- `Sneak`（如果产品决定它是 toggle）
- 某些 UI 开关
- 某些 mod 模式开关

统一语义：

- 每次 `source down edge` 翻转一次 `logicalActive`
- `source release` 只负责 re-arm，不负责翻转

推荐注意点：

- `Toggle` 的核心是“逻辑翻转”，不是“打一发长一点的 pulse”
- 如果某个 backend 不能稳定表达持久 active，则应该在 policy 层把该 action 改成更合适的 contract，而不是 backend 私自把 `Toggle` 偷偷当 `Pulse`

### Repeat

适用动作：

- 菜单方向长按
- 翻页
- 列表滚动
- 某些 mod 连发动作

统一语义：

- `source down edge` 立即触发第一次 pulse
- 持续按住时，按统一 repeat 时钟生成后续 pulse
- `source up edge` 立刻取消排程

关键原则：

- repeat 时钟应该位于生命周期层
- 不应该依赖 backend 猜测“游戏自己会不会 repeat”

### Axis

适用动作：

- Move
- Look
- Trigger analog
- Touchpad analog / gesture-derived axis

统一语义：

- 每帧提交值
- backend 不拥有 tap/hold/toggle 语义

## contract 与 phase 的关系

推荐把 `contract` 和 `phase` 明确区分：

- `contract` 是动作类别
- `phase` 是这一帧发给 backend 的操作类型

推荐映射如下：

- `Pulse`
  - `phase = Pulse`
- `Hold`
  - `phase = Press / Hold / Release`
- `Toggle`
  - 如果 backend 支持持久状态：
    - `phase = Press / Release`
  - 如果 backend 只能表达离散事务：
    - 由 policy 明确把该 action 降级到 `Pulse`
- `Repeat`
  - `phase = Pulse`
- `Axis`
  - `phase = Value`

这里的重点是：

- backend 永远不要只看 `phase` 不看 `contract`
- 同一个 `phase = Pulse`，可能来自 `Pulse` 动作，也可能来自 `Repeat` 动作

## 推荐的统一接口

建议把当前 keyboard 侧的 `TriggerAction / SubmitActionState` 概念抽象成共享接口。

推荐接口：

```cpp
class IActionLifecycleSink
{
public:
    virtual ~IActionLifecycleSink() = default;

    virtual bool TriggerAction(
        std::string_view actionId,
        ActionOutputContract contract,
        InputContext context,
        std::uint32_t sourceCode,
        float heldSeconds) = 0;

    virtual bool SubmitActionState(
        std::string_view actionId,
        ActionOutputContract contract,
        bool pressed,
        float heldSeconds,
        InputContext context,
        std::uint32_t sourceCode) = 0;

    virtual bool SubmitAxisValue(
        std::string_view actionId,
        float valueX,
        float valueY,
        InputContext context,
        std::uint32_t sourceCode) = 0;
};
```

说明：

- `TriggerAction`
  - 只给离散事务型动作用
  - 典型是 `Pulse / Repeat pulse`
- `SubmitActionState`
  - 只给状态型动作用
  - 典型是 `Hold / Toggle`
- `SubmitAxisValue`
  - 给 `Axis`

这个接口可以由：

- `ButtonEventBackend`
- `KeyboardBackend`
- `PluginActionBackend`
- `ModEventBackend`

分别实现，但它们必须共享同一层 lifecycle 决策。

## 每帧处理顺序

建议统一成下面这条顺序：

1. 读取一帧 `PadEvent` 快照
2. 解析 binding，得到 `actionId`
3. 用 `ActionBackendPolicy` 决定 backend 与 contract
4. 更新 `ActionLifecycleState` 表
5. 生成 `FrameActionPlan`
6. 各 backend 消费自己名下的 `PlannedAction`
7. 输出 debug / trace

这里最重要的是第 4 步和第 5 步不能被 backend 私有化。

## ActionLifecycleState 的状态机规则

### Pulse

- 当 `sourceDown` 从 `false -> true`
  - `pendingPulseCount += 1`
- 当 `sourceDown` 保持为 `true`
  - 不再追加 pulse
- 当 `sourceDown` 从 `true -> false`
  - 仅更新源状态

### Hold

- 当 `sourceDown` 从 `false -> true`
  - `logicalActive = true`
  - 若 `deliveredActive == false`，生成 `Press`
- 当 `sourceDown == true`
  - 继续生成 `Hold` 或仅维持 active
- 当 `sourceDown` 从 `true -> false`
  - 若 `deliveredActive == true`，生成 `Release`
  - `logicalActive = false`

### Toggle

- 当 `sourceDown` 从 `false -> true` 且 `rearmReady == true`
  - `logicalActive = !logicalActive`
  - `rearmReady = false`
  - 生成一次状态变化对应的 phase
- 当 `sourceDown` 从 `true -> false`
  - `rearmReady = true`

### Repeat

- 当 `sourceDown` 从 `false -> true`
  - 立即追加一个 pulse
  - `nextRepeatAtSeconds = now + initialDelay`
- 当 `sourceDown == true` 且 `heldSeconds >= nextRepeatAtSeconds`
  - 追加一个 pulse
  - `nextRepeatAtSeconds += interval`
- 当 `sourceDown` 从 `true -> false`
  - 清空 repeat 排程

## backend 如何消费同一套模型

### ButtonEventBackend

这是未来 Skyrim 原生 PC 事件主线的推荐消费者。

它应当：

- 消费 `FrameActionPlan`
- 按 `PlannedAction` 的 `contract + phase + outputCode` 构造或投递 `ButtonEvent`
- 不再自己重新判断 tap/hold/repeat/toggle

它不应当：

- 直接从 mapping 结果旁路生成 `ButtonEvent`
- 在 backend 内再藏一套 `repeat / toggle / first-press` 规则
- 绕过 planner 改写 ownership

### KeyboardBackend

新的定位不是“Skyrim 原生 PC 事件主线”，而是：

- `ModEvent / VirtualKey / F13-F20`
- 低延迟键盘辅助输出
- 将来可能的某些非原生、非文字输入型 keyboard helper route

因此建议保留它，但把它视为“共享生命周期模型的一个 backend 实现”，而不是全局输入语义中心。

对 keyboard backend 的建议是：

- 继续保留低延迟、稳定、兼容性好的输出能力
- 继续保留必要的 key-state 调度
- 但把 `DesiredKeyboardState / transactional pulse / windowed pulse / readiness defer` 收拢成 backend 私有细节

### ModEventBackend

如果 mod 事件最终有一部分继续走虚拟键，也应该消费同样的 `Pulse / Hold / Toggle / Repeat`，而不是单独开一套映射逻辑。

### PluginActionBackend

插件动作通常是离散事务，但仍建议统一走 `contract`，至少保持：

- planner 日志一致
- ownership 一致
- 调试路径一致

## 哪些当前工作应保留

建议保留：

- [ActionOutputContract.h](/c:/Users/xuany/Documents/dualPad/src/input/backend/ActionOutputContract.h)
- [ActionBackendPolicy.h](/c:/Users/xuany/Documents/dualPad/src/input/backend/ActionBackendPolicy.h)
- [FrameActionPlan.h](/c:/Users/xuany/Documents/dualPad/src/input/backend/FrameActionPlan.h)
- [FrameActionPlanner.h](/c:/Users/xuany/Documents/dualPad/src/input/backend/FrameActionPlanner.h)
- `TriggerAction / SubmitActionState` 这组抽象语义
- 当前 planner / debug logger 的方向

保留原因：

- 它们表达的是 backend-neutral 动作语义
- 不是 keyboard-only 的偶然产物

## 哪些当前工作应下沉为 keyboard 私有实现

建议下沉或局部化：

- `DesiredKeyboardState`
- transactional / windowed pulse
- keyboard readiness / deferred replay
- UI canonical scancode 解析
- keyboard-specific state tables

这些都属于 keyboard backend 的“如何发”，而不是统一层的“动作是什么”。

## 对 ButtonEvent 方案制定助手的合并要求

如果后续由另一位助手制定 `ButtonEvent` 主线方案，建议明确要求它遵守以下兼容边界：

1. 不得绕过 `ActionBackendPolicy`
2. 不得绕过 `FrameActionPlanner`
3. 不得在 `ButtonEventBackend` 内重新实现一套独立 contract 语义
4. `ButtonEventBackend` 必须消费 `FrameActionPlan`
5. keyboard backend 仍应保留为独立 backend，不应被删除
6. keyboard backend 的用途要改写为：
   - mod 虚拟键
   - 辅助键盘输出
   - 低延迟 keyboard helper route
7. `Pulse / Hold / Toggle / Repeat / Axis` 仍是共享 contract 集合
8. 一个 action 在一次生命周期里只能被一个 backend 持有

## 推荐迁移方式

### 第一阶段

- 保留 `ActionOutputContract / ActionBackendPolicy / FrameActionPlan / FrameActionPlanner`
- 将 `ButtonEventBackend` 接入 plan 消费链
- 把 Skyrim 原生 PC 事件主线从 keyboard route 切到 `ButtonEventBackend`

### 第二阶段

- 把 keyboard backend 的 routing 收窄到 `ModEvent / VirtualKey / helper outputs`
- 将 keyboard-specific readiness / state delivery 全部视为 backend 私有实现

### 第三阶段

- 若未来引入新的 upstream native-state backend，也继续复用同一套 planner / lifecycle 模型

## 最终要达成的兼容形态

期望的最终形态是：

- 同一份 `PadEvent` 快照
- 同一套 binding / context 解析
- 同一套 `contract`
- 同一份 `FrameActionPlan`
- 多个 backend 各自 materialize

这样不同方案之间的差异就只剩下：

- 事件发往哪里
- 用什么底层形式发
- 哪个 backend 拥有哪类动作

而不会再变成：

- 每种方案都有一套自己的动作生命周期定义

## 一句话结论

接下来如果回到 `ButtonEvent` 主线，真正应该被当作“共用资产”保留下来的，是：

- `actionId -> contract -> backend` 这一层
- 统一的动作生命周期状态机
- `FrameActionPlan` 这类 backend-neutral 计划结构

而不是继续把 keyboard backend 的具体交付细节当作全局语义中心。
