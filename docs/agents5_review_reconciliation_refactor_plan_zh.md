# agents5 审核意见对齐与下一轮重构计划

更新日期：2026-03-24

这份文档用于把最新一版 [agents5.md](/C:/Users/xuany/Documents/dualPad/agents5.md) 的深度研究建议，与当前仓库实际状态重新对齐，并据此给出下一轮重构计划。

本文只讨论当前正式主线，不再回到已经退役的实验路线。

适用前提：
- 当前正式架构见 [current_input_pipeline_zh.md](/C:/Users/xuany/Documents/dualPad/docs/current_input_pipeline_zh.md)
- 当前 backend ownership 见 [backend_routing_decisions.md](/C:/Users/xuany/Documents/dualPad/docs/backend_routing_decisions.md)
- 当前清理观察点见 [current_cleanup_risk_review_zh.md](/C:/Users/xuany/Documents/dualPad/docs/current_cleanup_risk_review_zh.md)

本轮结论主要基于当前代码与现行文档交叉核对即可完成，不需要额外新开 IDA 取证；只有在后续进入运行时语义或更底层 producer 行为调整时，才建议再做 MCP/IDA 验证。

## 一、结论摘要

最新一版 `agents5.md` 里的建议，需要分成三类看待：

1. 当前仍成立、而且值得继续进入重构计划的问题
2. 方向没错，但已经被当前主线部分吸收的问题
3. 已经过时、不能再按旧结论继续推进的问题

结合当前代码，当前还值得继续观察或后续收口的核心问题只剩 1 项：

1. `BindingResolver` 的 subset fallback 仍需继续观察

已经在当前分支完成并关闭的问题有 8 项：

1. `ItemYButton` descriptor 与 fallback 绑定错误
2. `KeyboardNativeBridge` 初始化失败后会永久失效
3. `PadEventSnapshotProcessor` / `UnmanagedDigitalPublisher` 热路径默认刷 info
4. `ActionBackendPolicy` 仍接受 `Mod.*` 前缀
5. Poll 窗口内无上界 drain
6. gap 处理仍是全域 reset
7. `XInputStateBridge` 在 bridge 层触发 commit
8. `ActionLifecycleBackend` 单实现薄接口

## 二、已经过时或已被吸收的建议

下面这些建议在旧快照里成立，但在当前代码里已经不再是“待完成重构项”：

### 1. native action 元数据分散

当前 native action 的主元数据已经集中到：
- [NativeActionDescriptor.h](/C:/Users/xuany/Documents/dualPad/src/input/backend/NativeActionDescriptor.h)
- [NativeActionDescriptor.cpp](/C:/Users/xuany/Documents/dualPad/src/input/backend/NativeActionDescriptor.cpp)

`ActionBackendPolicy`、native button materialization、axis projection 当前都共享这张主表。

### 2. `PadEventSnapshotProcessor` 仍是完全未拆分的 God object

当前已经拆出：
- [AxisProjection.h](/C:/Users/xuany/Documents/dualPad/src/input/injection/AxisProjection.h)
- [AxisProjection.cpp](/C:/Users/xuany/Documents/dualPad/src/input/injection/AxisProjection.cpp)
- [UnmanagedDigitalPublisher.h](/C:/Users/xuany/Documents/dualPad/src/input/injection/UnmanagedDigitalPublisher.h)
- [UnmanagedDigitalPublisher.cpp](/C:/Users/xuany/Documents/dualPad/src/input/injection/UnmanagedDigitalPublisher.cpp)

所以它已经不是旧版那种“所有事情都塞在一个文件里”的状态，但 orchestration 责任仍然偏重，后续仍值得继续收口。

### 3. `NativeButtonCommitBackend` 仍保留旧 lifecycle bridge 入口

当前 [NativeButtonCommitBackend.h](/C:/Users/xuany/Documents/dualPad/src/input/backend/NativeButtonCommitBackend.h) 已只保留：
- `BeginFrame()`
- `ApplyPlannedAction()`
- `CommitPollState()`
- `Emit()`

