# Phase 5 切片：GameplayProjection 取代 coordinator feedback loop

## 目标

- 把 gameplay 侧的 owner、gate、cancel、recovery 决策统一收口到 `GameplayProjection`，禁止再由 runtime shell、backend 或 legacy coordinator 临场补判。
- 冻结 `GameplayProjectionFrame` 及其子计划结构，使本阶段结束后，开发者只需要沿着“解析输入 -> 产出 projection frame -> 执行 frame”三步推进。
- 把当前 `GameplayOwnershipCoordinator -> PadEventSnapshotProcessor -> NativeButtonCommitBackend / KeyboardHelperBackend` 的 feedback loop 改成单向数据流。
- 明确 `Sprint` 这类 sustained digital 不再走 `DigitalOwner` family gate，而是走 source aggregation。
- 本 slice 不负责 prompt / glyph 解析，不负责 ingress / coalescing 边界检测重做，不把 gameplay presentation lease 重新塞回 gameplay projection。

## 冻结的设计决定

### 1. `GameplayProjection` 的输入与职责边界固定

- 输入固定为：
  - `KernelFrame`
  - `ResolvedActionFrame`
  - `GameplayPolicy`
  - 上一帧 `GameplayProjectionFrame`
  - `GameplayRecoveryInput`
- 输出固定为：
  - `GameplayProjectionFrame`
  - `GameplayProjectionFrame.presentationPlan`
- `GameplayProjection` 是纯决策层：
  - 不直接调用 `NativeButtonCommitBackend`
  - 不直接调用 `KeyboardHelperBackend`
  - 不直接写 `AuthoritativePollState`
  - 不直接发布 presentation / glyph 状态，但必须把 Phase 3 所需的 gameplay presentation 输入冻结到
    `GameplayProjectionFrame.presentationPlan`
- side effect 只能由 `GameplayProjectionExecutor` 或 `PollOutputAdapter` 执行。

### 2. `GameplayProjectionFrame` 与子计划结构现在冻结

```cpp
enum class ChannelOwner : std::uint8_t
{
    Gamepad,
    KeyboardMouse
};

enum class AnalogGateMode : std::uint8_t
{
    Open,
    ZeroedByKeyboardMouse
};

enum class DigitalGateMode : std::uint8_t
{
    Open,
    SuppressNewTransient,
    CancelAndSuppressNewTransient
};

enum class RecoveryMode : std::uint8_t
{
    None,
    SoftResyncOutputs,
    HardResetOutputs
};

enum class GameplayReasonCode : std::uint8_t
{
    None,
    NonGameplayContext,
    CarryPreviousOwner,
    MouseLookActive,
    MeaningfulRightStick,
    KeyboardMoveActive,
    MeaningfulLeftStick,
    KeyboardMouseCombatActive,
    MeaningfulTrigger,
    KeyboardMouseTransientDigitalActive,
    GamepadTransientDigitalActive,
    SoftResync,
    HardReset
};

enum class GameplayPresentationReasonCode : std::uint8_t
{
    None,
    CarryLookOwner,
    CarryMoveOwner,
    CarryCombatOwner,
    CarryDigitalOwner,
    NonGameplayContext,
    RecoveryRepublish
};

template <class T, std::size_t N>
struct FixedCommandList
{
    std::array<T, N> items{};
    std::size_t count{ 0 };
};

enum class HelperOutputKind : std::uint8_t
{
    KeyboardKey,
    ModEvent
};

enum class SustainedSourceBit : std::uint8_t
{
    None = 0,
    GamepadResolved = 1 << 0,
    KeyboardPhysical = 1 << 1,
    MousePhysical = 1 << 2
};

struct NativeTransientCommand
{
    ActionId actionId{};
    NativeControlCode control{ NativeControlCode::None };
    ActionPhase phase{ ActionPhase::None };
    ActionOutputContract contract{ ActionOutputContract::None };
    bool gateAware{ false };
    std::uint32_t contextRevision{ 0 };
};

struct NativeSustainedCommand
{
    ActionId actionId{};
    NativeControlCode control{ NativeControlCode::None };
    std::uint8_t activeSourceMask{ 0 };
    ActionOutputContract contract{ ActionOutputContract::None };
    std::uint32_t contextRevision{ 0 };
};

struct HelperOutputCommand
{
    ActionId actionId{};
    HelperOutputKind kind{ HelperOutputKind::KeyboardKey };
    std::uint16_t helperCode{ 0 };
    ActionPhase phase{ ActionPhase::None };
    ActionOutputContract contract{ ActionOutputContract::None };
    float heldSeconds{ 0.0f };
    std::uint32_t contextRevision{ 0 };
};

struct GamepadOutputPlan
{
    ProjectedAnalogState analog{};
    FixedCommandList<NativeTransientCommand, 32> transientDigital{};
    FixedCommandList<NativeSustainedCommand, 8> sustainedDigital{};
};

struct KeyboardHelperOutputPlan
{
    FixedCommandList<HelperOutputCommand, 24> commands{};
    bool enqueueBridgeResetBeforeApply{ false };
};

struct GatePlan
{
    AnalogGateMode lookGate{ AnalogGateMode::Open };
    AnalogGateMode moveGate{ AnalogGateMode::Open };
    AnalogGateMode leftTriggerGate{ AnalogGateMode::Open };
    AnalogGateMode rightTriggerGate{ AnalogGateMode::Open };
    DigitalGateMode transientDigitalGate{ DigitalGateMode::Open };
};

struct RecoveryPlan
{
    RecoveryMode mode{ RecoveryMode::None };
    bool resetNativeCommitBackend{ false };
    bool resetKeyboardHelperBackend{ false };
    bool resetSustainedDigitalAggregator{ false };
    bool clearProjectionStickyOwners{ false };
    bool clearRecoveryBaseline{ false };
    bool commitCleanRecoveryBaselineAfterApply{ false };
};

struct DecisionReasonByChannel
{
    GameplayReasonCode look{ GameplayReasonCode::None };
    GameplayReasonCode move{ GameplayReasonCode::None };
    GameplayReasonCode combat{ GameplayReasonCode::None };
    GameplayReasonCode digital{ GameplayReasonCode::None };
    GameplayReasonCode recovery{ GameplayReasonCode::None };
};

struct GameplayPresentationPlan
{
    PresentationOwner engineOwner{ PresentationOwner::KeyboardMouse };
    PresentationOwner menuEntryOwner{ PresentationOwner::KeyboardMouse };
    GameplayPresentationReasonCode reason{ GameplayPresentationReasonCode::None };
};

struct GameplayProjectionFrame
{
    LegacyInputContextCompat context{ LegacyInputContextCompat::Gameplay };
    std::uint32_t contextRevision{ 0 };
    ChannelOwner lookOwner{ ChannelOwner::KeyboardMouse };
    ChannelOwner moveOwner{ ChannelOwner::KeyboardMouse };
    ChannelOwner combatOwner{ ChannelOwner::KeyboardMouse };
    ChannelOwner digitalOwner{ ChannelOwner::KeyboardMouse };
    GamepadOutputPlan gamepadPlan{};
    KeyboardHelperOutputPlan helperPlan{};
    GatePlan gatePlan{};
    RecoveryPlan recoveryPlan{};
    GameplayPresentationPlan presentationPlan{};
    DecisionReasonByChannel reasons{};
};
```

