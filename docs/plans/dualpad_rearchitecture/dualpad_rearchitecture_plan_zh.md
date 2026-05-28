# DualPad 全新架构与重构方案（基于当前仓库问题与业界输入系统实践）
> 注意：本文是拆分前的历史导入副本，只保留为背景材料与旧口径对照。
> 如与 `README_zh.md` 或任一 `0*_slice_*.md` / `09*_slice_*.md` 冻结合同冲突，以拆分后的 slice 文档为准。
> 它不得作为当前执行合同、prove-out gate 或默认 CI schema 依据。

## 核心结论

不要继续以 `InputModalityTracker + ContextManager + GameplayOwnershipCoordinator + ScaleformGlyphBridge` 为中心修修补补。

新主线应改为：

**Input Kernel + Action Graph + Projections**

```text
Raw Sources / Host Facts
  -> IngressHub
  -> FrameAssembler
  -> InputKernel
      -> ContextResolver
      -> ActionSetResolver
      -> InteractionEngine
  -> ResolvedActionFrame
  -> Projections
      -> PresentationProjection
      -> GameplayProjection
      -> PromptProjection
      -> DualSenseFeatureProjection
  -> Adapters / Backends
      -> SkyrimCompatibilitySurface
      -> XInput Poll Output
      -> KeyboardHelper Output
      -> Scaleform Prompt Adapter
      -> Haptics Adapter
```

它不是“再分几个 helper”，而是把你当前混在一起的 6 个概念彻底拆开：

1. 原始输入事实
2. 当前菜单/上下文事实
3. action 解析与交互相位
4. UI/presentation 发布状态
5. gameplay owner / gate / output plan
6. glyph/prompt 解析

## 为什么这是对的

你的仓库自己已经在这个方向上露出正确直觉：

- `docs/brainstorms/2026-04-08-input-kernel-and-projection-architecture-requirements.md:10-19, 35-44` 已经提出 single authoritative kernel + presentation/gameplay/device-specific projections。
- `docs/ideation/2026-04-08-dualpad-input-kernel-switching-ideation.md:20-37` 已经指出 UI switching 和 gameplay handoff 不是同一个问题。
- `docs/ideation/2026-04-08-dualpad-ce3-architecture-ideation.md:16-39, 43-89` 已经指出 `InputModalityTracker` 是 choke point，`GameplayOwnershipCoordinator` 应变成 one-way gate plan。

我建议不是推翻这个方向，而是把它补齐成真正可运行的正式主线：

- 再增加 **Action Graph**（动作/绑定/interaction 的统一层）
- 再增加 **Menu Instance Registry / MenuStack Reconciler**（菜单实例身份真相源）
- 再增加 **PromptProjection / PromptService**（action-origin / glyph 候选层）

## 必须先改掉的根错误抽象

### 1. `InputContext` 不能再继续承载所有东西

当前 `src/input/InputContext.h` 把下面这些混成一个 enum：

- gameplay host state：`Combat / Sneaking / Riding`
- menu identity：`InventoryMenu / JournalMenu / MapMenu`
- runtime/native surface：`Cursor / Book / Favor / Lockpicking`
- generic fallback：`Menu / Unknown`

这会把：

- 菜单生命周期
- action 作用域
- glyph 查询范围
- owner policy
- gameplay 子状态

全部锁死到一个维度里。

新架构要拆成正交轴：

- `HostMode`：Gameplay / Menu / Console / Overlay
- `GameplaySubstate`：Combat / Sneaking / Riding / Death ...
- `UiContextId`：Inventory / Journal / Map / Favorites / Book ...
- `ActionSetStack`：`GameplayBase + CombatLayer`、`MenuBase + MapLayer`
- `PresentationPolicyId`：ControllerSticky / PointerFirst / Neutral / HybridCursor

### 2. `Trigger` 不能再同时表示物理控件、interaction、combo 和显示语义

当前 `src/input/Trigger.h` 里 `TriggerType` 同时包含：

- `Button`
- `Axis`
- `Gesture`
- `Combo`
- `Hold`
- `Tap`
- `Layer`

这意味着“按键本体”和“如何触发动作”被强行合并成一个 key。后果是：

- glyph 反查会误命中 gesture / hold / tap
- 配置和运行时 lookup 都会变得脆弱
- interaction semantics 不能干净地进入统一状态机

新模型必须拆成：

