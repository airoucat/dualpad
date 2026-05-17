# Phase 7 切片：重做 `IngressHub` / `FrameAssembler` / resync

本 slice 对应重构阶段里的 Phase 7，执行上直接承接：

- [docs/plans/dualpad_rearchitecture/06_slice_phase5_gameplay_projection_zh.md](06_slice_phase5_gameplay_projection_zh.md)
- `docs/plans/dualpad_rearchitecture/07_slice_phase6_prompt_projection_zh.md`

本文件的目的不是重复“为什么要重做 dispatcher”，而是把 Phase 7 需要冻结的工程合同一次性定清，让开发按顺序推进，不在 `work` 阶段临时决定：

- `IngressHub` 到底负责什么，不负责什么
- `FrameAssembler` 如何做 coalescing，何时禁止 merge
- boundary key 的精确定义
- transition frame 何时产出，产出后下游必须怎么处理
- sequence gap / overflow / explicit reset 如何统一进入 recovery
- native backend 与 helper backend 如何在同一恢复边界上同步 reset

## 目标

- 把当前 `PadEventSnapshotDispatcher -> PadEventSnapshotProcessor -> legacy coalescing / ad-hoc resync` 的链，切换成 `IngressHub -> FrameAssembler -> DualPadRuntime` 的单向入口。
- 冻结 “原始事件进入 runtime” 的唯一 contract，禁止后续 slice 再往 runtime shell、backend 或 projection executor 里补一套平行 coalescing。
- 解决跨边界 merge 导致的丢边沿、吞 transition、menu/context 切换残留、config reload 残留以及 backlog 下行为不确定问题。
- 把 sequence gap、queue overflow、explicit reset、manifest swap 全部收口到统一的 transition / recovery 语义，并把它们稳定映射给 Phase 5 已冻结的 `GameplayRecoveryInput -> RecoveryPlan` 链。
- 确保 native 输出面与 helper 输出面在 recovery 时使用同一帧、同一 reason、同一 reset 顺序，不允许出现一边已 reset、另一边仍沿用旧 held 状态的半恢复状态。

本 slice 明确不做下面这些事：

- 不改 `GameplayProjectionFrame`、`GatePlan`、`RecoveryPlan` 的结构；这些在 Phase 5 已冻结。
- 不改 `PromptProjection / PromptService` 的 prompt 解析规则；Phase 7 只负责让它们拿到正确的 frame 边界。
- 不把 `IngressHub` 变成业务决策层；它仍然只是 transport + ordering 层。
- 不重写 `InputKernel`、`InteractionEngine`、`PresentationProjection` 的内部算法；本 slice 只冻结它们的 ingress 边界。

## 冻结的设计决定

### 1. `IngressHub` 的职责边界现在固定

`IngressHub` 是唯一允许接收跨线程输入生产者的入口。它的职责固定为：

- 给所有 ingress 事件分配全局单调递增 `seq`
- 记录 `monotonicUs`
- 用单消费者队列把事件按提交顺序搬运到 runtime
- 在发现 queue overflow / producer gap / explicit reset 时注入健康标记事件
- 不做 coalescing
- 不做 gameplay / UI / prompt / owner 决策
- 不直接调用 backend

固定输入事件族如下：

```cpp
enum class IngressKind : std::uint8_t
{
    PadSnapshot,
    UiSnapshot,
    HostFacts,
    SourceEvidence,
    ManifestEpochChanged,
    DeviceFamilyChanged,
    ExplicitReset,
    QueueOverflow,
    SequenceGap
};

struct IngressEvent {
    std::uint64_t seq{ 0 };
    std::uint64_t monotonicUs{ 0 };
    IngressSource source{};
    IngressKind kind{};
    Payload payload{};
};
```

固定规则如下：

1. `IngressHub` 只负责“有序搬运”，不负责解释 `payload`。
2. 任何 producer 都不允许直接改 runtime 当前状态；它们只能 enqueue `IngressEvent`。
3. `QueueOverflow` 和 `SequenceGap` 不是日志提示，而是正式 ingress 事件；它们必须进入 `FrameAssembler`。
4. `ManifestEpochChanged`、`DeviceFamilyChanged`、`UiSnapshot` 都属于 boundary 可能变化的一级事件；它们不能在 hub 里被吞掉。
5. hub 内允许为 replaceable 事件保留“最后一份快照”缓存，供 overflow 之后重建首帧，但不允许直接把缓存当 published state。

### 2. `FrameAssembler` 是唯一允许做 coalescing 的层

`FrameAssembler` 的职责固定为：把 `IngressHub` drain 出来的 `IngressEvent` 序列组装成 runtime 可消费的不可变 frame。

它的固定输入只有两类：

- 按 `seq` 排序的 `IngressEvent` 序列
- 上一份 assembler 持久状态

它的固定输出只有两类：

- `Stable` frame
- `Transition` frame

### 1A. `ManifestEpochChanged` / `DeviceFamilyChanged` 现在是 revision 的唯一 authoritative marker

本页明确保留显式 marker 事件，而且把它们升级成 `manifestEpoch` 与 `deviceFamilyRevision` 的唯一 authoritative 边界来源；snapshot 只能配对、镜像、验证，不能再独立切 boundary。

建议固定 payload：

```cpp
struct ManifestEpochChangedPayload {
    std::uint32_t manifestEpoch;
};

struct DeviceFamilyChangedPayload {
    InputDeviceFamily family;
    std::uint32_t deviceFamilyRevision;
};
```

冻结规则如下：