### 2A. `GameplayProjectionFrame.context` 的 compatibility type 归宿现在冻结

`GameplayProjectionFrame.context` 从本页起不再依赖 `src/input/InputContext.*` 的 owning files。

- 该字段固定改用 `LegacyInputContextCompat`，建议定义在：
  - `src/input_v2/compat/LegacyInputContextCompat.h`
- `LegacyInputContextCompat` 的边界现在写死：
  - 它只承担 replay、黑盒兼容、debug dump、文档导出仍需使用的 legacy context 标签
  - 它不是 `ContextManager`、`MenuContextPolicy` 或任何 runtime authority 的一部分
  - `GameplayProjectionFrame` 的 authoritative 判定仍然来自 `contextRevision`、owner / gate / recovery plan 与上游 published seam；任何消费者都不得从这个 compat enum 反推新的 runtime 决策
- `09a` 删除顺序固定为：
  - 先把 `GameplayProjectionFrame` 与剩余消费者迁到 `LegacyInputContextCompat`
  - 再物理删除 `src/input/InputContext.h/.cpp` 与 `src/input/InputContextNames.h/.cpp`
- `LegacyInputContextCompat` 允许在 Phase 8 之后继续保留，但只能是 compatibility type：
  - 它不计入 legacy authority
  - 只有当 replay、docgen、public black-box surface 都不再输出 legacy context 标签时，才允许在后续 compatibility cleanup 中删除

### 2B. Phase 3 / Phase 5 共享的 `PublishedGameplayPresentation` seam 现在冻结

Phase 5 现在必须补上一条正式 published seam，专门替换 coordinator 旧的 presentation API。
`PresentationProjection` 在 Phase 3 已经冻结为只接受这条 seam；Phase 5 不允许再把
`GameplayProjectionFrame` 直接塞给 `PresentationProjection`。

```cpp
struct PublishedGameplayPresentation
{
    PresentationOwner engineOwner{ PresentationOwner::KeyboardMouse };
    PresentationOwner menuEntryOwner{ PresentationOwner::KeyboardMouse };
    std::uint32_t gameplayPresentationRevision{ 0 };
    GameplayPresentationReasonCode reason{ GameplayPresentationReasonCode::None };
    std::uint64_t publishedTick{ 0 };
};
```

