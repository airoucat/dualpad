# Phase 4 Slice：Action Graph 与 InteractionEngine

> 本文对应重构阶段里的 Phase 4。
> 这里的 Phase 4 是重构阶段编号，不等于 `.dualpad-builder` 里的 `DP4` 工作包。

## 目标

- 把当前 `Trigger -> BindingResolver -> ActionLifecycleCoordinator` 这条语义链，改造成 `CompiledActionGraph -> InteractionEngine -> ResolvedActionFrame`。
- 让 legacy `Button / Layer / Combo / Hold / Tap / Gesture / Axis` 只在编译期出现一次，由 `LegacyIniImporter` 降格为统一 IR，运行时不再把 `TriggerType` 当成 authoritative truth。
- 冻结 Phase 4 的稳定输入输出：
  - 输入：`ActionManifest`、`ActionSetStack`、最小可消费的 `KernelFrame`；若上游还未完整落地，只允许临时使用命名明确的 `LegacyInteractionInputAdapter`。
  - 输出：`CompiledActionGraph`、`InteractionStateStore`、`ResolvedActionFrame`、过渡期 `LegacyLifecycleBridge`。
- 保持对当前主线的可迁移性：本 slice 结束前，`GameplayProjection`、`PromptProjection`、`ScaleformPromptAdapter` 不切主路径，但必须能消费本 slice 产出的稳定合同。
- 本 slice 不做下面这些事：
  - 不在运行时继续新增 `TriggerType` 分支。
  - 不让 `BindingManager::GetTriggerForAction(...)` 回到任何 authoritative 路径。
  - 不在 `GameplayProjection` 或 `PromptProjection` 里补回输入交互语义。

## 冻结的设计决定

### 1. 落地顺序固定

1. 先定义 `ControlPath`。
2. 再定义 `BindingModifier`。
3. 再定义 `InteractionSpec`。
4. 再定义 `DisplayBinding`。
5. 再实现 `legacy trigger lowering`，把旧 INI 语义统一降格到上述 4 个类型。
6. 再实现 `ActionGraphCompiler`，产出不可变 `CompiledActionGraph`。
7. 再实现 `InteractionEngine` 与 `InteractionStateStore`。
8. 最后把 `BindingResolver / ActionLifecycleCoordinator` 退化成兼容壳层，并限定删除条件。

这个顺序不能交换。尤其不能先写运行时状态机，再临场决定 lowering 规则。

### 2. `ControlPath` 的冻结定义

`ControlPath` 只表达“物理输入路径”，不再混入 hold、tap、combo、glyph 或 fallback 语义。

建议固定到 `src/input_v2/actions/ActionGraphTypes.h`：

```cpp
enum class ControlPathKind : std::uint8_t {
    DigitalButton,
    AnalogAxis1D,
    TouchGesture,
    TouchRegion
};

enum class AxisComponent : std::uint8_t {
    None,
    X,
    Y
};

struct ControlPath {
    ControlPathKind kind;
    std::uint32_t code;
    AxisComponent component;
};
```

冻结规则：

- `code` 继续复用当前已经存在的 button bit、`PadAxisId`、`mapping_codes::*` 等数值，不在本 slice 重新发明第二套编码。
- `ControlPath` 不携带 `InputContext`、`ActionSet`、`device profile`、`holdThreshold`。
- combo 参与者不是 modifier 列表，而是多个 `ControlPath`。
- `Layer` 不是 `ControlPathKind`，它在 lowering 后变成带参与者约束的 `InteractionSpec`。

### 3. `BindingModifier` 的冻结定义

`BindingModifier` 只负责物理值整形，不表达交互语义。

```cpp
enum class BindingModifierKind : std::uint8_t {
    Deadzone,
    Scale,
    Invert,
    Clamp,
    AxisThreshold
};

struct BindingModifier {
    BindingModifierKind kind;
    float primary;
    float secondary;
};
```

冻结规则：

- `Deadzone / Scale / Invert / Clamp / AxisThreshold` 是 Phase 4 唯一允许的 modifier。
- `Hold / Tap / Repeat / Toggle / Chord / Gesture` 不得塞进 `BindingModifier`。
- 旧 `Trigger.modifiers` 里的按钮 modifier 不再映射到 `BindingModifier`，而是变成 `InteractionSpec.requiredPaths`。

### 4. `InteractionSpec` 的冻结定义

`InteractionSpec` 负责把 `ControlPath` 变化转换成“binding 已激活 / 已取消 / 正在进行”的交互状态；它不替代 action contract。

```cpp
enum class InteractionKind : std::uint8_t {
    Value,
    Press,
    Hold,
    Tap,
    Repeat,
    Toggle,
    Chord,
    Gesture
};

struct InteractionSpec {
    InteractionKind kind;
    std::uint16_t primaryPathIndex;
    std::vector<std::uint16_t> requiredPathIndices;
    std::uint64_t holdThresholdUs;
    std::uint64_t tapMaxUs;
    std::uint64_t repeatDelayUs;
    std::uint64_t repeatIntervalUs;
    std::uint64_t chordWindowUs;
    bool unordered;
};
```

冻结规则：

- `Value` 是模拟轴的唯一 passthrough 交互类型。
- `Press / Hold / Tap / Repeat / Toggle / Chord / Gesture` 是离散交互语义全集，本 slice 不再扩新名字。
- `requiredPathIndices` 是 chord / layer / modifier 约束的统一表达。
- `action.contract` 仍然保留在 action 定义上；`InteractionSpec` 只决定 binding 在什么时候变成“已激活”。
- `Layer` 的语义冻结为“精确 chord 约束”，不是顺序语义；当前代码没有顺序事件通道，Phase 4 不虚构这层能力。

### 4A. `Layer / Combo` 的冻结对照