- `ControlPath`：物理输入（Button/Axis/TouchRegion/Gyro...）
- `BindingModifier`：deadzone / invert / scale / filter
- `InteractionSpec`：Press / Hold / Tap / Repeat / Toggle / Chord / Gesture
- `DisplayBinding`：prompt/glyph 的显示优先级和候选

## 新架构：单一真相源与投影模型

## 1. IngressHub：所有输入与宿主事实的统一入口

输入来源：

- DualSense HID 读数
- RE 菜单开关事件
- RE/UI 栈快照
- RE 输入观察事实（keyboard/mouse/gamepad）
- runtime config reload
- controlmap overlay readiness
- future device capability updates

统一包装为：

```cpp
struct IngressEvent {
    uint64_t seq;
    uint64_t monotonicUs;
    IngressSource source;
    IngressKind kind;
    Payload payload;
};
```

规则：

- 多生产者，单消费者
- 只有 `IngressHub` 负责跨线程有序搬运
- 任何 singleton 不允许在自己的线程里维护“当前上下文/当前 owner/当前 glyph 平台”

## 2. FrameAssembler：构造不可变 `FactFrame`

职责：

- 从 `IngressEvent` 组装一个主线程可消费的 `FactFrame`
- 统一挂上：
  - 原始 pad state
  - keyboard/mouse/gamepad 活动证据
  - 当前 open menu 栈快照
  - gameplay host facts
  - sequence gap / overflow / merge metadata

关键规则：

- **只允许在相同 boundary key 下做 coalescing**
- boundary key 至少包含：
  - `manifestEpoch`
  - `contextRevision`
  - `menuStackRevision`
  - `deviceFamilyRevision`
- 一旦跨 boundary，必须产出显式 transition frame，禁止吞并
- 数字边沿不能静默丢；如果 merge 窗口内发生 press+release，必须显式保留 pulse edge

## 3. InputKernel：唯一高分辨率内部真相

`InputKernel` 是真正核心。

它拥有：

- 当前内部输入族：`KeyboardMouse / GenericGamepad / DualSense / Fallback`
- 最近 source activity ledger
- 菜单实例栈 / action set 栈
- interaction 状态表
- published state 的前态引用

它不直接给 Skyrim/UI 暴露布尔值；它只输出一个不可变 `KernelFrame`。

```cpp
struct KernelFrame {
    FactFrame facts;
    KernelState state;
    uint32_t revision;
};
```

## 4. ContextResolver：菜单/上下文/action set 的唯一决策器

替掉：

- `ContextManager`
- `MenuContextPolicy`
- `InputContextNames`
- 任何 bridge 里的自行 parse context

拆成 4 个部件：

### A. `ContextCatalog`

唯一 canonical catalog：

- context id
- aliases
- menu names
- runtime fingerprint hints
- family
- default action set
- presentation policy

### B. `UiMenuObserver`

每帧或每次事件读取 UI 的真实 open menu snapshot：

- `menuPtr`
- `menuName`
- `flags`
- `inputContext`
- `depth`
- `delegatePtr`
- `moviePtr`

### C. `MenuInstanceRegistry`

根据 snapshot diff 分配 `MenuInstanceId`，而不是按名字计数。

如果真的拿不到稳定实例身份，也必须显式进入 `degraded_identity` 模式，而不是继续假装“菜单名 = 实例”。

### D. `ActionSetResolver`

根据 top menu / overlay / gameplay substate 产出：

- `activeBaseSet`
- `activeLayers[]`
- `contextRevision`

这样 `FavoritesMenu` 不再是“另一个 context + generic Menu fallback”，而是：

- `MenuBase`
- `FavoritesLayer`
- 可选 `GridNavPolicy`

## 5. Action Graph：动作、绑定、interaction 的正式主表

这是新主线的第二个核心。

它要替掉当前：

- `BindingConfig + BindingManager` 直接做 runtime truth
- `GetTriggerForAction` 这种反查
- 配置里的 silent mismatch

### 5.1 结构

```cpp
ActionManifest
  - actions
  - action_sets
  - action_layers
  - bindings
  - display_bindings
  - output_descriptors
  - policies
```

### 5.2 action 定义

每个 action 必须声明：

- `id`
- `valueKind`：digital / analog1D / analog2D / cursor / helper
- `contract`：press / hold / tap / repeat / toggle / none
- `outputDescriptor`
- `promptHint`
- `domain`

### 5.3 binding 定义

binding 是：