1. `ManifestEpochChanged`
   - producer 固定为 `ActionManifestPublisher` / manifest hot-reload controller。
   - enqueue 顺序固定为：
     1. 先完成新 `CompiledActionGraph` 的 staging。
     2. enqueue `ManifestEpochChangedPayload{ newEpoch }`。
     3. 只有 marker 已成功入队，runtime 才允许让下一份 `Stable` frame 消费新 graph。
   - 它没有第二条 ingress snapshot 配对路径；marker payload 自身就是 `manifestEpoch` 的完整来源。
   - 如实现试图只切 active graph 指针而不发 marker，属于合同违例；新 graph 不得进入 authoritative path。
2. `DeviceFamilyChanged`
   - producer 固定为 `DeviceFamilyIngressPublisher -> SourceEvidenceCollector` 这条已发布链。
   - enqueue 顺序固定为：
     1. 先产出将要发布的 `SourceEvidenceSnapshot`。
     2. 若 `deviceFamilyRevision` 变化，先 enqueue `DeviceFamilyChangedPayload{ family, newRevision }`。
     3. 紧接着 enqueue 携带同一 `deviceFamilyRevision` 的 `IngressEvent(kind = SourceEvidence)`。
   - `DeviceFamilyChanged` marker 必须携带变化后的新 revision；禁止只发“发生变化了”而不带新值。
3. assembler 消费顺序固定为：
   - 先按 `seq` 消费 marker。
   - marker 一旦改变 boundary，就先 flush 边界前最后一份 `Stable` frame，再发 `Transition`。
   - 只有在 marker 已被消费且配对要求满足后，才允许发边界后的第一份 `Stable` frame。
4. 配对规则固定为：
   - `ManifestEpochChanged`：无额外 snapshot 配对要求；marker 自己即配对完成。
   - `DeviceFamilyChanged`：必须与其后第一份 `SourceEvidence.deviceFamilyEvidence.deviceFamilyRevision == marker.deviceFamilyRevision` 的 snapshot 配对。
5. fail-closed 行为固定为：
   - 如果 `SourceEvidence` snapshot 先到了、revision 已变，但前面没有匹配的 `DeviceFamilyChanged` marker，assembler 不得从 snapshot 直接切 boundary；必须拒绝该 snapshot 进入新的 `Stable` frame，置位 `FactHealth.boundaryMarkerMismatch = true`，并走 `ExplicitReset -> HardReset`。
   - 如果 `DeviceFamilyChanged` marker 已到，但后续第一份 `SourceEvidence` snapshot revision 不匹配，assembler 不得“就近取值”；必须置位 `FactHealth.pendingBoundaryMarkerPair = true` 与 `boundaryMarkerMismatch = true`，并走 `ExplicitReset -> HardReset`。
   - 如果 `DeviceFamilyChanged` marker 已到但直到下一个 boundary / health 事件前都没有等到配对 snapshot，assembler 不得发布该新 boundary 的第一份 `Stable` frame；必须继续保持 pending，并在后续 `ExplicitReset` 或 recovery 后重新起边界。

```cpp
enum class AssembledFrameKind : std::uint8_t
{
    Stable,
    Transition
};

struct IngressBoundaryKey {
    std::uint32_t manifestEpoch{ 0 };
    std::uint32_t contextRevision{ 0 };
    std::uint32_t menuStackRevision{ 0 };
    std::uint32_t deviceFamilyRevision{ 0 };
};

enum class TransitionReason : std::uint8_t
{
    ManifestEpochChanged,
    BoundaryKeyChanged,
    SequenceGap,
    QueueOverflow,
    ExplicitReset
};

struct TransitionFrameMeta {
    IngressBoundaryKey from{};
    IngressBoundaryKey to{};
    TransitionReason reason{ TransitionReason::BoundaryKeyChanged };
    bool requestSoftResync{ false };
    bool requestHardResync{ false };
    bool flushPendingPulseEdges{ false };
};

struct AssembledFactFrame {
    AssembledFrameKind kind{ AssembledFrameKind::Stable };
    std::uint64_t firstSeq{ 0 };
    std::uint64_t lastSeq{ 0 };
    IngressBoundaryKey boundaryKey{};
    FactFrame facts{};
    TransitionFrameMeta transition{};
};
```

冻结规则如下：

1. 只有 `FrameAssembler` 允许 merge ingress 事件。
2. `DualPadRuntime`、`InputKernel`、`PollOutputAdapter`、legacy dispatcher 都不允许再做第二层 coalescing。
3. `Stable` frame 必须包含一个完整且自洽的 `FactFrame` 视图。
4. `Transition` frame 不是日志，也不是旁路通知；它是 runtime 主循环必须消费的一等 frame。
5. assembler 内部允许维护 replaceable latest-state cache，但最终对外只能发布不可变 frame。
6. `FrameAssembler` 只发布 `AssembledFactFrame`；`AssembledFactFrame -> KernelFrame` 的唯一 builder 固定为 `src/input_v2/kernel/InputKernel.*` 中的 `BuildKernelFrame(const AssembledFactFrame&)`，不允许 runtime shell、projection 或 backend 私自再做第二个 builder。

### 3. boundary key 在 Phase 7 冻结为 4 个字段

本 slice 现在明确：boundary key 只由下面 4 个字段组成，后续 slice 不允许再偷偷补“隐形第五维”：

1. `manifestEpoch`
   - 唯一 authoritative 来源固定为 `IngressEvent(kind = ManifestEpochChanged).payload.manifestEpoch`
   - `AssembledFactFrame.facts.manifestEpoch` 只能镜像该 marker payload，不允许从 live `ActionManifest`、active graph 指针或其它 published state 直接取值
2. `contextRevision`
   - 来自 Phase 2 已发布字段 `ResolvedContextSnapshot.contextRevision`
3. `menuStackRevision`
   - 来自 Phase 2 已发布字段 `ResolvedContextSnapshot.menuStackRevision`；即便 top context 不变，只要实例栈变化也必须视作边界