`Layer` 与 `Combo` 从 Phase 4 起不再允许被实现者按“都像 chord”自由解释。二者的差异现在就冻结，供 Phase 1 importer、Phase 6 prompt scope、后续测试与文档统一对齐：

| 对照维度 | `Layer` | `Combo` |
| --- | --- | --- |
| 运行时语义 | 主路径交互的参与者约束 | 独立的 `Chord` 交互 |
| 顺序敏感 | 否。只检查主路径触发时 `requiredPaths` 是否已处于激活态 | 否。窗口内重叠即可 |
| 时间窗 | 无独立 `chordWindowUs`；沿用主路径交互自身的阈值 | 必须使用 `chordWindowUs = kLegacyComboWindowUs` |
| `InteractionKind` | 仍是 `Press / Hold / Tap / Repeat / Toggle` 之一，`Layer` 本身不是新的 `InteractionKind` | 固定降格为 `Chord` |
| primary path 选择 | 唯一主路径必须是最终执行动作的非 layer 控件；其余参与者全部进入 `requiredPathIndices` | primary path 固定为最后一个 legacy 参与者；其余参与者进入 `requiredPathIndices` |
| `BindingMatchPolicy` | `ExactOnly` | `ExactOnly` |
| 默认 display 语义 | 直接显示为 `required + primary`，例如 `L1 + Square` | 显示为组合候选；若 legacy token bridge 无法直渲染，必须由 manifest 明确补 token |
| 允许的 legacy 扩展 | 不扩成顺序层、不扩成三段式状态机 | 不扩成三键及以上 combo |

额外冻结规则：

- legacy `Layer:*` 明确不是 ordered chord。任何“按下顺序决定是否命中”的实现都视为违约。
- `Layer` 只能表达“一个主路径 + 一组同时成立的 required 路径”。如果 importer 产出多个候选主路径，compiler 必须 fail-closed，不允许运行时猜测。
- `Combo` 的窗口语义只能由 `InteractionEngine` 里的 `Chord` 状态机实现，不允许在 compiler、display、prompt 或后续 slice 重新定义第二套窗口规则。
- Phase 1 若仍保留 legacy AST / lowering 表，不得再写出 `Layer -> ordered chord`；必须回对齐本表。

### 5. `DisplayBinding` 的冻结定义

`DisplayBinding` 是 prompt / glyph 候选的编译产物，不再从运行时 `Trigger` 反查。

```cpp
enum class DisplayBindingMode : std::uint8_t {
    Primary,
    Alternate,
    Hidden
};

struct DisplayBinding {
    std::uint32_t bindingId;
    DisplayBindingMode mode;
    std::string token;
    std::string localizedLabel;
    std::uint16_t priority;
    std::string deviceProfile;
};
```

冻结规则：

- `DisplayBinding` 由 compiler 生成并验证，不允许 `ScaleformGlyphBridge` 现算。
- `Primary / Alternate / Hidden` 是唯一合法显示模式。
- `Button / Layer / Hold / Tap` 默认生成可显示候选。
- `Combo` 默认生成“组合候选”，但要标记为 legacy token bridge 不可直接渲染。
- `Gesture / Axis` 默认生成 `Hidden` 候选，除非 manifest 显式提供可显示 token。

### 6. `Action Graph` 的冻结定义

Phase 4 的 authoritative 运行时输入不是 `BindingManager`，而是不可变 `CompiledActionGraph`。

建议固定到 `src/input_v2/actions/CompiledActionGraph.h`：

```cpp
struct CompiledBinding {
    std::uint32_t bindingId;
    ActionId actionId;
    ActionSetId actionSetId;
    std::vector<ControlPath> paths;
    std::vector<BindingModifier> modifiers;
    InteractionSpec interaction;
    BindingMatchPolicy matchPolicy;
    std::uint32_t primaryDisplayBindingId;
};

struct CompiledActionGraph {
    std::uint32_t manifestEpoch;
    std::vector<ActionRecord> actions;
    std::vector<CompiledBinding> bindings;
    std::vector<DisplayBinding> displayBindings;
    CompiledBindingLookupTables lookups;
};
```

冻结规则：

- `CompiledActionGraph` 整体热切换，不能局部 mutate。
- `CompiledActionGraphPublisher` 固定为 runtime graph publication 与 hot-swap 的唯一 owner，代码落点为 `src/input_v2/actions/CompiledActionGraphPublisher.*`。
- `ActionGraphCompiler` 只产出不可变 graph；它不得直接替换 runtime active graph，也不得发 ingress marker。
- `CompiledActionGraphPublisher::Publish(const CompiledActionGraph&, std::uint64_t manifestEpoch)` 只能消费已经通过 Phase 1 `ActionManifestPublisher` 发布的 manifest epoch；若 epoch 不匹配，publish 必须 fail-closed。
- `BindingMatchPolicy` 只允许两个值：
  - `ExactOnly`
  - `PreferExactThenSubset`
- `PreferExactThenSubset` 只给 legacy `Button / Hold / Tap` 降格路径使用，用来保留当前非 combo modifier fallback。
- `Layer / Combo / Gesture / Axis` 一律 `ExactOnly`。
- `GetActionForTrigger(...)`、`GetTriggerForAction(...)`、`unordered_map<Trigger, action>` 不再是 authoritative 存储。

### 7. `FactFrame / KernelFrame` 合同

从 Phase 4 开始，`InteractionEngine` 的正式输入面只接受 `KernelFrame`，不再接受匿名“等价帧”。`FactFrame` 与 `KernelFrame` 的结构合同现在就冻结，后续由 Phase 7 负责供给，而不是在实现 `InteractionEngine` 时临场发明字段。

建议固定到 `src/input_v2/kernel/KernelFrame.h` 与 `src/input_v2/kernel/InputKernel.h`：

