# agents5 审核意见对齐与重构计划

这份文档的目的不是重复 `agents5.md`，而是把那份深度研究意见和**当前仓库真实状态**对齐，区分：

- 哪些判断已经被当前主线吸收，不能再按旧问题处理
- 哪些风险在当前代码里仍然成立
- 下一轮重构应该按什么顺序推进

适用范围：

- 当前仓库 `refactor-input` 主线
- 当前正式架构口径见：
  - [current_input_pipeline_zh.md](current_input_pipeline_zh.md)
  - [backend_routing_decisions.md](backend_routing_decisions.md)
  - [current_cleanup_risk_review_zh.md](current_cleanup_risk_review_zh.md)

## 一、结论摘要

`agents5.md` 的大方向有价值，但它审查时基于的是一个**更旧的主线快照**。因此其中不少“严重问题”在当前代码里已经不再成立，或者已经被重构吸收。

对当前分支真正还成立、且值得继续推进的点，主要是 6 项：

1. 原生 action 元数据仍然分散在多处，应该收成统一 descriptor/table
2. `PadEventSnapshotProcessor` 仍然过大，继续承担过多 orchestration 责任
3. `NativeButtonCommitBackend` 仍保留旧 `IActionLifecycleBackend` 适配入口
4. `ActionDispatcher` 对 `KeyboardHelper` 仍有一层偏重包装
5. `AuthoritativePollState` 仍带有一部分 XInput transport 派生字段
6. `KeyboardHelperBackend` 的 helper-key 到 DIK 解析仍是长 `if` 链

当前推荐的重构策略不是再做“大改路线”，而是：

- 保持现有两条正式输出线不变
- 保持 `AuthoritativePollState` 单一最终状态不变
- 继续做**删除旧适配层、集中元数据、拆分大 orchestrator、收窄 transport 耦合** 这类收口

## 一点五、当前落地状态

截至当前分支，这份计划里的主阶段已经完成：

1. `NativeButtonCommitBackend` 的旧 lifecycle bridge 已移除
2. `ActionDispatcher` 的 `KeyboardHelper` 分发已压平成单一路径
3. `NativeActionDescriptor` 已成为 native routing / axis projection / button materialization 的统一主表
4. `PadEventSnapshotProcessor` 已拆出：
   - `AxisProjection`
   - `UnmanagedDigitalPublisher`
5. `AuthoritativePollState` 已移除 `xinputButtons/xinputPressed/xinputReleased/xinputPulse` 这类 transport 派生字段
6. `KeyboardHelperBackend` 的 helper-key -> DIK 解析已改成表驱动

当前剩余的内容不再属于这份计划里的“必须完成重构项”，而更接近后续可选优化。

## 二、哪些意见已经过时

下面这些意见，在当前代码里已经不能按 `agents5.md` 原样成立。

### 1. “仍有两套权威状态来源”

这条在旧版本成立，但在当前主线已经被吸收。

当前正式口径已经是：

- `NativeButtonCommitBackend + PadEventSnapshotProcessor`
- `-> AuthoritativePollState`
- `-> XInputStateBridge`

旧的 `SyntheticPadState / CompatibilityInputInjector / VirtualGamepadState / NativeStateBackend` 已经退出默认主线，不再是当前运行时的并行权威状态来源。

因此，当前真正的问题不是“双权威状态”，而是：

- `AuthoritativePollState` 还可以继续瘦身
- 上游 native 元数据和 orchestration 还可以继续集中

### 2. “OpenInventory / OpenMagic / OpenMap / OpenSkills 仍在正式支持面”

这条已经过时。

这批动作已经撤出当前正式支持面，不应再按旧报告继续当成现役问题处理。当前更合理的做法是：

- 继续避免把它们重新引回正式路由
- 只保留文档里的历史/非目标说明

### 3. “存在 DispatchDirectPadEvent 这类 planner 旁路”

这条也已经过时。

当前代码里已经没有 `DispatchDirectPadEvent()` 这类 touchpad 直通 compatibility pulse 的主线旁路。围绕 touchpad/plugin/mod 的正式路径也已经收敛到当前 action/routing 模型里。