4. `deviceFamilyRevision`
   - 唯一 authoritative 来源固定为 `IngressEvent(kind = DeviceFamilyChanged).payload.deviceFamilyRevision`
   - 紧随 marker 的 `SourceEvidenceSnapshot.deviceFamilyEvidence.deviceFamilyRevision` 只承担配对、镜像和一致性校验，不能单独切 boundary
   - 它不是 `PresentationOwner`，而是 raw device family / capability 侧的修订号

冻结规则如下：

1. 只有 boundary key 完全相同，才允许 coalescing。
2. 任一字段变化，都必须结束当前 merge window。
3. `contextRevision` 与 `menuStackRevision` 不能互相代替；二者都必须保留。
4. `deviceFamilyRevision` 不允许从 `PublishedPresentationState.family` 逆推，也不允许直接读取
   `PublishedPresentationState.deviceFamilyRevision` 镜像字段；boundary key 只能读取
   `DeviceFamilyChanged` marker payload，并用后续配对的 `SourceEvidence` snapshot 校验该值。
5. Phase 8 做 cleanup 时，可以删除 legacy 字段，但不能改 boundary key 语义。

上游字段引用合同现在一并写死：

- Phase 7 直接消费 `ResolvedContextSnapshot.contextRevision` 与 `ResolvedContextSnapshot.menuStackRevision`，字段名与递增语义必须原样继承，不允许在 ingress 层重新命名或合并。
- Phase 7 只允许从 `IngressEvent(kind = DeviceFamilyChanged).payload.deviceFamilyRevision` 读取边界所需 revision；`SourceEvidenceSnapshot.deviceFamilyEvidence.deviceFamilyRevision` 与 `PublishedPresentationState.deviceFamilyRevision` 都只视为下游镜像 / explain / 校验字段，不得单独参与 boundary 判定。
- `FrameAssembler` 必须先消费 `DeviceFamilyChanged` marker，把 marker payload 原样填入 `IngressBoundaryKey.deviceFamilyRevision`；只有拿到后续配对且值一致的 `SourceEvidence` snapshot 后，才允许把该值镜像进 `AssembledFactFrame.facts.sourceEvidence.deviceFamilyEvidence.deviceFamilyRevision` 并发布新 boundary 的第一份 `Stable` frame。
- `AssembledFactFrame.facts.manifestEpoch / contextRevision / menuStackRevision / deviceFamilyRevision`
  必须与 `IngressBoundaryKey` 一一对应；`FrameAssembler` 只能填值，不能重写 schema。

### 4. 同 boundary key 下的 merge 规则现在固定

同一个 boundary key 内，`FrameAssembler` 必须按下面规则 merge：

| 事实类型 | merge 方式 |
| --- | --- |
| `PadSnapshot` 的模拟量状态 | 取最后一份快照 |
| `UiSnapshot` / `HostFacts` / `SourceEvidence` | 取最后一份快照 |
| `ManifestEpochChanged` / `DeviceFamilyChanged` | 不 merge，直接切 boundary |
| 数字按键边沿 | 必须累计到 pulse ledger，不能只保留最后状态 |
| health marker | 不 merge，直接转成 `Transition` frame |

数字边沿的冻结规则如下：

1. 如果同一 merge window 内发生 `press -> release`，即便最终 steady state 回到未按下，也必须在本 window 的 `FactFrame` 中保留 pulse edge。
2. 如果同一 merge window 内发生 `release -> press`，同样必须保留两次边沿，顺序以 `seq` 为准。
3. `PadSnapshot` 的“最后状态”不能覆盖 pulse ledger；二者必须同时存在。
4. 不允许因为“最终值没变”就丢掉 transient digital。

### 5. transition frame 的语义与产出顺序现在固定

一旦发生下面任一情况，assembler 必须产出 `Transition` frame：

- boundary key 变化
- `SequenceGap`
- `QueueOverflow`
- `ExplicitReset`

固定产出顺序如下：

1. 先结束并发布边界前的最后一份 `Stable` frame。
2. 再发布一份独立的 `Transition` frame。
3. 最后才开始边界后的下一份 `Stable` frame。

`Transition` frame 的固定语义如下：

1. 它不承载新的动作解析结果；它只承载边界切换与 recovery 指令。
2. 它必须显式记录：
   - `from` boundary key
   - `to` boundary key
   - `reason`
   - 是否请求 soft/hard resync
3. 它不能和边界后的第一份 `Stable` frame 合并。
4. runtime shell 在消费 `Transition` frame 时，必须先生成 Phase 5 需要的 `GameplayRecoveryInput`，再让下一份 `Stable` frame 进入 projection / executor。
5. 如果本次 boundary 之前的 window 里还有未发布的 pulse edge，必须先随边界前的 `Stable` frame 一起发布，不能拖到 transition 或边界后。

### 6. gap / overflow / explicit reset 的 recovery 语义现在固定

本 slice 把 recovery reason 固定成 3 档：

| ingress 事件 | transition reason | recovery 语义 |
| --- | --- | --- |
| boundary key 普通变化 | `BoundaryKeyChanged` | 不自动触发 gameplay recovery，只做 frame 边界切换 |
| `SequenceGap` | `SequenceGap` | `SoftResync` |
| `ExplicitReset` | `ExplicitReset` | `HardReset` |
| `QueueOverflow` | `QueueOverflow` | `HardReset` |

冻结规则如下：

1. `SequenceGap` 代表“事件链不完整但仍有最近状态可用”，因此只能触发 `SoftResync`。
2. `QueueOverflow` 代表“backlog 丢失且窗口不可信”，必须触发 `HardReset`。
3. `ExplicitReset` 的语义和 `HardReset` 等价；它不能走软恢复。
4. 普通 boundary key 变化不是 recovery；例如 menu open / close、普通 `contextRevision / menuStackRevision` 变化、`DeviceFamilyChanged`，不应默认清空输出面。
5. `ManifestEpochChanged` 不属于上面的“普通 boundary key 变化”；它的恢复语义以后文冻结的专项合同为准，必须触发 `HardReset` 并建立新 clean baseline。
6. Phase 7 只决定 `GameplayRecoveryInput` 的来源与 reason，不改 Phase 5 的 `RecoveryPlan` 字段。