```cpp
struct FactPulseEdge {
    ControlPath path;
    DigitalEdge edge;
    std::uint64_t seq;
    std::uint64_t timestampUs;
};

struct FactFrame {
    std::uint64_t firstIngressSeq;
    std::uint64_t lastIngressSeq;
    std::uint64_t monotonicUs;
    std::uint32_t manifestEpoch;
    std::uint32_t contextRevision;
    std::uint32_t menuStackRevision;
    std::uint32_t deviceFamilyRevision;
    PadSnapshot padSnapshot;
    std::vector<FactPulseEdge> pulseLedger;
    UiSnapshot uiSnapshot;
    HostFacts hostFacts;
    SourceEvidence sourceEvidence;
    FactHealth health;
};

struct KernelFrame {
    FactFrame facts;
    KernelState state;
    std::uint64_t kernelRevision;
};
```

为避免 `UiSnapshot / HostFacts / SourceEvidence / FactHealth / KernelState` 在实现阶段再次变成开放名词，本页同时冻结内层 schema、哨兵语义与 Phase 7 的填值边界。

#### 7A. 哨兵与空值语义先统一

- `Unknown`
  - 表示该 producer 本轮合同上应该给值，但当前 boundary 内还没有拿到第一份可信值。
- `Unavailable`
  - 表示当前路径按合同就是拿不到该事实，例如 `LegacyInteractionInputAdapter`、非 UI 语境下的 UI 字段，或上游尚未落地该发布面。
- `Empty`
  - 表示值是已知且合法的“空”，例如空 `ActionSetStack`、空 `scopeAnchorIds`、空 `windowState`、空 `controlSamples`。
- 禁止把 `Unknown`、`Unavailable`、`Empty` 混用成同一个 `0`/空串/空数组语义。
- `kernelRevision` 只比较冻结字段的正式值与正式哨兵；debug explain、日志缓存、trace label 不参与 revision。

#### 7B. `UiSnapshot` 正式结构

`UiSnapshot` 是 Phase 2 已发布 resolved context 在 kernel 面的镜像，不是新的 resolver。

| 字段 | 说明 | 合法 `Unknown / Unavailable / Empty` | 是否参与 `kernelRevision` | Phase 7 边界 |
| --- | --- | --- | --- | --- |
| `availability` | 当前 `UiSnapshot` 是否可用 | `Unknown` / `Unavailable` 均合法；不得省略 | 是 | 只能按上游发布态填值 |
| `uiContextId` | 当前 canonical UI context | `Unknown` 仅在本 boundary 尚未收到 resolved context 时允许；`Unavailable` 仅在 adapter 路径允许 | 是 | 只能复制 Phase 2 已发布字段，不能反查 catalog |
| `topMenuInstanceId` | 当前顶层 menu instance | `Empty` 表示 gameplay root / 无 menu；`Unknown` 仅允许在首帧前 | 是 | 只能复制 Phase 2 已发布字段 |
| `identityQuality` | 顶层实例身份质量 | `Unknown` 仅在 `availability != Known` 时允许 | 是 | 不能由 assembler 自己重判 |
| `contextRevision` | Phase 2 `ResolvedContextSnapshot.contextRevision` 镜像 | `0` 只允许和 `availability != Known` 同时出现 | 是 | 只能复制，不能自增 |
| `menuStackRevision` | Phase 2 `ResolvedContextSnapshot.menuStackRevision` 镜像 | `0` 只允许和 `availability != Known` 同时出现 | 是 | 只能复制，不能合并进别的 revision |
| `actionSets` | 当前正式 `ActionSetStack` | `Empty` 合法；不得用 `Unknown` 代替空栈 | 是 | 只能镜像已发布 stack，不能在 kernel 里重算 |
| `scopeAnchorIds` | 当前 scope anchor 链 | `Empty` 合法；`Unknown` 仅在 `availability != Known` 时允许 | 是 | 只能复制已发布 anchor 链 |

补充冻结规则：

- `UiSnapshot` 不携带 owner、prompt、display 候选，也不允许偷偷补回 `presentationPolicyId` 以外的推导结果。
- 只要 `uiContextId / topMenuInstanceId / identityQuality / contextRevision / menuStackRevision / actionSets / scopeAnchorIds` 任一变化，`kernelRevision` 都必须推进。
- Phase 7 只能填这些字段的值或正式哨兵，不能增删字段、换名或把 `actionSets + scopeAnchorIds` 折叠成匿名字符串。

#### 7C. `HostFacts` 正式结构

`HostFacts` 表示运行宿主侧事实，不允许从 projection 镜像倒推。

| 字段 | 说明 | 合法 `Unknown / Unavailable / Empty` | 是否参与 `kernelRevision` | Phase 7 边界 |
| --- | --- | --- | --- | --- |
| `availability` | `HostFacts` 发布面是否可用 | `Unknown` / `Unavailable` 合法 | 是 | 只能按 host publisher 的发布态填值 |
| `hostMode` | 宿主模式，例如 gameplay / menu / console | `Unknown` 仅在未收到首帧 host facts 时允许 | 是 | 不能由 `PublishedGameplayPresentation` 反推 |
| `gameplaySubstate` | gameplay 细分子态 | `Empty` 表示当前不在 gameplay；`Unknown` 仅在 `availability != Known` 时允许 | 是 | 只能复制 host facts 发布值 |
| `textEntryActive` | 是否处于文本输入/命名输入 | `false` 是正式值，不得用作未知哨兵 | 是 | 不得由 UI 名称或 prompt 行为猜出 |
| `cursorCaptured` | 宿主是否处于 cursor capture / pointer capture | `false` 是正式值 | 是 | 只能复制 host publisher 结果 |
| `gameplayInputBlocked` | 当前宿主是否阻止 gameplay 输出面 | `false` 是正式值 | 是 | 不能由 recovery 或 prompt 状态倒推 |

补充冻结规则：