固定 producer / owner / 消费顺序如下：

1. `GameplayProjection` 只负责给出 `GameplayProjectionFrame.presentationPlan`，
   不直接递增 `gameplayPresentationRevision`。
2. `GameplayPresentationPublisher` 是 Phase 5 之后唯一允许发布
   `PublishedGameplayPresentation` 的对象，固定落在
   `src/input_v2/runtime/DualPadRuntime.*` 中命名明确的
   `DualPadRuntime::PublishGameplayPresentation(...)`。
   - 不允许再出现“等价 runtime 发布点”
   - 不得放回 `GameplayOwnershipCoordinator`
   - `PadEventSnapshotProcessor`、`PollOutputAdapter`、backend、projection、hook façade
     都不得直接发布 `PublishedGameplayPresentation`
3. `GameplayPresentationPublisher` 的发布顺序固定为：
   - `ResolveGameplayProjection(...)` 产出 `GameplayProjectionFrame`
   - `PollOutputAdapter` 成功执行本帧 `RecoveryPlan + output plan`
   - `GameplayPresentationPublisher` 依据 `presentationPlan` 发布一份
     `PublishedGameplayPresentation`
   - Phase 3 的 `GameplayPresentationAdapter` 在下一次 `PresentationProjection` tick 里读取它
4. `gameplayPresentationRevision` 只在下面情况递增：
   - `engineOwner` 变化
   - `menuEntryOwner` 变化
   - `reason` 变化
   - `RecoveryPlan.mode == HardResetOutputs` 且 runtime 要求重新发布 clean gameplay baseline
5. `menuEntryOwner` 的冻结语义是：
   - 取最近一次 clean gameplay frame 的 `engineOwner`
   - 由 `GameplayPresentationPublisher` 在输出执行成功后提交
   - 菜单链、hook 层、`PresentationProjection` 都不得反写它
6. Phase 5 cutover 期间，如需用旧 coordinator 结果做对照，只允许存在命名固定的
   `CoordinatorGameplayPresentationBridge`。
   - 它只允许发布同结构的 `PublishedGameplayPresentation`
   - 不允许继续暴露旧 API 给 `PresentationProjection`

### 2B. `Command List Overflow Policy` 现在冻结

`FixedCommandList<NativeTransientCommand, 32>`、
`FixedCommandList<NativeSustainedCommand, 8>`、
`FixedCommandList<HelperOutputCommand, 24>` 是本 slice 的硬上限，不允许动态扩容或静默截断。

固定策略如下：

1. compile-time 约束：
   - 这 3 个容量是 Phase 5 的 ABI 合同
   - 任何新增 fan-out、额外 lowering 或 helper 路由若会突破上限，必须在同一轮同时修改：
     - 结构体容量
     - 本切片文档
     - replay / property / golden 资产
   - 不允许“先临时截断，后续再调容量”
2. runtime append 约束：
   - 只能通过单一路径 `TryAppend...` 进入各 `FixedCommandList`
   - 一旦任一列表 overflow，本帧必须 fail-closed，不允许保留前面已写入的部分命令继续下发
3. overflow 时的固定动作：
   - 记录一条 `[DualPad][GameplayProjectionOverflow]` 日志，至少包含
     `listKind`、`capacity`、`actionId/helperCode`、`contextRevision`、`frameContext`
   - 把 `gamepadPlan.transientDigital.count`、`gamepadPlan.sustainedDigital.count`、
     `helperPlan.commands.count` 全部清零
   - 强制设置：
     - `RecoveryPlan.mode = HardResetOutputs`
     - `resetNativeCommitBackend = true`
     - `resetKeyboardHelperBackend = true`
     - `resetSustainedDigitalAggregator = true`
     - `clearProjectionStickyOwners = true`
   - `helperPlan.enqueueBridgeResetBeforeApply = true`
   - `reasons.recovery = GameplayReasonCode::HardReset`
4. executor 不允许部分应用 overflow 前已经接受的命令；overflow 帧只能走 hard reset 路径。

### 3. `DigitalOwner` 的语义现在固定

- `digitalOwner` 只治理以下集合：
  - gameplay domain
  - native gamepad digital
  - `gateAware = true`
  - `digital class = transient`
- 当前 legacy 语义对应关系固定为：
  - `PulseMinDown` -> transient
  - `ToggleDebounced` -> transient
  - `HoldOwner` -> sustained
  - `RepeatOwner` -> sustained
- `Sprint` 从这一刻起不再属于 `DigitalOwner` family gate。
- `Sprint` 与后续所有 sustained gameplay digital 一律进入 `GamepadOutputPlan.sustainedDigital`，由 source aggregation 决定有效 held 状态。

### 4. `KeyboardHelperOutputPlan` 不受 `DigitalOwner` 直接治理