旧的 `TriggerAction()` / `SubmitActionState()` 入口已经不在当前 backend 中。

### 4. `ActionDispatcher` 对 `KeyboardHelper` 仍有多层厚包装

当前 [ActionDispatcher.cpp](/C:/Users/xuany/Documents/dualPad/src/input/ActionDispatcher.cpp) 已压平成单一 helper dispatch 路径，不再是旧版多层 lifecycle wrapper 结构。

### 5. `AuthoritativePollState` 仍预存 XInput transport 派生字段

当前 transport 派生字段已经移到：
- [XInputButtonSerialization.h](/C:/Users/xuany/Documents/dualPad/src/input/XInputButtonSerialization.h)
- [XInputButtonSerialization.cpp](/C:/Users/xuany/Documents/dualPad/src/input/XInputButtonSerialization.cpp)
- [XInputStateBridge.cpp](/C:/Users/xuany/Documents/dualPad/src/input/XInputStateBridge.cpp)

`AuthoritativePollState` 已回到“虚拟手柄硬件状态”这一角色。

### 6. `KeyboardHelperBackend` 的 helper-key 解析仍是长 if 链

当前 [KeyboardHelperBackend.cpp](/C:/Users/xuany/Documents/dualPad/src/input/backend/KeyboardHelperBackend.cpp) 已使用：
- `kFunctionKeyPoolEntries`
- `kVirtualKeyPoolEntries`
- `FindHelperKeyScancode()`

也就是表驱动解析，而不是旧的长条件链。

## 三、当前仍成立的问题

### 1. `BindingResolver` 的 subset fallback 仍需继续观察

当前 [BindingResolver.cpp](/C:/Users/xuany/Documents/dualPad/src/input/mapping/BindingResolver.cpp) 里仍有：
- `kAllowFallbackWithoutModifiers = true`

这不是当前必须立刻动手的 correctness 问题，但随着组合键面继续扩大，后续需要再次评估。

## 四、当前已完成的修复

### Phase 1：正确性与路由可用性修复

这一阶段已经在当前分支完成。

完成内容：

1. 修 `ItemYButton`
- [NativeActionDescriptor.cpp](/C:/Users/xuany/Documents/dualPad/src/input/backend/NativeActionDescriptor.cpp)
  - `ItemYButton` 已改为 `VirtualPadButtonRoleTriangle`
- [BindingManager.cpp](/C:/Users/xuany/Documents/dualPad/src/input/BindingManager.cpp)
  - `ItemYButton` fallback 已改为 `bits.triangle`
  - `ItemZoom` 继续保留 `bits.r3`

2. 修 `KeyboardNativeBridge` 初始化重试
- [KeyboardNativeBridge.h](/C:/Users/xuany/Documents/dualPad/src/input/backend/KeyboardNativeBridge.h)
- [KeyboardNativeBridge.cpp](/C:/Users/xuany/Documents/dualPad/src/input/backend/KeyboardNativeBridge.cpp)
  - 去掉一次失败后永久锁死的 `_initAttempted` 路径
  - 加入 `ReleaseResources()`，允许后续重试

3. 把热路径 info 日志纳入 `RuntimeConfig` gate
- [PadEventSnapshotProcessor.cpp](/C:/Users/xuany/Documents/dualPad/src/input/injection/PadEventSnapshotProcessor.cpp)
  - mapping/disptach/blocked-buttons 日志改为按开关输出
- [UnmanagedDigitalPublisher.cpp](/C:/Users/xuany/Documents/dualPad/src/input/injection/UnmanagedDigitalPublisher.cpp)
  - raw digital pulse 日志改为按开关输出

4. 收紧 `ModEvent` action surface
- [ActionBackendPolicy.cpp](/C:/Users/xuany/Documents/dualPad/src/input/backend/ActionBackendPolicy.cpp)
  - `IsModEventActionId()` 现在只接受 `ModEventKeyPool` 中实际存在的固定槽位
  - `Mod.*` 已不再被当作正式 mod action 路由