- `HostFacts` 的作用是描述宿主事实，不是发布 `PublishedGameplayPresentation` 的替身。
- `hostMode / gameplaySubstate / textEntryActive / cursorCaptured / gameplayInputBlocked` 任一变化都推进 `kernelRevision`。
- Phase 7 只能复制正式 host facts；不允许拿 menu refresh、owner、prompt 状态补写这些字段。

#### 7D. `SourceEvidence` 正式结构

`SourceEvidence` 必须与 Phase 3 的 `SourceEvidenceSnapshot` 对齐，但 Phase 7 只负责填值，不负责重定义证据模型。

| 字段 | 说明 | 合法 `Unknown / Unavailable / Empty` | 是否参与 `kernelRevision` | Phase 7 边界 |
| --- | --- | --- | --- | --- |
| `availability` | `SourceEvidence` 是否可用 | `Unknown` / `Unavailable` 合法 | 是 | 只能按 Phase 3 发布态填值 |
| `activeInputSource` | 当前原始来源证据结论 | `Unknown` 合法；不得用空串或默认枚举代替 | 是 | 不能由 owner / prompt 倒推 |
| `deviceFamilyEvidence.family` | 当前 device family | `Unknown` 合法；`Unavailable` 仅在 adapter 路径允许 | 是 | 只能来自 Phase 3 已发布 evidence |
| `deviceFamilyEvidence.deviceFamilyRevision` | 当前 device family revision | `0` 只允许与 `availability != Known` 同时出现 | 是 | 只能复制匹配的 `DeviceFamilyChanged` marker 与 `SourceEvidence` snapshot |
| `leaseState` | gameplay/menu 相关 lease 事实 | `Empty` 合法；未知时必须显式标成 `Unknown` | 是 | 只能复制，不得在 ingress 层重解释 |
| `suppressionState` | synthetic suppression / bridge 抑制事实 | `Empty` 合法；未知时必须显式标成 `Unknown` | 是 | 只能复制，不得从 helper/native 输出面反推 |
| `windowState` | 来源证据的窗口状态集合 | `Empty` 合法；未知时必须显式标成 `Unknown` | 是 | 只能复制，不得在 assembler 内新增窗口语义 |

补充冻结规则：

- `deviceFamilyEvidence.deviceFamilyRevision` 是 kernel 面的正式字段，但 Phase 7 不得直接从 `PublishedPresentationState.deviceFamilyRevision` 镜像字段取值。
- `activeInputSource / deviceFamilyEvidence / leaseState / suppressionState / windowState` 任一变化都推进 `kernelRevision`。
- Phase 7 只能搬运 Phase 3 已发布的结构与正式哨兵，不能删掉窗口/lease/suppression 中的任一内层域来简化 schema。

#### 7E. `FactHealth` 正式结构

`FactHealth` 是 ingress 健康与配对完整性的唯一记录面；它本身也是正式合同，不是日志附属品。

| 字段 | 说明 | 合法 `Unknown / Unavailable / Empty` | 是否参与 `kernelRevision` | Phase 7 边界 |
| --- | --- | --- | --- | --- |
| `sequenceGapDetected` | 本 boundary 内是否观察到 `SequenceGap` | 仅允许 `true/false`，不得省略 | 是 | 只能由显式 ingress health 事件置位 |
| `queueOverflowDetected` | 本 boundary 内是否观察到 `QueueOverflow` | 仅允许 `true/false` | 是 | 只能由显式 ingress health 事件置位 |
| `explicitResetRequested` | 是否收到 `ExplicitReset` | 仅允许 `true/false` | 是 | 只能由显式 ingress 事件置位 |
| `pendingBoundaryMarkerPair` | 是否仍等待 boundary marker 与 snapshot 配对完成 | 仅允许 `true/false` | 是 | 只能由 assembler 的配对状态机维护 |
| `boundaryMarkerMismatch` | 是否出现 marker/snapshot 不匹配 | 仅允许 `true/false` | 是 | 只能由 assembler 在 fail-closed 时置位 |
| `lastHealthyIngressSeq` | 最近一条未触发 health fault 的 `seq` | `0` 仅允许在尚未有健康事件时出现 | 是 | 只能复制 ingress 顺序，不得跳号猜值 |

补充冻结规则：

- `FactHealth` 变化本身会推进 `kernelRevision`，即使 `padSnapshot` 和其它事实不变也一样。
- `pendingBoundaryMarkerPair` 与 `boundaryMarkerMismatch` 是 Phase 7 marker 配对 fail-closed 的正式输出，不允许只打日志不入合同。
- Phase 7 不能在 transition 之外偷偷清零这些标记；只能通过下一份完成配对且成功发布的 `Stable` frame 自然复位。

#### 7F. `KernelState` 正式结构

`KernelState` 是 `InputKernel` 由 `FactFrame` 推导出来、供 `InteractionEngine` 直接消费的稳定派生态；Phase 7 不填它。

| 字段 | 说明 | 合法 `Unknown / Unavailable / Empty` | 是否参与 `kernelRevision` | Phase 7 边界 |
| --- | --- | --- | --- | --- |
| `boundaryKey` | 当前 kernel 绑定的 `IngressBoundaryKey` 镜像 | 不使用 `Unknown`/`Unavailable`；必须与 `facts.*Revision` 一一对应 | 是 | Phase 7 不得填写，由 `InputKernel` 从 `FactFrame` 构建 |
| `controlSamples` | 按 `ControlPath` 采样后的稳定控制值集合 | `Empty` 合法；未知值必须停留在 `facts.*`，不得在此重造哨兵 | 是 | Phase 7 不得填写或裁剪 |
| `pulseCursorSeq` | 当前已消化到的 `pulseLedger` 尾游标 | `0` 合法，表示当前帧无 pulse | 是 | Phase 7 不得填写 |
| `cleanBoundaryBaseline` | 当前 kernel 是否已建立 clean baseline | 仅允许 `true/false` | 是 | Phase 7 不得填写 |
| `healthDegraded` | 当前 kernel 是否因为 `FactHealth` 进入降级态 | 仅允许 `true/false` | 是 | Phase 7 不得填写 |