- set/layer
- control path
- modifiers
- interactions
- actionId
- scheme / device family 约束

### 5.4 display binding 定义

prompt 不再从 runtime binding hash map 逆推。

而是提前编译出：

- primary display binding
- alternates
- hidden/non-displayable binding
- device-specific token translation rule

### 5.5 legacy config 兼容

现有 `DualPadBindings.ini` 先通过 `LegacyIniImporter` 转成 manifest AST：

- `Hold:Square = X` 变成 `control=Square + interaction=Hold`
- `Combo:L1+R1 = X` 变成 chord interaction
- `Tap:*` / `Gesture:*` 都变成 interaction spec，而不是 runtime lookup key

## 6. InteractionEngine：统一相位状态机

你现在的 `TapHoldEvaluator + ComboEvaluator + ActionLifecycleCoordinator` 思路不是错，但位置还不够干净。

新架构里它应成为标准 action-phase engine：

- `Waiting`
- `Started`
- `Performed`
- `Canceled`
- `Ongoing`

它只基于：

- `KernelFrame`
- `ActionGraph`
- 上一帧 interaction state

输出：

```cpp
struct ResolvedActionFrame {
    vector<ActionPhaseChange> changes;
    vector<ActionValue> currentValues;
    vector<ActionOwnershipHint> hints;
};
```

backend 不再理解 hold/tap/repeat/toggle。

## 7. Projection 层：只读 kernel，只发布自己的真相

### 7.1 PresentationProjection

唯一允许决定 UI 平台/光标/菜单 refresh 的模块。

输出：

```cpp
struct PublishedPresentationState {
    DeviceFamily family;              // collapsed
    PresentationOwner owner;          // controller vs KBM
    NavigationOwner navigationOwner;
    CursorOwner cursorOwner;
    PointerIntent pointerIntent;
    UiContextId uiContext;
    ActionSetStack actionSets;
    uint32_t epoch;
    DirtyFlags dirty;
    DecisionReason reason;
};
```

它消费：

- kernel 内部高分辨率 family state
- context policy
- source evidence taxonomy（strong / weak / auxiliary）

它不读 gameplay backend，不读 glyph bridge，不读 keyboard helper side effects。

### 7.2 GameplayProjection

唯一允许决定 gameplay owner / suppression / cancel / materialization plan 的模块。

输出：

```cpp
struct GameplayProjectionFrame {
    ChannelOwner lookOwner;
    ChannelOwner moveOwner;
    ChannelOwner combatOwner;
    ChannelOwner digitalOwner;
    GamepadOutputPlan gamepadPlan;
    KeyboardHelperOutputPlan helperPlan;
    GatePlan gatePlan;
    RecoveryPlan recoveryPlan;
    DecisionReasonByChannel reasons;
};
```

它只读：

- `KernelFrame`
- `ResolvedActionFrame`
- `GameplayPolicy`
- 上一帧 `GameplayProjectionFrame`

它绝不直接调 backend 做副作用。

### 7.3 PromptProjection / PromptService

唯一允许决定 glyph/prompt 的模块。

输出：

```cpp
struct PromptDescriptor {
    bool ok;
    ActionId action;
    ActionSetId resolvedSet;
    PromptCandidate primary;
    vector<PromptCandidate> alternates;
    PromptResolutionSource source;
    PromptFallbackReason fallback;
    uint32_t contextRevision;
    uint32_t manifestEpoch;
};
```

`PromptCandidate` 至少包含：

- `bindingId`
- `origin`
- `token`
- `localizedLabel`
- `deviceProfile`
- `priority`

这直接解决：

- 一个 action 多个 trigger 的 nondeterminism
- gesture/axis 误做 button glyph
- generic Menu silent fallback
- future device art 扩展

### 7.4 DualSenseFeatureProjection

独立处理：

- `isDualSenseActive`
- adaptive trigger availability
- haptics availability
- future DS-specific capabilities

它不参与 `IsUsingGamepad`，也不污染 glyph/presentation owner。

## 8. CompatibilitySurface：把旧接口降格成适配器

### 8.1 `SkyrimCompatibilitySurface`

只做：

- `IsUsingGamepadHook()` -> 读 `PublishedPresentationState`
- `GamepadControlsCursorHook()` -> 读 `PublishedPresentationState`
- `BSPCGamepadDeviceHandler::IsEnabled()` -> 读同一状态
- menu refresh -> 只响应 `PresentationProjection` 的 dirty epoch