### 7. overflow 之后的 backlog 处理方式现在固定

发生 `QueueOverflow` 后，ingress 侧行为固定如下：

1. 立即停止继续扩张当前 merge window。
2. 丢弃 overflow 之前尚未消费的增量 backlog。
3. 保留每类 replaceable 事实的最后一份快照缓存：
   - 最后一份 `PadSnapshot`
   - 最后一份 `UiSnapshot`
   - 最后一份 `HostFacts`
   - 最后一份 `SourceEvidence`
   - 最近的 `manifestEpoch`
   - 最近的 `deviceFamilyRevision`
4. 先发布一份 `Transition` frame，`reason = QueueOverflow`，并请求 `HardReset`。
5. 再用缓存快照重建 overflow 之后的第一份 `Stable` frame；这份 frame 视为新的 clean baseline 起点。

明确禁止：

- 不允许把 overflow 前遗留的 transient digital、helper command、sticky owner 带进 overflow 后首帧。
- 不允许跳过 transition 直接用缓存快照继续跑。

### 8. native / helper 同步 reset 的行为现在固定

Phase 7 只改 recovery 触发来源，不改 Phase 5 的恢复结构；但它必须把同步 reset 行为钉死：

1. 每一份触发 `SoftResync` 或 `HardReset` 的 `Transition` frame，都必须在同一 runtime tick 内生成同一份 `GameplayRecoveryInput`。
2. 这份 `GameplayRecoveryInput` 必须同时驱动：
   - `NativeButtonCommitBackend`
   - `KeyboardHelperBackend`
   - sustained digital aggregator
   - projection sticky owner clear
3. 不允许出现：
   - native backend 已 reset，但 helper backend 还保留旧 held
   - helper backend 已 reset，但 native backend 还保留旧 trigger / button down
4. `AuthoritativePollState` 的 clean baseline 提交只能发生在 recovery 执行完成、且 recovery 后的第一份 `Stable` frame 成功 apply 之后。
5. `CustomActionDispatcher` 不单独拥有第三套 reset 语义；它通过 helper 链路继承 reset。

### 9. legacy dispatcher / processor 的最终角色现在固定

本 slice 完成后，下面两个旧入口只能保留 adapter / forwarding 角色：

- `src/input/injection/PadEventSnapshotDispatcher.*`
- `src/input/injection/PadEventSnapshotProcessor.*`

冻结规则如下：

1. dispatcher 只允许负责把现有 producer 输出转成 `IngressEvent` 并送进 `IngressHub`。
2. processor 只允许负责：
   - drain `AssembledFactFrame`
   - 调 `DualPadRuntime`
   - 桥接 legacy 日志 / 兼容入口
3. 旧路径里的 coalescing、gap 检测、manual resync、backend 直调，必须在本 slice 内删除或清空成空壳。
4. 后续 slice 不允许再把业务决策塞回 dispatcher / processor。

## 前置依赖

进入本 slice 前，下面依赖必须已经满足；缺任何一项，都先回到对应 slice 收口，不要在 Phase 7 内发明替代方案：

1. `docs/plans/dualpad_rearchitecture/01_slice_phase0_freeze_and_replay_barrier_zh.md`
   - replay / diff barrier 已可用，且覆盖 backlog、sequence gap、overflow、config reload。
2. `docs/plans/dualpad_rearchitecture/03_slice_phase2_menu_instance_truth_zh.md`
   - `ResolvedContextSnapshot.contextRevision` 与 `ResolvedContextSnapshot.menuStackRevision` 都已经成为已发布字段，且递增规则已冻结。
3. `docs/plans/dualpad_rearchitecture/04_slice_phase3_presentation_split_zh.md`
   - `DeviceFamilyIngressPublisher -> SourceEvidenceCollector` 已稳定发布 `DeviceFamilyChanged` marker，并紧随其后发布携带同一 `deviceFamilyRevision` 的 `SourceEvidenceSnapshot`。
   - Phase 7 只允许用 `DeviceFamilyChanged` marker payload 切 boundary，用 `SourceEvidenceSnapshot.deviceFamilyEvidence.deviceFamilyRevision` 做配对和一致性校验；若仍只能从 `family`、`PublishedPresentationState.deviceFamilyRevision` 或匿名 adapter 逆推，则不得进入本 slice。
4. `docs/plans/dualpad_rearchitecture/05_slice_phase4_action_graph_and_interaction_engine_zh.md`
   - `ResolvedActionFrame` 已经稳定，且 `FactFrame / KernelFrame` 合同、`AssembledFactFrame -> KernelFrame` builder owner、pulse / transient digital 的消费面都已固定。
5. `docs/plans/dualpad_rearchitecture/06_slice_phase5_gameplay_projection_zh.md`
   - `GameplayRecoveryInput -> RecoveryPlan` 的结构与执行顺序已冻结。
6. Phase 6 prompt 切换完成后，`PromptProjection / PromptService` 已经只消费 frame 边界，不再自己从 legacy dispatcher 拿状态。

## 涉及代码与文档

### 新增文件

- `src/input_v2/ingress/IngressEvent.h`
- `src/input_v2/ingress/IngressHub.h`
- `src/input_v2/ingress/IngressHub.cpp`
- `src/input_v2/ingress/FrameAssembler.h`
- `src/input_v2/ingress/FrameAssembler.cpp`
- `tests/input_v2/IngressHubTests.cpp`
- `tests/input_v2/FrameAssemblerTests.cpp`
- `tests/input_v2/IngressRecoveryTests.cpp`