补充冻结规则：

- `KernelState` 只允许由 `InputKernel::BuildKernelFrame(...)` 构建；Phase 7、runtime shell、projection、backend 都不能自己生产它。
- `controlSamples / pulseCursorSeq / cleanBoundaryBaseline / healthDegraded` 任一变化都推进 `kernelRevision`。
- 如果某项事实仍是 `Unknown` 或 `Unavailable`，必须保留在 `facts.*` 与 `healthDegraded` 上体现，不能在 `KernelState` 里用私有默认值掩盖。

冻结规则：

- `FactFrame` 是 `AssembledFactFrame.facts` 与 `KernelFrame.facts` 的同一份 schema。Phase 7 只能填值，不能重写字段集。
- `AssembledFactFrame -> KernelFrame` 的唯一 owner 固定为 `src/input_v2/kernel/InputKernel.*` 中的 `BuildKernelFrame(const AssembledFactFrame&)`。`DualPadRuntime`、`InteractionEngine`、projection、backend 都不允许私自再做第二个 builder。
- `kernelRevision` 只由 `InputKernel` 单调递增生成。任一 `facts.*` 字段、`KernelState` 派生结果或 pulse ledger 内容变化，都必须推进 revision；同一份 `AssembledFactFrame` 不允许产出两个不同 revision。
- `Transition` frame 不生成 `KernelFrame`。runtime 必须先消费 `Transition` 并完成 recovery / boundary 处理，再由下一份 `Stable` `AssembledFactFrame` 构建新的 `KernelFrame`。
- `FactFrame.monotonicUs` 代表本帧最后一条 ingress 事实的时间戳；`pulseLedger[*].timestampUs` 与 `seq` 一起决定边沿顺序，不能只靠布尔按下态反推。
- `FactFrame.manifestEpoch / contextRevision / menuStackRevision / deviceFamilyRevision` 必须与 ingress boundary key 一一对应；不得在 `InputKernel` 里重写或合并。

`LegacyInteractionInputAdapter` 只在上游尚未完整落地 `KernelFrame` 时允许存在，而且能力边界现在就定死：

- 允许搬运的最小字段只有：`padSnapshot`、`pulseLedger`、`monotonicUs`、`manifestEpoch`、`contextRevision`、`menuStackRevision`、`deviceFamilyRevision`、`health`。
- 它可以把缺失的 UI / host / source 事实填成显式 `Unavailable` / `Unknown` 哨兵值，但不得根据旧 runtime 状态“猜出” owner、prompt、display、action set 或 recovery 语义。
- 它不能重新实现 `TapHoldEvaluator`、`ComboEvaluator`、`BindingResolver`、legacy coalescing，也不能生成第二套 `kernelRevision`。
- 它的删除条件固定为：一旦 `InputKernel::BuildKernelFrame(...)` 已直接消费 `AssembledFactFrame`，`LegacyInteractionInputAdapter` 必须退化为测试夹具或删除，不允许跨出 Phase 7 继续作为正式接口。

### 8. `InteractionEngine` 的冻结职责

`InteractionEngine` 是唯一 binding 相位状态机，位置在 `ActionGraph` 之后、`GameplayProjection` 之前。

建议固定到 `src/input_v2/kernel/InteractionEngine.h` 与 `src/input_v2/actions/ResolvedActionFrame.h`：

```cpp
struct ActionPhaseChange {
    ActionId actionId;
    std::uint32_t bindingId;
    ActionPhase phase;
    std::uint64_t timestampUs;
};

struct ActionValueSnapshot {
    ActionId actionId;
    ActionValueKind kind;
    float scalar;
    float x;
    float y;
};

struct ResolvedActionFrame {
    std::uint32_t manifestEpoch;
    std::uint32_t contextRevision;
    std::vector<ActionPhaseChange> changes;
    std::vector<ActionValueSnapshot> values;
    std::vector<ActionOwnershipHint> ownershipHints;
};
```

冻结规则：

- `InteractionEngine` 以 binding 为状态单元，不再以 `sourceCode` 32 个槽位为主存储模型。
- `InteractionEngine` 的输入必须是：
  - `KernelFrame`，或者临时且受限的 `LegacyInteractionInputAdapter::BuildKernelFrame(...)` 结果。
  - `CompiledActionGraph`
  - `ActionSetStack`
  - 上一帧 `InteractionStateStore`
- `InteractionEngine` 不直接调用 backend。
- `ActionLifecycleCoordinator` 只能在过渡期消费 `ResolvedActionFrame`，不得继续拥有独立交互判定逻辑。

### 9. legacy trigger lowering 的冻结规则

legacy 语法必须在编译期一次性 lowering，映射关系现在就定死：

| legacy trigger | lowered `ControlPath` | lowered `InteractionSpec` | `BindingMatchPolicy` | 默认 `DisplayBinding` |
| --- | --- | --- | --- | --- |
| `Button:Square` | `[DigitalButton(Square)]` | `Press(primary=Square)` | `PreferExactThenSubset` | `Primary`，token 来自主按钮 |
| `Layer:L1+Square` | `[DigitalButton(L1), DigitalButton(Square)]` | `Press(primary=Square, required=[L1])` | `ExactOnly` | `Primary`，显示 `L1 + Square` |
| `Combo:L1+R1` | `[DigitalButton(L1), DigitalButton(R1)]` | `Chord(primary=R1, required=[L1], unordered=true, chordWindowUs=kLegacyComboWindowUs)` | `ExactOnly` | `Primary`，组合候选；legacy token bridge 可拒绝渲染 |
| `Hold:Square` | `[DigitalButton(Square)]` | `Hold(primary=Square, holdThresholdUs=kLegacyHoldThresholdUs)` | `PreferExactThenSubset` | `Primary`，interaction label=`Hold` |
| `Tap:Square` | `[DigitalButton(Square)]` | `Tap(primary=Square, tapMaxUs=kLegacyTapThresholdUs)` | `PreferExactThenSubset` | `Primary`，interaction label=`Tap` |
| `Gesture:TpSwipeLeft` | `[TouchGesture(TpSwipeLeft)]` | `Gesture(primary=TpSwipeLeft)` | `ExactOnly` | `Hidden`，除非 manifest 明确给 token |
| `Axis:LeftStickX` | `[AnalogAxis1D(LeftStickX)]` | `Value(primary=LeftStickX)` | `ExactOnly` | `Hidden`，固定不进入默认 prompt/glyph 候选；只有 manifest 显式声明 `display_bindings` 时才允许在 Phase 6 被消费 |