- `KeyboardHelperOutputPlan` 只消费已经在 `ResolvedActionFrame` 中完成 contract 解析的 helper 命令。
- gameplay owner / gate 的目标是防止同一 gameplay 硬件通道双写，不是阻断 helper 路由。
- helper 侧只受两类约束：
  - action domain / output descriptor 自身约束
  - `RecoveryPlan` 中的 reset 指令

### 5. `RecoveryPlan` 的责任固定

- `GameplayProjection` 只决定输出面 recovery，不重做 ingress gap 检测。
- `GameplayRecoveryInput` 的来源固定是 runtime shell；Phase 7 只能改变它如何被填充，不能改 `RecoveryPlan` 的结构和消费顺序。
- `RecoveryPlan` 的执行顺序固定为：
  1. 清理输出面状态
  2. 清理 sustained aggregator
  3. 清理 projection sticky owner
  4. 执行本帧 `GamepadOutputPlan` / `KeyboardHelperOutputPlan`
  5. 若 `commitCleanRecoveryBaselineAfterApply = true`，提交 clean baseline

### 6. 旧 `GameplayOwnershipCoordinator` 的切除顺序现在固定

1. 先落 `GameplayProjectionFrame.presentationPlan`、`GameplayPresentationPublisher` 与
   `PublishedGameplayPresentation` seam。
   - 此时 coordinator presentation API 仍可只读保留
   - 若需要新旧对照，只允许通过 `CoordinatorGameplayPresentationBridge` 发布同结构 seam
2. 再把 Phase 3 的 `GameplayPresentationAdapter` producer 从
   `CoordinatorGameplayPresentationBridge` 切到 `GameplayPresentationPublisher`。
   - 切换前必须完成 parity
   - 切换后 `PresentationProjection` 只能继续读 `PublishedGameplayPresentation`
3. 只有在 Phase 3 adapter 已经稳定改读新 seam 后，才允许删除 coordinator presentation API：
   - `PresentationHint`
   - `GameplayPresentationState`
   - `RecordGameplayPresentationHint(...)`
   - `RefreshPublishedGameplayPresentation(...)`
   - `GetPublishedGameplayPresentationState()`
   - `GetPublishedGameplayPresentationOwner()`
   - `GetPublishedGameplayMenuEntryOwner()`
   - 所有 lease 字段
4. 再切掉 runtime shell 中的 owner / gate 决策调用：
   - `UpdateDigitalOwnership(...)`
   - `ApplyOwnership(...)`
5. 再切 debug / probe 读取口：
   - `GetPublishedLookOwner()`
   - `GetPublishedDigitalOwner()`
   - 以及任何直接面向 presentation 的 probe
6. 最后删除 `GameplayOwnershipCoordinator.*` 本体与所有 include。
- 禁止出现“`GameplayProjection` 已经生效，但 Phase 3 仍直接读取 coordinator presentation API”的并存状态。
- 如果执行 Phase 5 时仓库里还残留 `InputModalityTracker <-> GameplayOwnershipCoordinator` 的
  gameplay presentation 读写，本 slice 必须先删这条依赖；不允许把 lease 逻辑搬进
  `GameplayProjection` 临时续命。

## 前置依赖

- `docs/plans/dualpad_rearchitecture/05_slice_phase4_action_graph_and_interaction_engine_zh.md`
  - 必须已经冻结 `ResolvedActionFrame`、`ActionId`、`ActionPhase`、compiled output descriptor。
- `docs/plans/dualpad_rearchitecture/04_slice_phase3_presentation_split_zh.md`
  - `PresentationProjection` 必须已经只接受 `PublishedGameplayPresentation`
  - `GameplayPresentationAdapter` 当前可以仍由 coordinator 供数，但消费者不能再依赖旧 API 形态
- `docs/plans/dualpad_rearchitecture/01_slice_phase0_freeze_and_replay_barrier_zh.md`
  - 必须已经有 replay / golden trace barrier，至少覆盖 gameplay handoff、menu 往返、resync。
- 当前事实文档必须先对齐：
  - `docs/current_input_pipeline_zh.md`
  - `docs/backend_routing_decisions.md`
  - `docs/unified_action_lifecycle_model_zh.md`
- 如需回看 gameplay ownership / sustained digital 的历史设计背景，可额外参考：
  - `docs/gameplay_input_ownership_investigation_and_plan_zh.md`
  - `docs/gameplay_sustained_digital_and_cursor_handoff_plan_zh.md`

## 涉及代码与文档

- 新增或修改目标文件：
  - `src/input_v2/projections/GameplayProjection.h`
  - `src/input_v2/projections/GameplayProjection.cpp`
  - `src/input_v2/compat/LegacyInputContextCompat.h`
  - `src/input_v2/adapters/PollOutputAdapter.h`
  - `src/input_v2/adapters/PollOutputAdapter.cpp`
  - `src/input_v2/runtime/DualPadRuntime.h`
  - `src/input_v2/runtime/DualPadRuntime.cpp`