### 需要修改的现有代码

- `src/input_v2/runtime/DualPadRuntime.h`
- `src/input_v2/runtime/DualPadRuntime.cpp`
- `src/input/injection/PadEventSnapshotDispatcher.h`
- `src/input/injection/PadEventSnapshotDispatcher.cpp`
- `src/input/injection/PadEventSnapshotProcessor.h`
- `src/input/injection/PadEventSnapshotProcessor.cpp`
- `src/input/backend/NativeButtonCommitBackend.h`
- `src/input/backend/NativeButtonCommitBackend.cpp`
- `src/input/backend/KeyboardHelperBackend.h`
- `src/input/backend/KeyboardHelperBackend.cpp`
- `src/input/AuthoritativePollState.h`
- `src/input/AuthoritativePollState.cpp`
- `xmake.lua`

### 只允许接线，不允许改规则的现有代码

- `src/input_v2/projections/GameplayProjection.h`
- `src/input_v2/projections/GameplayProjection.cpp`
- `src/input_v2/projections/PromptProjection.h`
- `src/input_v2/projections/PromptProjection.cpp`

这里最多允许做：

- 接收新的 `AssembledFactFrame`
- 读取新的 `GameplayRecoveryInput`
- 增加 explain / telemetry 字段

不允许做：

- 修改 owner 规则
- 修改 prompt 解析规则
- 在 projection 里补第二套 resync 判定

### 需要同步阅读的文档

- `docs/current_input_pipeline_zh.md`
- `docs/backend_routing_decisions.md`
- `docs/unified_action_lifecycle_model_zh.md`
- `docs/main_menu_glyph_current_status_zh.md`
- `docs/plans/dualpad_rearchitecture/05_slice_phase4_action_graph_and_interaction_engine_zh.md`
- `docs/plans/dualpad_rearchitecture/06_slice_phase5_gameplay_projection_zh.md`
- `docs/plans/dualpad_rearchitecture/07_slice_phase6_prompt_projection_zh.md`

如需历史背景，再回看 `docs/plans/dualpad_rearchitecture/dualpad_rearchitecture_plan_zh.md`。

## 实施步骤

### 1. 先定义 ingress 共享数据模型，不切行为

先在 `src/input_v2/ingress/IngressEvent.h` 和 `src/input_v2/ingress/FrameAssembler.h` 定义纯数据结构，一次性把下面这些类型定清：

- `IngressKind`
- `IngressEvent`
- `IngressBoundaryKey`
- `AssembledFrameKind`
- `TransitionReason`
- `TransitionFrameMeta`
- `AssembledFactFrame`

这一步要求：

1. transport 层类型与业务层类型分开。
2. `Transition` frame 要成为正式类型，不能继续用布尔旗标暗示。
3. boundary key 必须精确落成 4 个字段，不能留“以后再加”。
4. `FactFrame` 的字段集必须直接对齐 `docs/plans/dualpad_rearchitecture/05_slice_phase4_action_graph_and_interaction_engine_zh.md` 里冻结的合同；Phase 7 不得私自减字段或换名。

### 2. 落 `IngressHub`，把跨线程入口收口成唯一队列

在 `src/input_v2/ingress/IngressHub.*` 中实现统一 enqueue / drain 入口，固定要求如下：

1. 所有 producer 都走命名明确的 `IngressHub::PushEvent(const IngressEvent&)`；不允许保留“等价接口”。
2. `IngressHub` 负责分配全局 `seq` 与 `monotonicUs`。
3. queue full 时不能只打 log；必须 enqueue `QueueOverflow` 标记或在下一次 drain 时显式生成该事件。
4. producer 自身发现 gap 时，必须 enqueue `SequenceGap`；不允许只在本地静默自愈。
5. `ManifestEpochChanged`、`DeviceFamilyChanged` 必须作为显式 ingress 事件进入队列，不能只改共享原子值。

此步完成后，旧 dispatcher 仍可继续跑，但新 hub 已能记录完整 ingress 事件流。

### 3. 落 `FrameAssembler`，先只处理 stable frame

在 `src/input_v2/ingress/FrameAssembler.*` 中先实现 stable merge 路径，固定规则如下：

1. drain 顺序只看 `seq`。
2. 相同 boundary key 内，replaceable 事实取最后值。
3. 数字边沿累计到 pulse ledger，不允许被最后状态覆盖。
4. 只要还没遇到 boundary 变化或 health marker，就继续扩张当前 stable window。
5. assembler 必须维护“上一份已知 boundary key”和“当前 merge window”两份状态，不能用单个 mutable snapshot 混写。

这一阶段先不切 transition frame，只用测试验证 stable merge 不丢 pulse edge。

### 4. 再实现 transition frame，禁止跨边界吞并

当 stable merge 路径跑通后，再补 transition 逻辑，固定顺序如下：

1. 遇到 boundary key 变化，先 flush 边界前 stable frame。
2. 生成 `reason = BoundaryKeyChanged` 的 transition frame。
3. 用新 boundary key 开启新的 stable window。
4. 遇到 `SequenceGap`、`QueueOverflow`、`ExplicitReset` 时，不开启跨事件合并，直接落 transition。

关键要求：

1. transition frame 不能包含边界后的业务事实。
2. 边界后的第一份 stable frame 必须重新开始累计 `firstSeq`。
3. 旧窗口里残留的 pulse edge 必须先发布完，再进入 transition。

### 5. 把 gap / overflow / explicit reset 映射成统一 recovery 输入

在 `DualPadRuntime` 的 ingress 消费入口里，把 transition frame 映射到 Phase 5 的 recovery 输入，固定映射如下：

1. `BoundaryKeyChanged` -> 不生成 recovery，只推进 frame 边界。
2. `SequenceGap` -> 生成 `SoftResync` 型 `GameplayRecoveryInput`。
3. `QueueOverflow` -> 生成 `HardReset` 型 `GameplayRecoveryInput`。
4. `ExplicitReset` -> 生成 `HardReset` 型 `GameplayRecoveryInput`。