补充冻结点：

- `Combo:*` 继续保持“恰好两个数字按钮、无序、窗口内重叠”的现状，不在本 slice 扩展三键 combo。
- legacy `Layer:*` 不做顺序要求，只做精确参与者要求。
- touchpad 手势沿用当前 `mapping_codes::*` 编码，不创建新枚举树。
- lowering 后的 IR 不再保存 `TriggerType`；最多只保留 `LegacyOrigin` 供 debug / migration log 使用。

## 前置依赖

- `docs/plans/dualpad_rearchitecture/02_slice_phase1_catalog_and_manifest_compiler_zh.md`
  - 必须已经产出 `ActionManifest`、`LegacyIniImporter`、`ManifestValidator` 的基本框架。
- `docs/plans/dualpad_rearchitecture/03_slice_phase2_menu_instance_truth_zh.md`
  - 必须已经产出可消费的 `ActionSetStack` 与 `contextRevision`。
- `docs/plans/dualpad_rearchitecture/04_slice_phase3_presentation_split_zh.md`
  - 必须保证 `PresentationProjection` 不再承担交互判定逻辑。
- `docs/plans/dualpad_rearchitecture/01_slice_phase0_freeze_and_replay_barrier_zh.md`
  - 必须已经有 replay / golden trace barrier，供本 slice 做回归对照。
- 如果 `KernelFrame` 还没完整落地，本 slice 只允许新增命名明确的 `LegacyInteractionInputAdapter`。
  - 它只能把当前 `SyntheticPadFrame`、现有数字边沿缓存、`contextRevision`、`menuStackRevision`、`deviceFamilyRevision`、`manifestEpoch` 映射到本页冻结的 `FactFrame / KernelFrame` 最小字段。
  - 它必须显式产出 `Unavailable` / `Unknown` 哨兵值，而不是在适配层内回填第二套 UI / owner / prompt 真相。
  - 它不能带入新的 runtime 真相，也不能成为长期接口。

## 涉及代码与文档

### 当前代码入口

- `src/input/Trigger.h`
- `src/input/BindingConfig.h`
- `src/input/BindingConfig.cpp`
- `src/input/BindingManager.h`
- `src/input/BindingManager.cpp`
- `src/input/mapping/TriggerMapper.h`
- `src/input/mapping/TriggerMapper.cpp`
- `src/input/mapping/BindingResolver.h`
- `src/input/mapping/BindingResolver.cpp`
- `src/input/mapping/TapHoldEvaluator.h`
- `src/input/mapping/TapHoldEvaluator.cpp`
- `src/input/mapping/ComboEvaluator.h`
- `src/input/mapping/ComboEvaluator.cpp`
- `src/input/backend/ActionLifecycleCoordinator.h`
- `src/input/backend/ActionLifecycleCoordinator.cpp`
- `src/input/backend/FrameActionPlanner.h`
- `src/input/backend/FrameActionPlanner.cpp`
- `src/input/injection/PadEventSnapshotProcessor.h`
- `src/input/injection/PadEventSnapshotProcessor.cpp`
- `src/input/glyph/ScaleformGlyphBridge.h`
- `src/input/glyph/ScaleformGlyphBridge.cpp`

### 本 slice 新增代码

- `src/input_v2/actions/ActionGraphTypes.h`
- `src/input_v2/actions/ActionGraphTypes.cpp`
- `src/input_v2/actions/LegacyTriggerLowering.h`
- `src/input_v2/actions/LegacyTriggerLowering.cpp`
- `src/input_v2/actions/ActionGraphCompiler.h`
- `src/input_v2/actions/ActionGraphCompiler.cpp`
- `src/input_v2/actions/CompiledActionGraph.h`
- `src/input_v2/actions/CompiledActionGraph.cpp`
- `src/input_v2/actions/ResolvedActionFrame.h`
- `src/input_v2/kernel/KernelFrame.h`
- `src/input_v2/kernel/InputKernel.h`
- `src/input_v2/kernel/InputKernel.cpp`
- `src/input_v2/kernel/InteractionStateStore.h`
- `src/input_v2/kernel/InteractionStateStore.cpp`
- `src/input_v2/kernel/InteractionEngine.h`
- `src/input_v2/kernel/InteractionEngine.cpp`
- `src/input_v2/adapters/LegacyLifecycleBridge.h`
- `src/input_v2/adapters/LegacyLifecycleBridge.cpp`

### 本 slice 新增测试

- `tests/LegacyTriggerLoweringTests.cpp`
- `tests/ActionGraphCompilerTests.cpp`
- `tests/InteractionEngineTests.cpp`
- `tests/KernelFrameContractTests.cpp`
- `tests/LegacyLifecycleBridgeTests.cpp`
- `xmake.lua`
  - 新增 `DualPadInputV2Tests` target，单独编译 Phase 4 测试，不把它们塞进旧 menu policy focused target。

### 必须对照的文档