### 8.2 `ScaleformPromptAdapter`

替代今天的 `ScaleformGlyphBridge` 重逻辑。

旧接口保留：

- `DualPad_GetActionGlyphToken(actionId, contextName)`
- `DualPad_GetActionGlyph(actionId, contextName)`

但语义改成：

- contextName 只在 adapter 层 resolve，一旦无效明确报错
- 真正 prompt 解析只走 `PromptService`
- 旧 token API 只是 `primary.token` 的兼容包装
- 新 API 增加 `GetActionGlyphCandidates` / `GetPromptDescriptor`

### 8.3 `PollOutputAdapter`

`NativeButtonCommitBackend`、`AxisProjection`、`KeyboardHelperBackend` 继续保留，但它们只消费 projection plan。

## 原有功能如何完整覆盖

### 1. DualSense HID / upstream poll / XInput bridge

保留：

- `HidReader`
- `hid/*`
- `UpstreamGamepadHook`
- `AuthoritativePollState`
- `XInputStateBridge`

变化：

- 这些模块从“半理解业务语义”退回“执行输出计划”。

### 2. 菜单上下文、unknown menu、menu stack

保留功能，彻底重做实现：

- known menu 映射
- runtime classification
- passthrough overlay
- unknown menu policy

但全部进入：

- `ContextCatalog`
- `MenuClassifier`
- `MenuInstanceRegistry`
- `ActionSetResolver`

### 3. 动态 glyph

保留并加强：

- 主菜单
- Journal / Map / Favorites / Book / future menus
- device-specific token translation
- fallback art
- multi-origin candidate handling

### 4. native current-state materialization

保留并更稳定：

- `FrameActionPlan` 概念仍可保留，但应演进为更纯粹的 `ResolvedActionFrame + GameplayOutputPlan`
- `NativeButtonCommitBackend` / `AxisProjection` 继续做 translator

### 5. keyboard helper / ModEvent / combo-native hotkeys

保留：

- `KeyboardHelperBackend`
- `CustomActionDispatcher`
- controlmap overlay

但改为 action manifest 的 output descriptor 驱动，不再由零散 config/bridge/self-owned states 驱动。

### 6. future DS-specific haptics / triggers

保留，而且更容易扩展，因为它拥有独立 feature projection。

## 代码级重构映射

### 保留并改造的模块

- `HidReader.*`
- `src/input/hid/*`
- `PadEventSnapshot.*`（可保留 transport 类型，但语义升级）
- `SyntheticStateReducer.*`（可保留 reduced raw frame 能力）
- `NativeButtonCommitBackend.*`
- `AxisProjection.*`
- `KeyboardHelperBackend.*`
- `AuthoritativePollState.*`
- `XInputStateBridge.*`
- `ControlMapOverlay.*`
- `CustomActionDispatcher.*`
- `HapticsSystem.*`

### 拆分替换的模块

- `InputModalityTracker.*`
  - -> `SourceEvidenceCollector`
  - -> `PresentationProjection`
  - -> `SkyrimCompatibilitySurface`
- `ContextManager`（在 `InputContext.*` 内）
  - -> `UiMenuObserver`
  - -> `MenuInstanceRegistry`
  - -> `ContextResolver`
- `MenuContextPolicy.*`
  - -> `ContextCatalog + MenuClassifier`
- `InputContextNames.*`
  - -> generated `ContextCatalog`
- `GameplayOwnershipCoordinator.*`
  - -> `GameplayProjectionResolver`
- `ScaleformGlyphBridge.*`
  - -> `ScaleformPromptAdapter + PromptService`
- `BindingConfig.* + BindingManager.*`
  - -> `LegacyIniImporter + ActionManifestLoader + BindingCompiler + CompiledBindingTables`
- `PadEventSnapshotDispatcher.*`
  - -> `IngressHub + FrameAssembler`
- `PadEventSnapshotProcessor.*`
  - -> orchestration shell：调用 kernel、interaction engine、projections、executors

### 应直接删除的旧逻辑

- `GetTriggerForAction()` 作为 glyph 主路径
- 所有 `ParseInputContextName(...).value_or(Menu)` 类型 silent fallback
- `passthroughMenuCounts`
- owner coordinator 直接调 backend 的副作用入口
- context/glyph/presentation 的重复 alias 表

## 推荐目录结构