这里明确禁止：

- 在 `GameplayProjection` 内再次判断 gap / overflow。
- 在 backend 内自行猜测“是不是该 reset 了”。

### 6. 固定 overflow 后首帧重建顺序

overflow 路径必须按下面顺序实现，不能调换：

1. hub 记录 `QueueOverflow`
2. assembler 先发 transition frame
3. runtime 先执行 recovery
4. assembler 再用 replaceable latest-state cache 组装 overflow 后首个 stable frame
5. runtime 仅在该首帧成功 apply 后，才提交新的 clean baseline

这样做的目的，是让 overflow 之后的第一份稳定快照成为新的起点，而不是把旧 backlog 的残片带过来。

### 7. 把 native / helper reset 收口到同一执行边界

实现切换时，固定要求如下：

1. `DualPadRuntime` 在消费 recovery transition 时，必须只生成一份 recovery context。
2. `PollOutputAdapter` 必须先应用 recovery，再应用 recovery 后的下一份 stable frame。
3. `NativeButtonCommitBackend` 和 `KeyboardHelperBackend` 必须在同一 runtime tick 内 reset。
4. `AuthoritativePollState` 只在 recovery 完成后接受新的 stable analog / digital 结果。

这里不能再保留任何“native 先清，helper 下一帧再补清”的过渡逻辑。

### 8. 把 legacy dispatcher / processor 退化成 adapter

等新 ingress 路径稳定后，按下面顺序收缩旧路径：

1. `PadEventSnapshotDispatcher` 只保留 producer -> `IngressHub` 的适配。
2. `PadEventSnapshotProcessor` 只保留 `AssembledFactFrame -> DualPadRuntime` 的适配。
3. 删除旧的：
   - coalescing window
   - manual gap detect
   - ad-hoc reset flag
   - backend 直调
4. 保留 parity / telemetry 日志，直到新路径通过验证。

### 9. 用 Phase 0 的 replay 资产做 cutover 前对比

正式切 authoritative 前，必须让新旧路径并行跑一段 shadow compare，至少比较：

- stable frame 边界
- transition frame 产出时机
- recovery reason
- native/helper reset 次数
- overflow 后首帧的 baseline 提交时机

只要以上任一项存在未解释 diff，就不能切掉旧路径。

## Shadow Compare Exit Gate

Phase 7 的 shadow compare 不再只是“先比一阵再说”，而是必须满足下面这组硬门槛才能切 authoritative：

1. 比对对象固定为：
   - 新路径：`IngressHub -> FrameAssembler -> DualPadRuntime`
   - 旧路径：命名明确的 `LegacyIngressParityPath`，即切主路径前仍存在的 dispatcher / processor legacy coalescing 链
2. 必跑场景固定为：
   - gameplay 内快速 `press -> release`
   - gameplay -> menu -> gameplay
   - manifest reload
   - sequence gap
   - queue overflow
   - explicit reset
   - native / helper 同时存在时的 reset
3. 允许的 intentional diff 只有两类：
   - 新路径新增了独立 `Transition` frame 记录，而旧路径只有隐式边界切换
   - 新路径日志 / telemetry 比旧路径多出 `boundaryKey`、`reason`、`firstSeq / lastSeq` 等 explain 字段
4. 明确不允许的 diff：
   - recovery reason 不一致
   - pulse ledger 顺序不一致
   - stable frame 边界不一致
   - native / helper reset 次数或 tick 不一致
   - overflow 后首帧 baseline 提交时机不一致
5. `LegacyIngressParityPath` 的最长存活窗口固定为：
   - Phase 7 切 authoritative 的同一轮验证窗口内允许存在
   - 一旦切主路径，必须在同一 PR 或紧随其后的清理 PR 中降为 debug / test-only
   - 不允许跨过 Phase 8 runtime closeout 继续留在 live runtime
6. cutover PR 必须同时做到：
   - 记录 intentional diff 白名单
   - 把 `LegacyIngressParityPath` 从 live runtime 决策链降到 parity / telemetry 角色
   - 明确列出仍保留的 legacy shim 与删除时点

## Rollback Boundary / Emergency Revert

Phase 7 的回滚边界固定如下；出现 live smoke 或 replay 失真时，只允许沿这些边界回退，不允许用“先把旧逻辑留着再说”替代合同：

1. `Rollback Boundary A`
   - `IngressHub` 已记录完整事件流
   - `FrameAssembler` 已能离线产出 `AssembledFactFrame`
   - live runtime 仍以 `LegacyIngressParityPath` 为 authoritative
   - 这是 Phase 7 最低风险的可回滚形态
2. `Rollback Boundary B`
   - `DualPadRuntime` 已消费 `AssembledFactFrame`
   - `LegacyIngressParityPath` 仍保留为仅限 fallback / parity 的命名路径
   - 旧 dispatcher coalescing 尚未物理删除
3. emergency revert 顺序固定为：
   - 先把 live runtime 路由回 `LegacyIngressParityPath`
   - 保留 `IngressHub` / `FrameAssembler` 的 telemetry 与 replay 产物，继续用于定位 diff
   - 禁止在回滚时混入新的 coalescing / reset 逻辑
4. 旧 dispatcher / processor 的 coalescing 与 reset 删除，必须晚于：
   - `Shadow Compare Exit Gate` 通过
   - 至少一轮 live smoke 通过
   - replay / golden trace 结果与 cutover 版本一致
5. 任一 emergency revert 都必须连同以下内容一起回退：
   - 代码接线
   - shadow compare 白名单
   - replay / snapshot 预期
   - 与 Phase 7 authoritative path 绑定的 explain 文档