### 4. “NativeInputPreControlMapHook / consumer-side native button splice 仍是活跃主线”

这条已经过时。

旧 `NativeInputPreControlMapHook` 及相关实验代码已经从默认工程目标移除，只保留历史文档与逆向参考意义。

### 5. “ControlMapOverlay 仍是 pre-controlmap 实验路线”

这条在当前代码里也不成立。

当前 `ControlMapOverlay` 已经不是旧 consumer-side 输入实验，而是：

- `kDataLoaded` 后
- 读取 `Data/SKSE/Plugins/DualPadControlMap.txt`
- 直接调用游戏自己的 `ControlMap` parser/rebuild 链

所以它当前是正式运行时的一部分，不应再按“旧实验 hook”处理。

## 三、当前仍成立的审查意见

下面这些点，是把 `agents5.md` 和当前代码对齐后，仍然值得保留的有效结论。

### 1. 原生 action 元数据分散

这仍然是当前最值得优先收口的问题。

同一件事目前至少散在这些位置：

- [src/input/backend/ActionBackendPolicy.cpp](../src/input/backend/ActionBackendPolicy.cpp)
  - `TryMapButtonEventButton`
  - `TryMapNativeAxis`
  - `IsTriggerAxisCode`
- [src/input/injection/PadEventSnapshotProcessor.cpp](../src/input/injection/PadEventSnapshotProcessor.cpp)
  - `ResolveAxisAction`
  - `ApplyAxisBinding`
- [src/input/backend/NativeButtonCommitBackend.cpp](../src/input/backend/NativeButtonCommitBackend.cpp)
  - native button materialization
- [src/input/backend/NativeControlCode.h](../src/input/backend/NativeControlCode.h)
  - native code 定义和字符串化

风险：

- 新增一个 native action 往往要改 3 到 5 处
- 同一 action 的“路由、分组、硬件 materialization、日志名”容易漂移
- 当前 `controlmap` 上下文事件面继续扩张时，维护成本会进一步上升

### 2. `PadEventSnapshotProcessor` 仍然太大

当前它仍承担过多职责：

- 规划 orchestration
- axis action 解析
- analog 发布
- unmanaged raw digital 发布
- source block 协调
- synthetic frame metadata 发布

尤其这几段最典型：

- [src/input/injection/PadEventSnapshotProcessor.cpp](../src/input/injection/PadEventSnapshotProcessor.cpp)
  - `PublishUnmanagedDigitalState`
  - `ResolveAxisAction`
  - `ApplyAxisBinding`

这会让后续任何 native action/routing 调整都继续挤进同一个文件。

### 3. `NativeButtonCommitBackend` 仍有旧 lifecycle bridge 入口

当前主路已经是 planner-owned：

- `ActionDispatcher`
- `-> NativeButtonCommitBackend::ApplyPlannedAction()`

但 backend 本身还保留了旧接口：

- [src/input/backend/NativeButtonCommitBackend.h](../src/input/backend/NativeButtonCommitBackend.h)
  - `TriggerAction`
  - `SubmitActionState`
- [src/input/backend/NativeButtonCommitBackend.cpp](../src/input/backend/NativeButtonCommitBackend.cpp)
  - `Legacy bridge path for IActionLifecycleBackend callers`

这说明当前仍有一层“旧适配模型”没有彻底拔干净。

### 4. `ActionDispatcher` 对 KeyboardHelper 仍有偏重包装

当前原生数字路线已经走：

- `ActionDispatcher`
- `-> NativeButtonCommitBackend::ApplyPlannedAction()`

而 `KeyboardHelper` 仍使用：

- `TryDispatchLifecycleBackend`
- `TryDispatchLifecycleBackendState`
- `TryDispatchPlannedLifecycleBackend`

这三层泛化包装当前主要只服务：

- `KeyboardHelper`
- `ModEvent -> KeyboardHelper`

因此已经有点“为了抽象而抽象”。

### 5. `AuthoritativePollState` 仍带有 transport 派生字段