- 兼容层与现有执行面：
  - `src/input/backend/NativeButtonCommitBackend.h`
  - `src/input/backend/NativeButtonCommitBackend.cpp`
  - `src/input/backend/KeyboardHelperBackend.h`
  - `src/input/backend/KeyboardHelperBackend.cpp`
  - `src/input/AuthoritativePollState.h`
  - `src/input/AuthoritativePollState.cpp`
- 必须收缩或删除的 legacy 文件：
  - `src/input/injection/GameplayOwnershipCoordinator.h`
  - `src/input/injection/GameplayOwnershipCoordinator.cpp`
  - `src/input/injection/PadEventSnapshotProcessor.h`
  - `src/input/injection/PadEventSnapshotProcessor.cpp`
  - `src/input/InputContext.h`
  - `src/input/InputContext.cpp`
  - `src/input/InputContextNames.h`
  - `src/input/InputContextNames.cpp`
  - `src/input/InputModalityTracker.cpp`
- 必须同步阅读但不在本 slice 里扩 scope 的文档：
  - `src/ARCHITECTURE.md`
  - `docs/plans/dualpad_rearchitecture/04_slice_phase3_presentation_split_zh.md`
- 如需历史背景，再回看 `docs/plans/dualpad_rearchitecture/dualpad_rearchitecture_plan_zh.md`。

## 实施步骤

### 1. 先落 `GameplayProjection` 的类型与纯接口

- 在 `src/input_v2/projections/GameplayProjection.h` 中直接定义本页冻结的所有 enum / struct，
  包括 `GameplayPresentationPlan`。
- 对外只暴露两个入口：
  - `GameplayProjectionFrame ResolveGameplayProjection(...)`
  - `void ExecuteGameplayProjection(const GameplayProjectionFrame& frame, ...)`
- `ResolveGameplayProjection(...)` 所在编译单元禁止 include 任何 legacy backend 头文件。
- `ExecuteGameplayProjection(...)` 可以放在同文件，也可以下沉到 `src/input_v2/adapters/PollOutputAdapter.*`，但不得把决策逻辑回流到 adapter。

### 2. 固定 action 到 output plan 的归类规则

- 从 `ResolvedActionFrame` 生成 output candidates 时，按以下顺序归类：
  1. `backend = NativeButtonCommit` 且 `digital class = transient` -> `gamepadPlan.transientDigital`
  2. `backend = NativeButtonCommit` 且 `digital class = sustained` -> `gamepadPlan.sustainedDigital`
  3. `backend = NativeState` 且目标是 look / move / trigger -> `gamepadPlan.analog`
  4. `backend = KeyboardHelper` 或 `backend = ModEvent` -> `helperPlan.commands`
- `Plugin` 动作不进入 `GameplayProjectionFrame`，继续留在独立 executor；本 slice 不把 plugin action 混进 gameplay output plan。
- `Sprint` 的冻结映射：
  - action domain 仍是 gameplay
  - output 仍是 native gamepad digital
  - 但归类到 `sustainedDigital`
  - `activeSourceMask` 的来源固定为：
    - `GamepadResolved`
    - `KeyboardPhysical`
    - `MousePhysical`

### 3. 固定 owner 解析规则

- `lookOwner`：
  - `mouseLookActive` 优先切到 `KeyboardMouse`
  - 否则若右摇杆达到 `GameplayPolicy.lookEnterThreshold`，切到 `Gamepad`
  - 若当前 owner 已是 `Gamepad`，沿用 `GameplayPolicy.lookSustainThreshold`
  - 无新证据时保留上一帧 owner
- `moveOwner`：
  - `IsKeyboardMoveActive()` 优先切到 `KeyboardMouse`
  - 否则若左摇杆达到 `GameplayPolicy.moveEnterThreshold`，切到 `Gamepad`
  - sustain 逻辑与 `lookOwner` 同型
- `combatOwner`：
  - `IsKeyboardMouseCombatActive()` 优先切到 `KeyboardMouse`
  - 否则若扳机达到 `GameplayPolicy.triggerEnterThreshold`，切到 `Gamepad`
  - `combatOwner` 同时治理 `LT` 与 `RT`
- `digitalOwner`：
  - 只看 transient gameplay digital
  - `IsKeyboardMouseDigitalActive()` 优先切到 `KeyboardMouse`
  - 否则若本帧存在 transient gamepad digital candidate，切到 `Gamepad`
  - sustained digital 不参与这里的 owner 竞争
- 非 gameplay context：
  - 四个 owner 都强制为 `KeyboardMouse`
  - `GatePlan` 强制关闭 gameplay gamepad 输出
  - `RecoveryPlan` 负责是否 cancel 当前 transient / clear sticky state

### 4. 固定 gate 生成规则