```text
src/input_v2/
  runtime/
    DualPadRuntime.*
  ingress/
    IngressEvent.*
    IngressHub.*
    FrameAssembler.*
  kernel/
    KernelTypes.*
    KernelState.*
    SourceEvidenceResolver.*
    InteractionEngine.*
  context/
    ContextCatalog.*
    UiMenuObserver.*
    MenuInstanceRegistry.*
    MenuClassifier.*
    ContextResolver.*
    ActionSetResolver.*
  actions/
    ActionManifest.*
    BindingCompiler.*
    CompiledBindingTables.*
    ResolvedActionFrame.*
  projections/
    PresentationProjection.*
    GameplayProjection.*
    PromptProjection.*
    DualSenseFeatureProjection.*
  adapters/
    SkyrimCompatibilitySurface.*
    ScaleformPromptAdapter.*
    PollOutputAdapter.*
    KeyboardHelperAdapter.*
    HapticsAdapter.*
  config/
    LegacyIniImporter.*
    ManifestValidator.*
    AtomicConfigReloader.*
  telemetry/
    InputTraceRecorder.*
    DecisionExplain.*
    ReplayHarness.*
```

## 完整重构路线

## Phase 0：先把当前行为冻结成可回放资产

目标：给后续所有切换建立 regression barrier。

产物：

- `InputTraceRecorder`
- `ReplayHarness`
- golden trace 集
- 当前 exported glyph API 的 black-box 结果集
- native poll state / keyboard helper output / menu platform output diff 工具

至少录这些场景：

- gameplay 行走/攻击/格挡/冲刺
- gameplay -> menu -> gameplay
- Main Menu glyph
- Journal 确认/取消
- Map cursor / zoom / open journal
- Favorites 左右切页/接受/取消
- Book 左右翻页
- Console / Creations / Lockpicking
- combo-native pause/screenshot/hotkeys
- backlog / sequence gap / overflow
- config reload 成功 / 失败

## Phase 1：先落地 canonical catalog 与 manifest compiler

目标：先把“名字、别名、action、binding、prompt、policy”统一到一个编译产物。

做法：

- 引入 `ContextCatalog`
- 引入 `ActionManifest`
- 写 `LegacyIniImporter`，把现有两个 INI 转成 AST
- 写 `ManifestValidator`
- 实现 scratch compile + atomic swap + last-known-good 保留

硬约束：

- unknown action -> load fail
- duplicate alias -> load fail
- ambiguous display binding -> load fail 或要求显式 priority
- inheritance cycle -> load fail
- 同名 action 跨 set 冲突 -> load fail

## Phase 2：重建菜单实例真相源

目标：彻底废掉 name-only menu stack。

做法：

- 加 `UiMenuObserver`
- 每帧拿 UI snapshot
- `MenuInstanceRegistry` 做 diff
- `ContextResolver` 产出 `UiContextId + ActionSetStack + contextRevision`

这个阶段完成后：

- `ContextManager` 只做镜像对照，不再 authoritative
- `MenuContextPolicy` 只剩 legacy importer 的历史作用

## Phase 3：拆 `InputModalityTracker`

目标：去掉那个超级 choke point。

拆成：

- `SourceEvidenceCollector`
- `PresentationProjection`
- `SkyrimCompatibilitySurface`

规则：

- hooks 只读 `PublishedPresentationState`
- gameplay facts 不再塞进 UI presentation 模块
- menu open/close 对称
- 任何 refresh 都由 dirty epoch 驱动

## Phase 4：把 trigger 语义迁移到 Action Graph + InteractionEngine

目标：不再让 runtime 用“混合 trigger key”做 binding/glyph/interaction。

做法：

- legacy trigger parser 改成 compile-time lowering
- `Hold/Tap/Combo/Gesture` 编译为 interaction spec
- 引入 `ResolvedActionFrame`
- `ActionLifecycleCoordinator` 逐步退化成兼容壳层，最后删掉或并入 interaction engine

## Phase 5：GameplayProjection 一次性取代 coordinator feedback loop

目标：owner/gate/cancel 变成 one-way projection。

做法：

- `GameplayProjectionResolver` 从 `KernelFrame + ResolvedActionFrame` 算出 per-channel owners
- 直接产出 `GamepadOutputPlan / KeyboardHelperOutputPlan / GatePlan / RecoveryPlan`
- `PadEventSnapshotProcessor` 变成 projection executor
- 删除 `GameplayOwnershipCoordinator -> backend` 的直接 side effect