当前它不仅保存统一硬件状态，还预计算了：

- `xinputButtons`
- `xinputPressedButtons`
- `xinputReleasedButtons`
- `xinputPulseButtons`

见：

- [src/input/AuthoritativePollState.h](../src/input/AuthoritativePollState.h)
- [src/input/AuthoritativePollState.cpp](../src/input/AuthoritativePollState.cpp)

而当前真正消费这些派生字段的主要只有：

- [src/input/XInputStateBridge.cpp](../src/input/XInputStateBridge.cpp)
- debug logger

这和“`AuthoritativePollState` 代表虚拟手柄硬件状态，bridge 只做序列化”的目标之间还存在一点耦合。

### 6. `KeyboardHelperBackend` 的 helper-key DIK 解析仍是长条件链

当前：

- [src/input/backend/KeyboardHelperBackend.cpp](../src/input/backend/KeyboardHelperBackend.cpp)
  - `ResolveFunctionKeyPoolScancode`
  - `ResolveVirtualKeyPoolScancode`

但 `ModEvent` 键池已经是 data-table 风格：

- [src/input/backend/ModEventKeyPool.h](../src/input/backend/ModEventKeyPool.h)
  - `kModEventKeySlots`

这说明 helper 键池这层还有继续数据驱动化的空间。

## 四、当前不建议优先处理的点

### 1. `ControlMapOverlay`

当前它已经从“危险实验实现”收口成“走游戏原生 parser/rebuild 的运行时 overlay”。

只要没有新的 crash 或行为问题，短期内不建议把清理精力先投在这里。

### 2. `InputModalityTracker`

它现在职责虽然也不小，但边界已经比较明确：

- 只负责 KBM/gamepad 并存
- 只负责 UI platform 切换
- 不接管动作 routing

所以短期内不应优先重构。

### 3. 再做大的主线改道

当前主线已经完成了：

- `AuthoritativePollState`
- runtime `ControlMap` overlay
- `KeyboardHelperBackend` 收口
- combo-native 与 mod-key 两条线拆分

所以接下来更值得做的是**收口与精简**，而不是再次改主线方向。

## 五、重构目标

下一轮重构应只围绕 4 个目标展开：

1. 降低 native action 元数据重复
2. 缩小 orchestration god object
3. 拔掉已经不再承担正式职责的旧适配接口
4. 让 `AuthoritativePollState` 更接近“硬件状态对象”，而不是 transport 预处理结果

非目标：

- 不重新引回旧 compatibility 主线
- 不重新扩大已撤出的 action surface
- 不重新引入 keyboard-native 作为 Skyrim 原生事件主路
- 不为了“更抽象”而再加一层新框架

## 六、分阶段计划（已完成）

### Phase 1：删除 `NativeButtonCommitBackend` 的旧 lifecycle bridge

目标：

- 去掉 `TriggerAction`
- 去掉 `SubmitActionState`
- 去掉对应 legacy path 注释和适配逻辑

前提：

- 确认当前运行时 native 路由只走 `ApplyPlannedAction()`
- 确认没有其它遗留调用点

收益：

- 去掉一套已过时的入口合同
- backend 职责更单纯

当前状态：已完成。

### Phase 2：压平 `ActionDispatcher` 的 KeyboardHelper 包装层

目标：

- 把目前三层 lifecycle wrapper 收成更直接的 `DispatchKeyboardHelperAction()` 风格
- `ModEvent -> helper key` 与直接 `KeyboardHelper` 共用同一条窄路径

收益：

- dispatcher 更容易读
- “native 直 dispatch / keyboard 间接 dispatch” 的结构差异更清楚

当前状态：已完成。

### Phase 3：建立统一 `NativeActionDescriptor` 表

目标：

- 用一张 descriptor table 描述：
  - `actionId`
  - backend
  - native control code
  - kind(button/axis/trigger)
  - materialized xinput bit/axis
  - 可选日志名/调试名

优先替换：

- `ActionBackendPolicy` 的 native button/axis 分类
- `PadEventSnapshotProcessor` 的 axis 分流
- `NativeButtonCommitBackend` 的 native materialization
- `NativeControlCode` 的字符串化辅助