6. `LegacyIngressParityPath` 在回滚窗口内也只能承担 fallback / parity 职责，不得吸收新的业务规则、owner 规则或 prompt 规则。

## 验证与观测

### 自动测试

至少新增并跑通下面 3 类测试：

1. `IngressHubTests`
   - 验证全局 `seq` 单调递增
   - 验证 queue full 会产出 `QueueOverflow`
   - 验证 producer gap 会产出 `SequenceGap`
2. `FrameAssemblerTests`
   - 验证同 boundary key merge
   - 验证 pulse edge 不丢
   - 验证 boundary key 变化会发 `Transition`
   - 验证 transition 不吞边界后的第一份 stable frame
3. `IngressRecoveryTests`
   - 验证 `SequenceGap -> SoftResync`
   - 验证 `QueueOverflow -> HardReset`
   - 验证 native/helper 在同一 tick reset
   - 验证 overflow 后首帧才提交 clean baseline

建议固定的构建与运行入口为：

```powershell
xmake build DualPad
xmake build DualPadIngressTests
xmake run DualPadIngressTests
```

### replay / golden trace 必测场景

必须至少回放下面场景，并比较 stable / transition / recovery 序列：

1. gameplay 中快速 `press -> release`，确认 merge 不丢 pulse edge。
2. gameplay -> menu -> gameplay，确认 context boundary 会产出 transition，而不是吞并成单帧。
3. manifest reload，确认 `ManifestEpochChanged` 产出专项 `Transition`，触发 `HardReset`，并在恢复后建立新 clean baseline。
4. sequence gap，确认触发 soft resync。
5. queue overflow，确认触发 hard reset，且 overflow 前 backlog 不会泄漏到 overflow 后首帧。
6. helper 输出与 native 输出同时存在时的 reset，确认两者同 tick 清空。

### 运行时观测

本 slice 必须新增并保留下面这些日志类别：

- `[DualPad][IngressHub]`
- `[DualPad][FrameAssembler]`
- `[DualPad][IngressTransition]`
- `[DualPad][IngressRecovery]`

日志至少要能回答下面问题：

1. 当前 stable frame 的 `firstSeq / lastSeq` 是多少。
2. 当前 boundary key 是什么。
3. 为什么发了 transition frame。
4. 这次 transition 是否触发 recovery，触发的是 soft 还是 hard。
5. native/helper 是否在同一 tick reset。

## 退出条件

只有下面条件全部满足，Phase 7 才算完成：

1. `IngressHub` 已成为唯一跨线程 ingress 入口。
2. `FrameAssembler` 已成为唯一 coalescing 层。
3. boundary key 已按本页定义固定为 4 个字段，代码里没有第二套隐式边界判定。
4. `Transition` frame 已成为 runtime 正式消费对象，而不是调试旁路。
5. `SequenceGap`、`QueueOverflow`、`ExplicitReset` 已全部走统一 recovery 输入，不再存在 legacy 手写 reset 分支。
6. overflow 后首帧重建顺序已按本页合同实现，旧 backlog 不会泄漏到新 baseline。
7. native backend 与 helper backend 的 reset 已保证同 tick 执行。
8. 旧 dispatcher / processor 已退化为 adapter，不再保留 coalescing / recovery 决策权。
9. `DualPadIngressTests` 与 replay 必测场景全部通过。
10. `Shadow Compare Exit Gate` 已通过，`LegacyIngressParityPath` 已不再参与 live runtime authoritative 决策。

## 交接给下一 slice 的合同

交给 `docs/plans/dualpad_rearchitecture/09_slice_phase8_cutover_cleanup_and_ci_zh.md` 的固定合同如下：

1. ingress 层的唯一真相源已经固定为：
   - `IngressHub`
   - `FrameAssembler`
   - `AssembledFactFrame`

## 最终合同汇总

以下汇总必须与上文正文保持一致；如果后续修改正文，也必须同步修改本节，不能再依赖“末尾覆盖前文”的补丁式口径。

### A. boundary marker 与 boundary key 的 authoritative 来源覆盖规则

1. `manifestEpoch`
   - 唯一 authoritative 来源固定为 `IngressEvent(kind = ManifestEpochChanged).payload.manifestEpoch`。
   - `facts.manifestEpoch` 只是该 marker payload 的镜像；不再允许从 live `ActionManifest`、active graph 指针或其它 published state 直接取值。
   - `ManifestEpochChanged` 没有第二条 ingress snapshot 配对路径；marker 自己就是完整值。
2. `deviceFamilyRevision`
   - 唯一 authoritative 来源固定为 `IngressEvent(kind = DeviceFamilyChanged).payload.deviceFamilyRevision`。
   - `facts.sourceEvidence.deviceFamilyEvidence.deviceFamilyRevision` 必须与 marker payload 完全一致；`SourceEvidence` snapshot 只承担配对和一致性校验，不再单独切 boundary。
3. `contextRevision / menuStackRevision`
   - 仍只允许直接复制 `ResolvedContextSnapshot.contextRevision / menuStackRevision`，字段名与递增语义不得改写。
4. `PublishedPresentationState.deviceFamilyRevision`、任何 debug 镜像字段、以及 graph 内部缓存的 `manifestEpoch`
   - 只视为下游 explain 信息，不得参与 boundary 判定。
5. `AssembledFactFrame.facts.manifestEpoch / contextRevision / menuStackRevision / deviceFamilyRevision`
   - 必须与 `IngressBoundaryKey` 一一对应；`FrameAssembler` 只能填值，不能重写 schema。

### B. marker payload、producer、顺序与 fail-closed 规则

