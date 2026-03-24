# 统一动作生命周期模型

更新日期：2026-03-24

本文描述当前主线下仍然有效的统一动作生命周期合同。

重点不是某个 backend 如何发出去，而是 planner 如何先决定动作语义，再由 backend 消费这份结果。

## 一句话模型

统一链路应当是：

`输入触发器 -> 当前上下文 -> actionId -> contract -> lifecycle state machine -> FrameActionPlan -> backend dispatch`

关键点：

- `contract` 属于 `actionId`。
- backend 只消费 `FrameActionPlan`。
- lifecycle state machine 位于 backend 之上。

## 设计目标

- 让 native 数字动作与 keyboard helper 共享同一套高层语义合同。
- 避免不同 backend 各自重复实现 `Pulse / Hold / Repeat / Toggle`。
- 保证一个 stateful action 的整个生命周期只属于一个 owning backend。
- 保持 planner 作为唯一动作合同决定层。

## contract 属于 action，不属于物理按键

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
- `TouchpadPress -> ModEvent1 -> Pulse`

## 生命周期 ownership 规则

一条 stateful action 在整个生命周期里只能有一个 owning backend。

例如：

- `Sprint` 一次按住不能半途切给别的 backend。
- `MenuDown` 一次长按不能前半段走一套 repeat，后半段再切到另一套 repeat。

## 当前 planner / backend 分工

### planner 层负责

- 根据上下文和绑定把输入解析成 `actionId`
- 查 descriptor / policy 决定：
  - backend
  - contract
  - native control code 或 helper action
- 通过 `ActionLifecycleCoordinator` 显式产出 `Press / Hold / Repeat / Release / Pulse`

### backend 层负责

- `NativeButtonCommitBackend`
  - 将 planner 决定好的数字动作 materialize 为标准手柄硬件位
- `AxisProjection`
  - 将 planner 决定好的轴动作写入统一硬件状态
- `KeyboardHelperBackend`
  - 将 planner 决定好的 helper / ModEvent 动作转成 simulated keyboard

backend 不应重新解释动作合同。

## 当前主线里的 contract 形态

当前实际仍然需要的主要是：

- `Pulse`
- `Hold`
- `Repeat`
- `Toggle`（少量动作）
- `None`（不走生命周期合同的辅助路径）

其中：

- 原生手柄线重点依赖 `Pulse / Hold / Repeat`
- `ModEvent` 当前只做 `Pulse`

## 当前正式约束

- `FrameActionPlan` 是运行时合同，不是调试影子。
- `ActionLifecycleCoordinator` 是唯一高层 lifecycle 决策层。
- `NativeButtonCommitBackend` / `KeyboardHelperBackend` 都不应各自重新发明一套 contract 解释。
- 若后续新增 backend，也应继续消费同一份 planner-owned lifecycle 结果。