收益：

- 新增一个 native action 只改一处主表
- 减少“同一动作多处写死”的漂移风险

当前状态：已完成。当前主表文件是 [src/input/backend/NativeActionDescriptor.h](../src/input/backend/NativeActionDescriptor.h) 和 [src/input/backend/NativeActionDescriptor.cpp](../src/input/backend/NativeActionDescriptor.cpp)。

### Phase 4：拆分 `PadEventSnapshotProcessor`

目标：

- 保留它作为总 orchestrator
- 但把至少两类职责拆出去：
  - `AxisProjection`
  - `UnmanagedDigitalPublisher`

推荐方向：

- `PadEventSnapshotProcessor`
  只负责：
  - `Reduce`
  - `Plan`
  - `Dispatch`
  - 汇总 publish
- 具体 axis/raw digital 写入逻辑拆成小模块

收益：

- 当前主线更容易维护
- `PadEventSnapshotProcessor` 不再继续膨胀

当前状态：已完成。当前拆出的模块是 [src/input/injection/AxisProjection.h](../src/input/injection/AxisProjection.h) / [src/input/injection/AxisProjection.cpp](../src/input/injection/AxisProjection.cpp) 与 [src/input/injection/UnmanagedDigitalPublisher.h](../src/input/injection/UnmanagedDigitalPublisher.h) / [src/input/injection/UnmanagedDigitalPublisher.cpp](../src/input/injection/UnmanagedDigitalPublisher.cpp)。

### Phase 5：收窄 `AuthoritativePollState`

目标：

- 评估是否把 `xinputButtons/xinputPressed/xinputReleased/xinputPulse`
  从状态层移回 bridge/logger 层

原则：

- `AuthoritativePollState` 保留硬件事实
- transport 派生结果尽量留在 transport

注意：

- 这一步优先级低于前 4 步
- 如果 debug logger 仍明显受益于这些字段，也可以暂时保留

当前状态：已完成。XInput 按钮序列化已移到 [src/input/XInputButtonSerialization.h](../src/input/XInputButtonSerialization.h) / [src/input/XInputButtonSerialization.cpp](../src/input/XInputButtonSerialization.cpp)，由 bridge/logger 按需计算。

### Phase 6：把 helper-key 解析改成表驱动

目标：

- 用 `constexpr` table 替代 `KeyboardHelperBackend` 里的长 `if` 链

范围：

- `FKey.*`
- `VirtualKey.*`

收益：

- helper 键池扩展成本更低
- 更符合当前 `ModEventKeyPool` 的表驱动风格

当前状态：已完成。

## 七、原建议执行顺序

建议按这条顺序推进：

1. `NativeButtonCommitBackend` legacy 接口清理
2. `ActionDispatcher` keyboard helper 包装压平
3. `NativeActionDescriptor` 主表建设
4. `PadEventSnapshotProcessor` 拆分
5. `AuthoritativePollState` 瘦身
6. `KeyboardHelperBackend` 表驱动化

原因：

- 前 2 步收益大、风险小
- 第 3 步能为后续两步清理打基础
- 第 5、6 步更像结构优化而非功能风险修复

## 八、验收标准

完成上述阶段后，至少应满足：

- `AuthoritativePollState -> XInputStateBridge` 行为不变
- 当前已验证可用的：
  - 原生手柄 current-state
  - `Pause`
  - `NativeScreenshot`
  - `ModEvent`
  不发生回归
- `xmake build -j 1 DualPad` 通过
- debug 日志仍能提供足够诊断信息
- README / 架构文档与运行时代码口径保持一致

当前状态：已满足。

## 九、后续可选优化

当前这份计划完成后，后续更适合考虑的是：

1. 继续减少 `NativeControlCode::ToString()` 与 descriptor 表之间的元数据重复
2. 评估是否把更多 debug-only 派生信息从运行时热路径推迟到日志侧计算
3. 针对 `ControlMapOverlay` 与 combo-native 事件继续做小范围验证，而不是再改主线方向