关键要求：

- reset/resync 统一作用到 native/helper/custom output
- gameplay domain 不再只认 exact `Gameplay`，而认明确的 gameplay host + layer set

## Phase 6：PromptProjection / PromptService 切换

目标：glyph 不再反查 unordered_map。

做法：

- 预编译 `display_bindings`
- 运行时按 `ActionSetStack + device profile + prompt scope` 查 `PromptDescriptor`
- 旧 `DualPad_GetActionGlyphToken` 保留，但只是 wrapper
- 对 invalid contextName 显式返回 `UnknownContext`，不再 `value_or(Menu)`

这个阶段完成后：

- `ScaleformGlyphBridge` 只剩 delegate 注册和参数编解码
- `TriggerToButtonArtToken` 退化成 `PromptOriginTranslator` 的一部分

## Phase 7：重做 dispatcher/coalescing/resync

目标：解决跨边界 snapshot 丢边沿问题。

做法：

- 引入 `IngressHub + FrameAssembler`
- 仅同 boundary key 允许 merge
- 显式保留 transition frame
- overflow / gap 进入统一 recovery
- helper backend 与 native backend 一起 reset/resync

## Phase 8：删旧主线，收口文档与 CI

目标：不保留第二套 formal mainline。

做法：

- 删除：
  - `InputModalityTracker` old responsibilities
  - `ContextManager`
  - `MenuContextPolicy` runtime authority
  - `BindingManager` runtime authority
  - reverse glyph lookup
  - duplicate alias maps
- 文档改成 generated + reviewed
- CI 增加 replay / property / fuzz / prompt snapshot tests

## 测试与验证方案

## 1. Replay 测试

每次变更都回放 golden trace，比较：

- `PublishedPresentationState`
- `GameplayProjectionFrame`
- `AuthoritativePollState`
- `KeyboardHelperOutputPlan`
- `PromptDescriptor`

## 2. Property/Fuzz 测试

### menu stack

随机生成：

- 同名菜单重复打开
- overlay + tracked 混合
- out-of-order close
- snapshot diff 缺失/迟到

验证：

- stack 不泄漏
- top context 一致
- unresolved case 必须显式标记

### action graph

随机生成：

- 多 binding 指向同一 action
- explicit prompt override
- parent/base layer override
- invalid alias / cycle

验证：

- compile determinism
- prompt determinism
- fail-closed 行为

### backlog/coalescing

随机生成：

- burst inputs
- context boundary during backlog
- sequence gap + reload

验证：

- boundary 不吞
- recovery 可解释
- helper/native reset 一致

## 3. Contract 测试

给每个公开接口建黑盒断言：

- `IsUsingGamepad`
- `GamepadControlsCursor`
- `BSPCGamepadDeviceHandler::IsEnabled`
- `DualPad_GetActionGlyphToken`
- `DualPad_GetActionGlyph`
- mod event 输出
- combo-native outputs

## 4. Explainability 测试

每个关键输出都必须能解释：

- 为什么当前是这个 UI 平台
- 为什么某个 channel owner 是手柄/KBM
- 为什么 glyph 选中了这个 origin/token
- 为什么本帧 suppress / cancel 某动作

## 文档治理

当前你最大的问题之一不是“没文档”，而是“文档不是单一真相源”。

新架构要加两件事：

1. 从 `ContextCatalog + ActionManifest` 自动生成：
   - `docs/generated/context_catalog_zh.md`
   - `docs/generated/action_sets_zh.md`
   - `docs/generated/prompt_matrix_zh.md`
   - `docs/generated/policies_zh.md`
2. CI 检查 generated docs 是否与运行时编译表一致

这样 README / DOC_INDEX 只能总结，不再手写事实表。

## 我最强烈的实现建议

如果只能给一条最重要建议：

**把“当前系统中心”从 `InputModalityTracker` 挪到 `InputKernel`，再把 glyph 从 reverse lookup 改成 `PromptService`。**

这是最关键的两个根修：

- 前者解决 context/owner/presentation 的多真相源
- 后者解决 glyph/action/binding 的错误抽象

做完这两件事，你现在那些看似分散的问题——

- menu stack identity
- unknown menu passthrough
- gameplay/menu seed
- close 时序残留
- glyph fallback 分叉
- multi-binding nondeterminism
- coalescing 跨边界丢语义
- 文档漂移

才会一起开始收敛，而不是继续在不同角落长 patch。