- `lookOwner = KeyboardMouse` -> `GatePlan.lookGate = ZeroedByKeyboardMouse`
- `moveOwner = KeyboardMouse` -> `GatePlan.moveGate = ZeroedByKeyboardMouse`
- `combatOwner = KeyboardMouse` -> 左右扳机 gate 都设为 `ZeroedByKeyboardMouse`
- `digitalOwner = KeyboardMouse` 时：
  - 若上一帧 `digitalOwner = Gamepad` -> `CancelAndSuppressNewTransient`
  - 否则 -> `SuppressNewTransient`
- `digitalOwner = Gamepad` 时：
  - `transientDigitalGate = Open`
- gate 只约束 `gamepadPlan.transientDigital`，不约束 `gamepadPlan.sustainedDigital` 和 `helperPlan.commands`

### 5. 固定 `GamepadOutputPlan` 的生成方式

- `analog`：
  - 先按 action graph 解析出目标轴值
  - 再由 `GatePlan` 统一 zero-out
  - `GameplayProjection` 输出的 `analog` 已经是最终可发布值，executor 不再二次判断 owner
- `transientDigital`：
  - 只保留经过 gate 后允许下发的 transient native digital 命令
  - 被 gate 拦掉的命令不进入计划列表
- `sustainedDigital`：
  - 每个 action 一条命令，不按 press / hold / release 拆成多条
  - executor 根据 `activeSourceMask` 维护真实 held 状态
  - `activeSourceMask != 0` 即表示本帧想继续 held

### 6. 固定 `KeyboardHelperOutputPlan` 的生成方式

- helper 命令直接从 `ResolvedActionFrame` 映射到 `HelperOutputCommand`
- `helperCode` 必须来自 Phase 4 的 compiled output descriptor，禁止 runtime 反查字符串配置
- `ModEvent` 在这里已经转成稳定 helper output code，不再在 executor 里做“临时翻译 actionId”
- 只有 `RecoveryPlan.mode != None` 时，才允许 `enqueueBridgeResetBeforeApply = true`

### 7. 固定 `RecoveryPlan` 的生成方式

- 输入来源固定是 runtime shell 提供的 `GameplayRecoveryInput`
- 映射规则固定为：
  - 正常帧 -> `RecoveryMode::None`
  - sequence gap / soft resync -> `RecoveryMode::SoftResyncOutputs`
  - explicit reset / overflow / hard resync -> `RecoveryMode::HardResetOutputs`
- `SoftResyncOutputs`：
  - `resetNativeCommitBackend = true`
  - `resetKeyboardHelperBackend = true`
  - `resetSustainedDigitalAggregator = true`
  - `clearProjectionStickyOwners = true`
  - `clearRecoveryBaseline = false`
- `HardResetOutputs`：
  - 上述 4 项全部为 `true`
  - `clearRecoveryBaseline = true`
- 仅当本帧是 clean frame 且执行成功，才把 `commitCleanRecoveryBaselineAfterApply` 置为 `true`

### 8. 固定 executor 的 side effect 顺序

- `PollOutputAdapter` 的执行顺序必须固定为：
  1. 先执行 `RecoveryPlan`
  2. 再把 `GatePlan` 下发到 native output executor
  3. 再应用 `gamepadPlan.sustainedDigital`
  4. 再应用 `gamepadPlan.transientDigital`
  5. 再应用 `helperPlan.commands`
  6. 最后把 `gamepadPlan.analog` 发布到 `AuthoritativePollState`
- 不允许在步骤 3 到步骤 6 之间回头再问 owner / gate。
- `NativeButtonCommitBackend` 在这一阶段保留，但角色只剩 executor：
  - 消费 transient command
  - 消费 sustained contributor mask
  - 消费 gate mode
  - 不再持有 gameplay owner 决策

### 9. 固定 runtime shell 的改造方式

- 正式挂点固定在 `src/input_v2/runtime/DualPadRuntime.*`
- 如果当下 cutover 仍经过 `src/input/injection/PadEventSnapshotProcessor.*`，该文件只允许做一层
  `LegacyPadSnapshotRuntimeAdapter`：
  - 把 legacy snapshot / frame / recovery facts 整理成 Phase 5 所需输入
  - 立即委托给 `DualPadRuntime`
  - 禁止继续在 legacy processor 里保留 gameplay owner / gate / analog suppression 判断
- `PadEventSnapshotProcessor::FinishFramePlanning(...)` 现有这段逻辑必须整块删除：
  - `UpdateDigitalOwnership(...)`
  - `SetGameplayDigitalGatePlan(...)`
  - `ForceCancelGateAwareGameplayTransientActions()`
  - `ApplyOwnership(...)`

### 10. 按固定顺序切除旧 coordinator

1. 先落 `GameplayPresentationPublisher`，让 `GameplayProjectionFrame.presentationPlan`
   能稳定发布 `PublishedGameplayPresentation`。