- `docs/unified_action_lifecycle_model_zh.md`
- `docs/current_input_pipeline_zh.md`
- `docs/mapping_snapshot_atomicity_audit_and_injection_contract_zh.md`
- `docs/plans/dualpad_rearchitecture/03_slice_phase2_menu_instance_truth_zh.md`
- `docs/plans/dualpad_rearchitecture/04_slice_phase3_presentation_split_zh.md`

如需背景材料，可额外回看：

- `docs/reviews/2026-04-10-binding-action-semantics-review_zh.md`
- `docs/plans/dualpad_rearchitecture/dualpad_rearchitecture_plan_zh.md`

## 实施步骤

### 1. 建立 Phase 4 的统一 IR，不接 runtime

- 在 `src/input_v2/actions/ActionGraphTypes.*` 中一次性定义：
  - `ControlPath`
  - `BindingModifier`
  - `InteractionSpec`
  - `DisplayBinding`
  - `BindingMatchPolicy`
- 先只写类型、`ToString(...)`、hash、等值比较、debug dump。
- 此步禁止接入 `BindingConfig`、`PadEventSnapshotProcessor` 或任何旧 runtime。
- 产物标准：
  - 所有 legacy trigger 语义都能被这套类型描述。
  - 不再需要 `TriggerType::Hold / Tap / Combo / Layer` 才能表达绑定。

### 2. 实现 `LegacyTriggerLowering`，把旧语法降格为新 IR

- 在 `src/input_v2/actions/LegacyTriggerLowering.*` 中实现：
  - `LowerLegacyTrigger(const LegacyTriggerAst&, const LegacyInteractionDefaults&)`
  - `LowerLegacyDisplayBinding(...)`
- 这里要直接吃 Phase 1 的 importer AST；不要再读一遍 INI 字符串。
- 把当前散落在这些文件里的语义收口到 lowering：
  - `src/input/BindingConfig.cpp`
  - `src/input/mapping/TapHoldEvaluator.cpp`
  - `src/input/mapping/ComboEvaluator.cpp`
  - `src/input/mapping/TriggerMapper.cpp`
- 本步必须补齐单元测试，覆盖：
  - `Button / Layer / Combo / Hold / Tap / Gesture / Axis`
  - `FN + Face` 非法 chord
  - combo 必须是两个数字按钮
  - `Layer` 不做顺序语义
  - `Layer` 与 `Combo` 的 primary path / 时间窗 / display 行为差异
  - `Gesture / Axis` 默认 `Hidden` display binding

### 3. 实现 `ActionGraphCompiler`，生成不可变图与查找表

- 在 `src/input_v2/actions/ActionGraphCompiler.*` 中把 manifest + lowered bindings 编译成：
  - `CompiledActionGraph`
  - `CompiledBindingLookupTables`
  - `DisplayBinding` 索引
- 这里要把当前 runtime fallback 显式化：
  - `Button / Hold / Tap` 生成 `PreferExactThenSubset`
  - `Layer / Combo / Gesture / Axis` 生成 `ExactOnly`
- 编译失败条件现在就定死：
  - 未知 action
  - display binding priority 冲突且无显式 tie-break
  - 同一 action set 内 binding 重复
  - `Combo` 不是两键
  - legacy lowering 产出空 `ControlPath`
- 本步结束后，`BindingManager` 只允许保留为兼容装载层，不再新增功能。

### 4. 建立 `InteractionStateStore` 与控制路径采样输入

- 在 `src/input_v2/kernel/InteractionStateStore.*` 中定义 binding 级状态：
  - `pressedAtUs`
  - `holdFired`
  - `toggleLatched`
  - `lastRepeatAtUs`
  - `chordLatched`
  - `currentScalar / currentVector`
- 如果前置 slice 的 `KernelFrame` 已落地，直接消费它。
- 如果还没落地，仅允许新增 `LegacyInteractionInputAdapter`：
  - 从 `SyntheticPadFrame`、touchpad 事件、axis 状态与现有 ingress 元数据生成本页冻结的最小 `FactFrame / KernelFrame`。
  - 不允许在 adapter 里重新实现 `TapHoldEvaluator`、`ComboEvaluator` 或第二套 `kernelRevision` 生成器。
- 本步结束后，`InteractionEngine` 所需输入必须已经能按 `ControlPath` 读到状态。

### 5. 实现 `InteractionEngine`，把 binding 级激活转成 `ResolvedActionFrame`

- 在 `src/input_v2/kernel/InteractionEngine.*` 中按下面顺序实现，不能跳步：
  1. `Value` 与 `Press`
  2. `Hold` 与 `Tap`
  3. `Chord`
  4. `Repeat`
  5. `Toggle`
  6. `Gesture`
- 每实现一类交互，就补对应测试，再继续下一类。
- `InteractionEngine` 的帧内流程固定为：
  1. 读取当前 `ActionSetStack` 可见的 compiled bindings。
  2. 对每个 binding 拉取 `ControlPath` 当前值。
  3. 应用 `BindingModifier`。
  4. 评估 `InteractionSpec`。
  5. 产出 `ActionPhaseChange` 与 `ActionValueSnapshot`。
  6. 合并同一 action 的多 binding 结果，保留 bindingId 供 explainability。
- 本步禁止直接生成 `FrameActionPlan`。

### 6. 加入 `LegacyLifecycleBridge`，把新合同接回旧主线

- 在 `src/input_v2/adapters/LegacyLifecycleBridge.*` 中实现：
  - `ResolvedActionFrame -> FrameActionPlan` 的过渡映射
  - 旧 backend 所需的 lifecycle 事件封装
- `ActionLifecycleCoordinator` 的处理顺序改成：
  - 先消费 `ResolvedActionFrame`
  - 如果 Phase 4 新路径禁用，再退回旧路径