本阶段验收结果：
- ItemMenu 下 `ItemZoom / ItemXButton / ItemYButton` 物理位已各自独立
- helper bridge 初始化失败后允许后续重试
- 默认配置下不再刷热路径 info
- `Mod.` 已不再进入正式 `ModEvent` 路由

## 五、本轮不建议推进的方向

下面这些不建议重新作为主重构方向推进：

- 重新引回 `keyboard-native` 作为 Skyrim 原生控制主线
- 重新引回 native button splice / queue surgery
- 重新扩大已撤出的：
  - `OpenInventory`
  - `OpenMagic`
  - `OpenMap`
  - `OpenSkills`
- 主动重构 `ControlMapOverlay`
  - 当前它已经收口成走原生 parser / rebuild 的稳定实现
  - 除非出现新的 crash 或行为问题，否则不应优先动它

## 六、下一轮重构计划

### Phase 2：Poll 窗口收口

状态：已完成

目标：
1. 把 drain 从无上界改成有界 drain
2. backlog 场景显式 coalescing
3. 降低 Poll 单次最坏工作量

具体动作：
- [PadEventSnapshotDispatcher.cpp](/C:/Users/xuany/Documents/dualPad/src/input/injection/PadEventSnapshotDispatcher.cpp)
  - 增加 bounded drain 版本
  - 每个 Poll 最多处理固定数量 snapshot
  - 剩余 snapshot 采用“保最新 + 标 coalesced”方式处理
- [PadEventSnapshotProcessor.cpp](/C:/Users/xuany/Documents/dualPad/src/input/injection/PadEventSnapshotProcessor.cpp)
  - 保持现有 `overflowed / coalesced / sourceTimestampUs` 发布语义

### Phase 3：gap 处理改成分域 resync

状态：已完成

目标：
1. 去掉全域 reset 的过大故障域
2. native gap 不再默认重置 keyboard helper 域

具体动作：
- [PadEventSnapshotProcessor.cpp](/C:/Users/xuany/Documents/dualPad/src/input/injection/PadEventSnapshotProcessor.cpp)
  - 用更窄的 native-domain resync 替代全局 `ResetState()`

### Phase 4：bridge 纯序列化

状态：已完成

目标：
1. 把 commit 时机从 bridge 内部移出
2. 让 `XInputStateBridge` 只做 transport 序列化

具体动作：
- [UpstreamGamepadHook.cpp](/C:/Users/xuany/Documents/dualPad/src/input/injection/UpstreamGamepadHook.cpp)
  - 显式组织 drain / commit / fill synthetic state 的顺序
- [XInputStateBridge.cpp](/C:/Users/xuany/Documents/dualPad/src/input/XInputStateBridge.cpp)
  - 移除 bridge 内部 `CommitPollState()` 调用

### Phase 5：低风险精简

状态：已基本完成

目标：
1. `ActionLifecycleBackend` 已移除
2. `PollCommitCoordinator` 已去掉 `Cooldown / nextRepeatAtUs`
3. `NativeControlCode` 字符串化已并回 descriptor 主表实现
4. `PollCommitCoordinator` 的 `ToggleFlip / toggledOn / suppressedPulseCount` 残留已移除

## 七、优先级建议

当前剩余优先级：
1. 继续评估 `BindingResolver` 的 subset fallback

原因：
- Phase 1 到 Phase 4 已完成
- 当前已没有更高优先级的架构边界问题

## 八、这轮不需要额外 IDA 的原因

这轮计划里的问题主要是：
- 代码职责边界
- drain/work budget
- gap/resync 故障域
- bridge 与 commit 的时机边界
- 低风险抽象清理

这些都可以直接由当前代码与现行文档确认。

只有在后续需要继续改变：
- 原生 producer 行为假设
- `ControlMap` 解析语义
- 某个 handler 家族的游戏内消费规则

时，才建议再补一轮 IDA/MCP 取证。