2. 再把 Phase 3 的 `GameplayPresentationAdapter` producer 切到新 seam，并保留
   `CoordinatorGameplayPresentationBridge` 做只读 parity。
   - 切换动作固定发生在 `DualPadRuntime::PublishGameplayPresentation(...)` 已稳定发布
     新 seam 之后；禁止让 Phase 3 adapter 直接挂到任意临时 publisher
3. parity 通过后，删除 coordinator presentation API 与 lease 字段。
4. 再把 runtime shell 中所有 `GameplayOwnershipCoordinator` 决策调用删掉，改接
   `GameplayProjectionFrame`。
5. 再把 probe / log 读口切到 `GameplayProjectionFrame` 与
   `PublishedGameplayPresentation` 的 debug snapshot。
6. 最后删 `GameplayOwnershipCoordinator.h/.cpp`，并清理所有 include、前向声明和文档引用。

## Shadow Compare Exit Gate

Phase 5 的 seam-swap 现在必须满足下面这组正式退出门槛，才允许把
`GameplayPresentationAdapter` producer 从 `CoordinatorGameplayPresentationBridge`
切到 `GameplayPresentationPublisher`，并继续删除 coordinator presentation API：

1. 固定比对对象：
   - 新路径：`DualPadRuntime::PublishGameplayPresentation(...) -> PublishedGameplayPresentation`
   - 旧路径：命名明确的 `CoordinatorGameplayPresentationBridge`
2. 必跑场景固定如下：
   - `engineOwner` 在 `KeyboardMouse <-> Gamepad` 之间来回切换
   - `menuEntryOwner` 在 gameplay clean baseline 后保持稳定，再经历 menu 往返
   - `reason` 从普通 gameplay 变到 `RecoveryRepublish`
   - `RecoveryPlan.mode == HardResetOutputs` 后发布 clean gameplay baseline
   - gameplay -> menu -> gameplay，确认 Phase 3 adapter 不再回读 coordinator API
   - resync / overflow 之后的第一次 clean gameplay frame
3. parity 必比字段固定如下：
   - `engineOwner`
   - `menuEntryOwner`
   - `gameplayPresentationRevision`
   - `reason`
   - `publishedTick` 的单调性与“晚于 `PollOutputAdapter` 成功执行本帧 output plan”这一顺序约束
4. 唯一允许的 intentional diff 只有两类：
   - 新路径新增 explain / telemetry 字段，但 `PublishedGameplayPresentation` 的正式字段值完全一致
   - 新路径 `publishedTick` 的日志粒度更细，但仍满足“同一 runtime tick 内只发布一份正式 seam”
5. 明确不允许的 diff：
   - `engineOwner` 不一致
   - `menuEntryOwner` 不一致
   - `gameplayPresentationRevision` 推进时机不一致
   - `HardResetOutputs` 后是否 republish clean gameplay baseline 的结论不一致
   - Phase 3 adapter 切源后仍回读任何 coordinator presentation API
6. `CoordinatorGameplayPresentationBridge` 的最长存活窗口固定如下：
   - 允许存在于 `Consumer Swap` 前后的同一轮 parity / live smoke 验证窗口内
   - 一旦切换到 `GameplayPresentationPublisher` 并通过本节 gate，必须在同一 PR
     或紧随其后的 cleanup PR 中降级为 test-only / debug-only
   - 不允许跨出 Phase 5 继续留在 live runtime authoritative 决策链
7. bridge 的最晚删除点固定为：
   - 不晚于 `Coordinator Deletion` checkpoint 完成后的 cleanup PR
   - 进入 Phase 6 前，仓库里不得再保留 live runtime 可触达的
     `CoordinatorGameplayPresentationBridge`

## 验证与观测

- 构建验证：
  - `xmake build DualPad`
- replay / golden trace：
  - 复用 Phase 0 的 replay harness，比对：
    - `GameplayProjectionFrame`
    - `PublishedGameplayPresentation`
    - `AuthoritativePollState`
    - `KeyboardHelperOutputPlan`
    - recovery 后的 clean baseline 提交结果
- 必测场景：
  - 鼠标 look 与右摇杆 look 来回抢占，只允许一个 look 通道写入
  - 键盘移动与左摇杆移动来回抢占，只允许一个 move 通道写入
  - 键鼠战斗输入与 `LT / RT` 来回抢占，只允许一个 combat 通道写入
  - `Sprint`：
    - 手柄先按住，再按键盘，不丢 held
    - 键盘先按住，再按手柄，松键盘后不丢 held
    - mouse button 映射到 sprint 时也不丢 held
  - `Jump / Activate`：
    - `digitalOwner = KeyboardMouse` 时不再新起 transient gamepad digital
    - 从 `Gamepad -> KeyboardMouse` 的切换帧能 cancel 旧 transient
  - 非 gameplay context：
    - menu / console 进入时 analog 归零
    - gameplay transient 被 cancel，不残留
  - resync / hard reset：
    - native commit backend 被 reset
    - keyboard helper 被 reset
    - sustained aggregator 被 reset
    - sticky owner 被 clear
  - `PublishedGameplayPresentation`：
    - `engineOwner` 变化时 revision 递增一次
    - `menuEntryOwner` 只来自最近一次 clean gameplay frame
    - Phase 3 adapter 切源后不再回读 coordinator API
  - overflow：
    - transientDigital 超限时不允许部分下发
    - sustainedDigital 超限时强制 hard reset
    - helper commands 超限时强制 bridge reset