- 这一步的目标是“旧主线可运行”，不是“旧逻辑继续拥有交互真相”。
- 一旦 bridge 稳定，`TapHoldEvaluator`、`ComboEvaluator`、`TriggerMapper` 就只能保留给 fallback / parity comparison，不再当主路径。

### 7. 切换 authoritative path，并限制旧接口生存范围

- `PadEventSnapshotProcessor` 的 authoritative 路径改成：
  - 输入 -> `CompiledActionGraph`
  - `InteractionEngine`
  - `ResolvedActionFrame`
  - `LegacyLifecycleBridge`
  - `FrameActionPlan`
- 旧接口的生存范围现在就定死：
  - `BindingConfig::ParseTrigger(...)` 仅用于 legacy importer 阶段，不能再被 runtime resolver 调用。
  - `BindingManager::GetActionForTrigger(...)`、`GetTriggerForAction(...)` 仅允许出现在 parity 测试或过渡 shim。
  - `ScaleformGlyphBridge` 不得继续新增对 `Trigger` 的依赖。
- 如果这一步做完还需要在 runtime 新增 `TriggerType` 分支，说明 Phase 4 没收口，必须返工，不准进入下一 slice。

## 验证与观测

### 构建与测试

- `xmake build DualPad`
- `xmake build DualPadInputV2Tests`
- `xmake run DualPadInputV2Tests`

`DualPadInputV2Tests` 必须至少覆盖：

- `LegacyTriggerLoweringTests`
  - 7 种 legacy trigger 的 lowering 结果
  - `BindingMatchPolicy` 是否按冻结规则生成
- `ActionGraphCompilerTests`
  - unknown action fail-closed
  - display binding 冲突 fail-closed
  - combo 非两键 fail-closed
  - graph epoch 与 lookup table 稳定性
- `InteractionEngineTests`
  - hold threshold
  - tap window
  - `Layer` 的非顺序 required-path 命中
  - combo 无序窗口
  - repeat delay / interval
  - toggle latch
  - axis `Value`
- `KernelFrameContractTests`
  - `AssembledFactFrame -> KernelFrame` 只由 `InputKernel` 生成
  - `kernelRevision` 只在 facts / state 变化时推进
  - `LegacyInteractionInputAdapter` 不得生成 owner / prompt / display 真相
- `LegacyLifecycleBridgeTests`
  - `ResolvedActionFrame` 到 `FrameActionPlan` 的基本 parity
  - owning action release / recover 过渡

### replay / parity

- 使用 Phase 0 的 replay harness 回放至少这些场景：
  - gameplay 里的普通按键、长按、连发
  - 两键 combo native hotkey
  - `Menu.Accept / Menu.Cancel`
  - `Book.PreviousPage`
  - `Game.Move / Game.Look`
- Phase 4 的过渡标准不是 prompt 完全切新，而是：
  - `FrameActionPlan` 行为与旧主线在上述场景下保持等价或显式解释差异。

### 运行时观测

- 新增或复用 debug log，至少输出：
  - `manifestEpoch`
  - `contextRevision`
  - active `ActionSetStack`
  - matched `bindingId`
  - `InteractionKind`
  - emitted `ActionPhase`
- 对 parity mismatch 单独打日志，不允许静默降级。

## 退出条件

- `CompiledActionGraph` 已成为 authoritative binding truth，运行时主路径不再查 `unordered_map<Trigger, action>`。
- `CompiledActionGraphPublisher::Publish(...)` 已成为 compiled graph 热切换的唯一入口；Phase 7 不允许另造 graph publish 点。
- `InteractionEngine` 已成为 authoritative interaction truth，运行时主路径不再依赖 `TapHoldEvaluator`、`ComboEvaluator`、`TriggerMapper` 的结果。
- `ResolvedActionFrame` 已稳定，且 `PadEventSnapshotProcessor` 可以经由 `LegacyLifecycleBridge` 驱动旧 `FrameActionPlan`。
- `FactFrame / KernelFrame` 字段集与 `kernelRevision` 推进规则已按本页固定；运行时主路径不再接受匿名“等价帧”。
- `BindingManager::GetTriggerForAction(...)` 不再出现在 glyph / prompt / runtime authoritative path。
- `BindingConfig::ParseTrigger(...)` 只保留给 legacy importer 或测试。
- `DualPadInputV2Tests` 通过，且 replay / parity 对照完成并记录差异。
- 若仍然需要在 backend 中重新解释 `Hold / Tap / Combo / Toggle`，则本 slice 不算完成。

## 交接给下一 slice 的合同

交接目标是 `docs/plans/dualpad_rearchitecture/06_slice_phase5_gameplay_projection_zh.md`。

下一 slice 可以直接假设下面这些合同已经稳定：

- `CompiledActionGraph` 是不可变、可热切换、带 `manifestEpoch` 的 authoritative 图。
- `ResolvedActionFrame` 是 gameplay 决策唯一输入，不需要也不允许回头读 legacy `Trigger`。
- `InteractionEngine` 已经把 binding 激活、hold/tap/chord/repeat/toggle 语义收口。
- 后续 slice 若需要消费 ingress / frame 事实，只能沿用本页 `FactFrame / KernelFrame` 合同，不得再定义第二套字段集。
- `LegacyLifecycleBridge` 只是过渡层；Phase 5 可以继续消费它，但不能把新的业务规则写回桥里。
- `DisplayBinding` 已经在 compiler 中生成并验证；Phase 5 不改它的结构，Phase 6 直接接着消费。

下一 slice 不允许再重新决定下面这些问题：

- `Layer` 是否顺序敏感。
- `Layer / Combo` 是否共用同一套时间窗或 primary path 选择规则。
- `Combo` 是否支持三键。
- `Button / Hold / Tap` 是否保留 subset fallback。
- `DisplayBinding` 是否继续由 runtime 反查生成。

如果 Phase 5 发现这些设计不够用，必须单独开变更说明，而不是在实现时静默改合同。