1. `ManifestEpochChanged`
   - producer 固定为 `ActionManifestPublisher` / manifest hot-reload controller。
   - enqueue 顺序固定为：
     1. 先完成新 `CompiledActionGraph` 的 staging。
     2. enqueue `ManifestEpochChangedPayload{ newEpoch }`。
     3. 只有 marker 已成功入队，runtime 才允许让下一份 `Stable` frame 消费新 graph。
   - assembler 消费顺序固定为：
     1. flush 边界前最后一份 `Stable` frame
     2. 发 `TransitionReason::ManifestEpochChanged`
     3. 再开始边界后的第一份 `Stable` frame
2. `DeviceFamilyChanged`
   - producer 固定为 `DeviceFamilyIngressPublisher -> SourceEvidenceCollector` 这条已发布链。
   - enqueue 顺序固定为：
     1. 先产出将要发布的 `SourceEvidenceSnapshot`
     2. 若 `deviceFamilyRevision` 变化，先 enqueue `DeviceFamilyChangedPayload{ family, newRevision }`
     3. 紧接着 enqueue 携带同一 `deviceFamilyRevision` 的 `IngressEvent(kind = SourceEvidence)`
   - assembler 消费顺序固定为：
     1. 先消费 marker
     2. 立刻结束旧 boundary 的 merge window
     3. 只有拿到配对的 `SourceEvidence` snapshot 后，才允许发新 boundary 的第一份 `Stable` frame
3. fail-closed 行为固定为：
   - 如果 `SourceEvidence` snapshot revision 已变，但前面没有匹配的 `DeviceFamilyChanged` marker，assembler 不得从 snapshot 直接切 boundary；必须拒绝该 snapshot 进入新的 `Stable` frame，置位 `FactHealth.boundaryMarkerMismatch = true`，并走 `ExplicitReset -> HardReset`。
   - 如果 `DeviceFamilyChanged` marker 已到，但后续第一份 `SourceEvidence` snapshot revision 不匹配，assembler 不得“就近取值”；必须置位 `FactHealth.pendingBoundaryMarkerPair = true` 与 `boundaryMarkerMismatch = true`，并走 `ExplicitReset -> HardReset`。
   - 如果 `DeviceFamilyChanged` marker 已到但直到下一个 boundary / health 事件前都没有等到配对 snapshot，assembler 不得发布该新 boundary 的第一份 `Stable` frame；必须保持 pending，并在后续 `ExplicitReset` 或 recovery 后重新起边界。

### C. `manifestEpoch` 不再属于“普通 boundary 不 recovery”

1. `manifestEpoch` 变化必须发 `TransitionReason::ManifestEpochChanged`，且：
   - `requestHardResync = true`
   - `flushPendingPulseEdges = true`
2. runtime shell 对接 Phase 5 时，固定映射为既有 `HardReset` / `RecoveryPlan.mode = HardResetOutputs`，不得另外发明半套 manifest-only reset。
3. 该 `HardReset` 必须清空：
   - `InteractionStateStore`
   - native 输出面 held / latch / pending commit
   - helper backend 旧 held / helper queue
   - sustained digital aggregator
   - projection sticky owner / sticky prompt 继承态
4. `AuthoritativePollState` 的 clean baseline 只能在：
   - `ManifestEpochChanged` 对应的 recovery 已执行完成
   - recovery 后第一份 `Stable` frame 已成功 apply
   之后再提交。
5. `PublishedGameplayPresentation` 必须在 recovery 后按 clean gameplay baseline 重新发布，不能沿用旧 manifest 下的发布对象。
6. `PublishedPromptScope` 必须在 recovery 后重新发布；即使其它 presentation 字段不变，只要 `manifestEpoch` 变了也不允许继续沿用旧 scope。
7. `DeviceFamilyChanged`、menu open / close、普通 context/menu stack 边界切换仍属于普通 `BoundaryKeyChanged`，默认不自动触发 gameplay recovery。

### D. 对验证、退出条件与交接合同的覆盖

1. `IngressRecoveryTests`
   - 除 `SequenceGap -> SoftResync`、`QueueOverflow -> HardReset` 外，必须额外覆盖 `ManifestEpochChanged -> HardReset`。
   - 必须验证 manifest reload 后：
     - `InteractionStateStore` 被清空
     - native/helper/sustained aggregator 同 tick reset
     - `PublishedGameplayPresentation` / `PublishedPromptScope` 被 republish
2. replay / golden trace
   - `manifest reload` 的期望从“只切 boundary、不触发 gameplay recovery”改为“切 boundary 且触发 `HardReset`，随后建立新 clean baseline”。
3. 退出条件
   - 除现有 `SequenceGap / QueueOverflow / ExplicitReset` 统一 recovery 外，还必须满足 `ManifestEpochChanged` 已走专项 `HardReset` 路径。
   - 代码中不得再存在从 snapshot 或 published state 直接推导 `manifestEpoch / deviceFamilyRevision` 边界的第二 authority。
4. 交接给 Phase 8 的合同
   - `manifestEpoch` 与 `deviceFamilyRevision` 的 authoritative 来源已经冻结为 marker payload，Phase 8 只能删旧路径，不能改回 snapshot/source-driven boundary。
   - `ManifestEpochChanged -> HardResetOutputs -> clean baseline republish` 已固定，Phase 8 只能做清理和 CI 覆盖，不能改恢复级别。
   - recovery 的唯一 ingress 来源已经固定为 transition frame；Phase 8 不允许再新增“legacy resync flag”。
   - native/helper 同步 reset 的语义已经固定；Phase 8 只能做清理和 CI 覆盖，不能改执行顺序。
   - boundary key 的 4 个字段已经冻结；Phase 8 只能删旧路径，不能重写边界定义。
   - `LegacyIngressParityPath` 只允许作为 Phase 7 的临时 fallback / parity 路径存在；Phase 8 必须删除它，而不是继续把它当长期缓冲区。
   - 旧 dispatcher / processor 只剩 adapter 角色；Phase 8 应删除其残余 authority，而不是继续给它们补功能。