- 观测日志必须新增并保持稳定：
  - `GameplayProjection` 每次 owner 变化打一条 log
  - `GatePlan` 变化打一条 log
  - `Sprint` 的 sustained source mask 变化打一条 log
  - `RecoveryPlan` 执行打一条 log
  - `GameplayPresentationPublisher` 每次 revision 递增打一条 log
  - `GameplayProjectionOverflow` 每次 overflow 打一条 log

## 退出条件

- `GameplayProjectionFrame` 及其四个子计划已按本页结构落地，且未出现额外临时字段。
- `PublishedGameplayPresentation` 已由 `GameplayPresentationPublisher` 正式发布，且
  Phase 3 的 `GameplayPresentationAdapter` 已改读这条 seam。
- `Shadow Compare Exit Gate` 已通过，且 `CoordinatorGameplayPresentationBridge`
  已降级为 test-only / debug-only，或已在 cleanup PR 中删除。
- runtime shell 中不存在任何直接调用 `GameplayOwnershipCoordinator` 的 gameplay 决策逻辑。
- `GameplayOwnershipCoordinator.*` 已删除。
- `Sprint` 已从 `DigitalOwner` family gate 中移出，改由 sustained aggregation 驱动。
- `NativeButtonCommitBackend` 与 `KeyboardHelperBackend` 只执行计划，不再二次推断 gameplay owner。
- 任一 `FixedCommandList` overflow 都会按本文冻结的 fail-closed 规则进入 `HardResetOutputs`，
  不存在静默截断或部分执行。
- replay / golden trace 能稳定比较 `GameplayProjectionFrame`，并覆盖上面的必测场景。

### Rollback Boundary / Emergency Revert

Phase 5 的 cutover 至少拆成下面 3 个可回退边界，禁止把 seam 切换、coordinator 删除、
runtime shell 改造绑成一个不可逆提交：

1. `Seam Publish`
   - `GameplayProjectionFrame.presentationPlan` 与 `GameplayPresentationPublisher` 已落地
   - `CoordinatorGameplayPresentationBridge` 仍可只读保留
   - Phase 3 adapter 还没切源
2. `Consumer Swap`
   - Phase 3 的 `GameplayPresentationAdapter` 已改读新 seam
   - coordinator presentation API 仍可只读保留，便于紧急回退
3. `Coordinator Deletion`
   - 只有在 replay / live parity 通过后，才允许删 API、删 lease、删文件

紧急回退顺序固定为：

1. 先把 Phase 3 adapter producer 切回 `CoordinatorGameplayPresentationBridge`
2. 再回退 `GameplayPresentationPublisher`
3. 最后才允许回退 runtime shell 对 `GameplayProjectionFrame` 的消费

回退时允许暂留的对象只有：

- `CoordinatorGameplayPresentationBridge`
- `GameplayPresentationPublisher`
- `LegacyPadSnapshotRuntimeAdapter`

回退时明确禁止：

- 把 coordinator 的 owner / gate 决策逻辑塞回 backend
- 让 `InputModalityTracker` 重新充当 gameplay presentation authority
- 只回退代码不回退 `PublishedGameplayPresentation` / replay / golden 基线

## 交接给下一 slice 的合同

- 交给 `docs/plans/dualpad_rearchitecture/07_slice_phase6_prompt_projection_zh.md` 的固定事实：
  - gameplay owner / gate / recovery 已经从 prompt / glyph / presentation 中彻底解耦
  - `GameplayProjectionFrame` 是稳定产物，Phase 6 不得向其中塞 prompt 字段
  - `PublishedGameplayPresentation` 是给 Phase 3 的唯一 gameplay presentation seam，
    Phase 6 不得绕过它去读 coordinator 或 `GameplayProjection` 内部状态
  - `ActionId`、`ResolvedActionFrame`、compiled output descriptor 已可被 prompt projection 直接消费，无需再看 gameplay coordinator
- 交给后续 `Phase 7` 的固定事实：
  - `RecoveryPlan` 结构与消费顺序已经冻结
  - `Phase 7` 只能改变 `GameplayRecoveryInput` 的来源与判定，不能改 `GameplayProjectionFrame`、`GatePlan`、`RecoveryPlan` 语义
