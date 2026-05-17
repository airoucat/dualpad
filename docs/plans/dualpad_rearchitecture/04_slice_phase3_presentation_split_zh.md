# Phase 3：拆分 `InputModalityTracker`，收口 `PresentationProjection`

本 slice 对应重构阶段里的 Phase 3，执行上直接承接：

- [docs/plans/dualpad_rearchitecture/03_slice_phase2_menu_instance_truth_zh.md](03_slice_phase2_menu_instance_truth_zh.md)

本文件的目的不是复述“为什么要拆”，而是冻结这一阶段必须落地的实现合同，让开发可以按顺序推进，不在 `work` 阶段临场决定：

- `SourceEvidenceCollector` 到底收什么，不收什么
- `PresentationProjection` 到底算什么，不算什么
- `SkyrimCompatibilitySurface` 到底读什么，不读什么
- 三个 hook 和 menu refresh 应按什么顺序切

## 目标

本 slice 完成后，仓库里与 UI / platform / cursor 相关的运行时真相必须改成：

```text
raw RE input / synthetic suppression / published gameplay presentation / published device family evidence / resolved ui context
  -> SourceEvidenceCollector
  -> PresentationProjection
  -> PublishedPresentationState
  -> SkyrimCompatibilitySurface
  -> IsUsingGamepad / GamepadControlsCursor / BSPCGamepadDeviceHandler::IsEnabled / menu refresh
```

必须同时达成下面 6 条：

1. `InputModalityTracker` 不再同时承担“采证 + 决策 + hook 兼容 + refresh 调度”四件事。
2. `PublishedPresentationState` 成为 UI / platform-facing 的单一已发布真相源。
3. Phase 2 已收口的 gameplay presentation 继续作为上游事实来源，但进入 UI 链前必须先收口成
   `PublishedGameplayPresentation` seam。
4. 菜单 open / close 的平台继承必须对称，不能再有一个 `OnAuthoritativeMenuOpen(...)` 提前继承、另一个 `ReconcileContextState()` 事后补救的双路径。
5. menu refresh 不再由 setter 副作用触发，而只由 `PublishedPresentationState.epoch + dirty` 驱动。
6. Phase 3 不引入 `PromptProjection`、`Action Graph`、cursor position synchronization 或新的 gameplay owner 规则。

本 slice 的非目标也一并冻结：

- 不修改 `GameplayOwnershipCoordinator` 的 owner 规则。
- 不修改 `ScaleformGlyphBridge` 的 glyph 解析路径。
- 不把 `CursorOwner` 扩大成 cursor position handoff 方案。
- 不顺手把 `InputModalityTracker` 彻底删文件；本 slice 允许它退化成过渡 façade。

## 冻结的设计决定

### 1. 组件职责边界

| 组件 | 允许负责 | 明确禁止 |
| --- | --- | --- |
| `SourceEvidenceCollector` | 收集原始来源证据、窗口状态、lease、synthetic suppression、context boundary 后的窗口重置 | 直接回答 hook、直接决定 `PresentationOwner`、直接触发 menu refresh、直接读取 glyph / gameplay backend |
| `PresentationProjection` | 基于上游事实生成 `PublishedPresentationState`，维护 `epoch`、`dirty`、`reason`，处理 `Gameplay -> Menu` 与 `Menu -> Gameplay` 的投影切换 | 读取 RE 原始事件链、自己维护 synthetic suppression、直接调 UI task、直接读 `ScaleformGlyphBridge` 或 backend |
| `SkyrimCompatibilitySurface` | 只把 `PublishedPresentationState` 适配给 `IsUsingGamepad`、`GamepadControlsCursor`、`BSPCGamepadDeviceHandler::IsEnabled`、menu refresh | 自己再做 owner 推断、自己再读 `GameplayOwnershipCoordinator`、自己维护 lease / sticky / policy |

### 2. `SourceEvidenceCollector` 的职责与状态源

`SourceEvidenceCollector` 是“采证器”，不是“仲裁器”。它只负责把当前散落在 `InputModalityTracker` 里的原始证据和窗口状态收成一份稳定快照。

固定输入源如下：

- `RE::InputEvent*`
  - keyboard button / char
  - mouse button / wheel / move
  - gamepad button / thumbstick
- `KeyboardHelperBackend` 的 synthetic keyboard suppression
- `PublishedDeviceFamilyEvidence`
  - 由命名固定的 `DeviceFamilyIngressPublisher` 发布
  - `SourceEvidenceCollector` 只允许读取最近一次已发布值，不允许自己推断或自增 revision
- Phase 2 菜单真相层发出的 context boundary
  - 至少要有 `uiContext`、`contextRevision`、`actionSetStack`、`presentationPolicyId`
  - 若 `03_slice_phase2_menu_instance_truth_zh.md` 产出的还不是最终 `ContextResolver`，此处只允许接命名固定的 `Phase2ResolvedContextFeedAdapter`，且它只允许逐字段搬运 Phase 2 已发布的 `uiContext`、`contextRevision`、`actionSetStack`、`presentationPolicyId`、`topMenuInstanceId`、`identityQuality`、`menuStackRevision`，不允许生成第二套 context 真相，也不允许回头查 `ContextCatalog`
- monotonic time
- authoritative menu-open 提前信号
  - 只作为 boundary 提示，不再自己做 presentation 继承

`SourceEvidenceCollector` 必须维护但不得越权解释的状态：

- `keyboardEvidence`
- `mouseButtonEvidence`
- `mouseMoveEvidence`
- `gamepadEvidence`
- `pointerSignal`
  - `None`
  - `HoverOnly`
  - `PointerActive`
- `syntheticKeyboardWindow`
- `gamepadLease`
- `lastObservedContextRevision`
- `lastObservedDeviceFamilyRevision`

本 slice 固定要求：

1. `SourceEvidenceCollector` 不持有 `PresentationOwner / CursorOwner / NavigationOwner`。
2. gameplay presentation 不进入它的内部状态；`engineOwner / menuEntryOwner` 只作为 `PresentationProjection` 的独立输入。
3. 所有 mouse move threshold / sticky / suppression 仍可沿用当前行为，但必须转成“证据字段 + 配置参数”，不能继续以 setter 副作用形式存在。

建议新增文件：

- `src/input_v2/kernel/SourceEvidenceCollector.h`
- `src/input_v2/kernel/SourceEvidenceCollector.cpp`
- `src/input_v2/kernel/DeviceFamilyIngressPublisher.h`
- `src/input_v2/kernel/DeviceFamilyIngressPublisher.cpp`

### 2A. 稳定的 `PublishedDeviceFamilyEvidence` ingress / source evidence seam

从本 slice 起，`deviceFamilyRevision` 的 authoritative producer 固定是 source evidence 链上的
`PublishedDeviceFamilyEvidence`，不属于 `PresentationProjection`、`SkyrimCompatibilitySurface`、
`PromptProjection` 或任何 UI-facing hook 兼容层。

```cpp
enum class DeviceFamilyEvidenceSource : std::uint8_t
{
    None,
    RawInputIngress,
    PollBridge,
    ExplicitResync
};

struct PublishedDeviceFamilyEvidence
{
    DeviceFamily family{ DeviceFamily::KeyboardMouse };
    std::uint32_t deviceFamilyRevision{ 0 };
    DeviceFamilyEvidenceSource source{ DeviceFamilyEvidenceSource::None };
    std::uint64_t publishedTick{ 0 };
};
```

固定 owner、发布时间点与消费顺序如下：

1. `DeviceFamilyIngressPublisher` 是唯一允许递增 `deviceFamilyRevision` 的对象。
2. `DeviceFamilyIngressPublisher` 只能运行在 raw ingress / source evidence 侧；不允许放进
   `PresentationProjection`、`SkyrimCompatibilitySurface` 或 `InputModalityTracker` façade 内部。
3. 每个主线程 input tick 的顺序固定为：
   - 先由 `DeviceFamilyIngressPublisher` 消费最新 ingress / poll bridge 事实并发布一份
     `PublishedDeviceFamilyEvidence`
   - 再由 `SourceEvidenceCollector` 读取最近一次已发布值并拷入 `SourceEvidenceSnapshot`
   - 最后才允许 `PresentationProjection` 消费 `SourceEvidenceSnapshot`
4. `deviceFamilyRevision` 只在下面三种情况下递增：
   - `family` 从一种家族切到另一种家族
   - `source` 从无效恢复为有效，或从一条稳定来源切到另一条稳定来源
   - runtime 明确发出 `ExplicitResync`
5. 菜单开关、`epoch` 抖动、owner 切换、cursor 变化都不得递增 `deviceFamilyRevision`。
6. `PresentationProjection` 允许把 `family` 复制到 `PublishedPresentationState.family`，
   也允许把 `deviceFamilyRevision` 复制到 published state 供日志和下游读取；但后续任何 slice
   都禁止从 `PublishedPresentationState.family` 逆推 revision。

### 3. `PresentationProjection` 的职责边界

`PresentationProjection` 是此 slice 的唯一决策层。它读取上游证据，发布 UI-facing 的折叠状态。

固定输入：

1. `SourceEvidenceSnapshot`
   - 必须已经包含 `PublishedDeviceFamilyEvidence`
2. Phase 2 的 resolved UI context 输出
   - 至少包含 `uiContext`
   - `contextRevision`
   - `actionSetStack`
   - `presentationPolicyId`
   - `presentationPolicyId` 的 authoritative owner 仍然是 Phase 2 `ContextResolver`；Phase 3 只允许消费或转发，不允许从 `uiContext`、`actionSetStack` 或 `ContextCatalog` 反查第二份值
3. gameplay presentation 适配输入
   - 当前阶段固定为 `PublishedGameplayPresentation`
4. 上一份 `PublishedPresentationState`

固定输出：

```cpp
struct PublishedPresentationState {
    DeviceFamily family;
    std::uint32_t deviceFamilyRevision;
    PresentationOwner owner;
    NavigationOwner navigationOwner;
    CursorOwner cursorOwner;
    PointerIntent pointerIntent;
    UiContextId uiContext;
    ActionSetStack actionSets;
    std::uint32_t epoch;
    PresentationDirtyFlags dirty;
    PresentationDecisionReason reason;
};
```

本 slice 里 `PresentationProjection` 必须遵守下面 11 条：

1. gameplay 下的 `owner` 只能来自 `PublishedGameplayPresentation.engineOwner`，不能重新拼
   `LookOwner + mouse-look active`。
2. `Gameplay -> Menu` 时菜单入口继承只能来自
   `PublishedGameplayPresentation.menuEntryOwner`，不能回退到 `_gameplayOwner`、
   `_gameplayMenuEntrySeed` 或任何 tracker 自己的 seed。
3. `Menu -> Gameplay` 时必须重新消费最近一次已发布的 `PublishedGameplayPresentation`，
   而不是保留菜单内最后一次 mouse / gamepad 抢占结果。
4. `family` 与 `deviceFamilyRevision` 只能来自 `SourceEvidenceSnapshot.deviceFamilyEvidence`；
   不允许在 projection 内根据 `owner`、`uiContext`、`PublishedPresentationState.family`
   或 hook 结果逆推。
5. `presentationPolicyId` 只能来自 Phase 2 已发布的 resolved context；`PresentationProjection`
   与 `Phase2ResolvedContextFeedAdapter` 都不得自行从 `UiContextId`、`ActionSetStack`、
   `ContextCatalog` 或任何本地 policy 表反查它。
6. `NavigationOwner`、`CursorOwner`、`PointerIntent` 都必须作为投影结果发布，不能再由 hook 层临时判断。
7. `dirty` 只在对外可见字段变化时置位；内部证据刷新但未改变外部可见结果时，不允许无意义地抖动 `epoch`。
8. `epoch` 只在 `dirty != None` 时递增。
9. `PresentationProjection` 可以保留 shadow compare 日志，但不能把 legacy tracker 当最终 source。
10. `PresentationProjection` 不直接触发 `RefreshMenus()`；它只发布 `dirty + epoch`。
11. `PresentationProjection` 禁止直接读取 `GameplayOwnershipCoordinator`、
    `DeviceFamilyIngressPublisher`、`ContextManager` 或任何 hook 返回值。

建议新增文件：

- `src/input_v2/projections/PresentationProjection.h`
- `src/input_v2/projections/PresentationProjection.cpp`

### 4. `SkyrimCompatibilitySurface` 的职责边界

`SkyrimCompatibilitySurface` 是唯一允许碰引擎 hook 的层。它是 adapter，不是 policy owner。

固定输入：

- 最近一次已提交的 `PublishedPresentationState`

固定输出行为：

1. `IsUsingGamepadHook()`
   - 只读 `PublishedPresentationState.owner`
2. `GamepadControlsCursorHook()`
   - 只读 `PublishedPresentationState.cursorOwner`
3. `BSPCGamepadDeviceHandler::IsEnabled()`
   - 非 remap mode 一律按当前逻辑直接放行
   - 仅 remap mode 下读取 `PublishedPresentationState.owner`
4. menu refresh
   - 只响应 `PublishedPresentationState.epoch`
   - 同一 `epoch` 最多入队一次 UI task

明确禁止：

- 在 remap mode 下重新调用 legacy `ResolveIsUsingGamepad()`
- 直接读 `ContextManager`
- 直接读 `GameplayOwnershipCoordinator`
- 因为 cursor owner 变化就立刻重新推导 presentation owner

建议新增文件：

- `src/input_v2/adapters/SkyrimCompatibilitySurface.h`
- `src/input_v2/adapters/SkyrimCompatibilitySurface.cpp`

### 5. 当前阶段固定保留的 gameplay adapter 关系

Phase 3 不重新设计 gameplay owner 决策；但从这一刻起，`PresentationProjection` 不再把
`GameplayOwnershipCoordinator::GameplayPresentationState` 当直接输入类型，而是只接受命名固定的
`PublishedGameplayPresentation` seam。

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

当前阶段唯一允许的 producer chain 固定为：

```text
GameplayOwnershipCoordinator::GameplayPresentationState
  -> GameplayPresentationAdapter
  -> PublishedGameplayPresentation
  -> PresentationProjection
```

要求：

1. `GameplayPresentationAdapter` 是 Phase 3 唯一允许存在的 gameplay presentation bridge。
2. `GameplayPresentationAdapter` 只允许读取 coordinator 已发布的 presentation 结果，
   不允许回读 `LookOwner / MoveOwner / CombatOwner / DigitalOwner`、lease 内部状态，
   也不允许自己重新做 owner 解释。
3. `gameplayPresentationRevision` 在 Phase 3 由 `GameplayPresentationAdapter` 持有并递增；
   只在 `engineOwner`、`menuEntryOwner`、`reason` 变化，或 runtime 明确要求重新发布 clean gameplay
   baseline 时递增，不得按帧递增。
4. 主线程消费顺序固定为：
   - 上游 producer 刷新 coordinator published state
   - `GameplayPresentationAdapter` 发布一份 `PublishedGameplayPresentation`
   - `PresentationProjection` 在拿到 `SourceEvidenceSnapshot` 与 Phase 2 resolved context 之后消费它
5. 到 Phase 5 时，只允许替换 `GameplayPresentationAdapter` 的实现来源，不允许改
   `PresentationProjection` 的消费接口或字段名。
6. `SourceEvidenceCollector` 不参与 gameplay presentation 的生成、递增或回滚。

### 5A. `deviceFamilyRevision` 与 gameplay presentation 的交接口径

Phase 3 向后续 slice 固定交接两条独立 published seam：

- `PublishedDeviceFamilyEvidence`
- `PublishedGameplayPresentation`

后续 slice 若需要 boundary key、prompt scope 或 runtime explain 字段，必须直接消费这两条 seam
或它们在 `SourceEvidenceSnapshot` / `PublishedPresentationState` 中的镜像字段，不允许：

- 从 `PublishedPresentationState.family` 逆推 `deviceFamilyRevision`
- 从 `GameplayProjectionFrame`、`InputModalityTracker`、`ContextManager` 或 hook 结果重新拼
  `engineOwner / menuEntryOwner`

### 6. hook 与 refresh 的切换顺序

本 slice 的切换顺序现在冻结，开发时不再改顺序：

1. **先引入新类型与 shadow publish，不切 hook。**
   - `SourceEvidenceCollector` 和 `PresentationProjection` 先在后台生成 `PublishedPresentationState`
   - `GameplayPresentationAdapter` 与 `DeviceFamilyIngressPublisher` 先发布稳定 seam
   - legacy `InputModalityTracker` 继续回答 hook
   - 新旧结果并行打 diff log
2. **先切 menu refresh，再切 hook。**
   - `SkyrimCompatibilitySurface` 先接管 `epoch + dirty -> RefreshMenus()`
   - 此时 `IsUsingGamepad / GamepadControlsCursor / IsEnabled` 仍可走 legacy tracker
   - 目标是先验证发布时间，而不是先验证 hook 结果
3. **第一条切 `IsUsingGamepadHook()`。**
   - 这是 UI / platform 最广泛的消费面
   - 切完后优先验证 gameplay HUD、Pause、Journal、Map
4. **第二条切 `GamepadControlsCursorHook()`。**
   - cursor 是独立链，比 platform 更容易出现“状态对了、位置没跟上”的假阳性
   - 因此必须在 `IsUsingGamepad` 稳定后再切
5. **最后切 `BSPCGamepadDeviceHandler::IsEnabled()`。**
   - 它只在 remap mode 下依赖 presentation
   - 风险最低，应该最后切，避免它掩盖更早的切换问题
6. **最后一轮再删 legacy tracker 的决策代码。**
   - 在三个 hook 都切完且 parity 通过之前，不删旧逻辑
   - parity 通过后，`InputModalityTracker` 只保留转发入口或完全空壳

禁止事项：

- 不允许同一个提交同时切三个 hook。
- 不允许在 hook 尚未切完时就删除 shadow compare 日志。
- 不允许把 refresh 继续留在 `SetPresentationOwner()` / `SetCursorOwner()` 这类 setter 副作用里。

## 前置依赖

进入本 slice 前，下面依赖必须已满足：

1. Phase 0 已能提供最少一组 replay / 对比资产。
   - 至少覆盖：
     - `gameplay -> pause`
     - `gameplay -> journal`
     - `gameplay -> map`
     - menu 内 keyboard takeover 后 gamepad reclaim
2. Phase 2 已提供稳定的 UI context 输出合同。
   - 不要求名字一定叫 `ContextResolver`
   - 但必须有：
     - `uiContext`
     - `contextRevision`
     - `actionSetStack`
     - `presentationPolicyId`
   - 上述字段必须来自 Phase 2 已发布 seam；本 slice 不允许为了补齐 `presentationPolicyId` 回头查 `ContextCatalog`
3. 当前主线的
   `GameplayOwnershipCoordinator::GameplayPresentationState -> GameplayPresentationAdapter -> PublishedGameplayPresentation`
   生产链已保持有效。
4. `InputModalityTracker` 当前 Phase 2 行为已经稳定。
   - 即：
     - `_engineGameplayPresentationLatch`
     - `_gameplayMenuEntrySeed`
     - `OnAuthoritativeMenuOpen(...)` 的提前同步
5. 本 slice 不等待 `PromptProjection` 或 `Action Graph`。

如果以上任一依赖未落地，不要在本 slice 内临时补设计；应先回到对应前置 slice 收口。

## 涉及代码与文档

### 新增文件

- `src/input_v2/kernel/SourceEvidenceCollector.h`
- `src/input_v2/kernel/SourceEvidenceCollector.cpp`
- `src/input_v2/projections/PresentationProjection.h`
- `src/input_v2/projections/PresentationProjection.cpp`
- `src/input_v2/adapters/SkyrimCompatibilitySurface.h`
- `src/input_v2/adapters/SkyrimCompatibilitySurface.cpp`
- `src/input_v2/adapters/GameplayPresentationAdapter.h`
- `src/input_v2/adapters/GameplayPresentationAdapter.cpp`

### 需要修改的现有代码

- `src/input/InputModalityTracker.h`
- `src/input/InputModalityTracker.cpp`
- `src/input/ContextEventSink.cpp`
- `src/input/backend/KeyboardHelperBackend.cpp`
- `src/main.cpp`

### 只允许轻量适配，不允许改规则的现有代码

- `src/input/injection/GameplayOwnershipCoordinator.h`
- `src/input/injection/GameplayOwnershipCoordinator.cpp`

这里最多允许做：

- 暴露稳定的 adapter 读取接口
- 暴露 `GameplayPresentationAdapter` 所需的只读 published state 读取接口
- 增加 revision / explain 日志字段

不允许做：

- 重写 owner 规则
- 改 sticky / hysteresis
- 把 `MoveOwner` 重新并入 `engineOwner`

### 当前执行输入

- `docs/authoritative-baseline/README.md`
- `docs/authoritative-baseline/work-packages/README.md`
- `.dualpad-builder/feature_list.json`
- `.dualpad-builder/sprint_plan.json`
- `docs/plans/dualpad_rearchitecture/03_slice_phase2_menu_instance_truth_zh.md`
- `docs/current_input_pipeline_zh.md`
- `docs/menu_context_policy_current_status_zh.md`
- `src/ARCHITECTURE.md`

如需背景材料，可额外回看：

- `docs/phase2_gameplay_presentation_owner_minimal_plan_zh.md`
- `docs/phase1_phase4_code_review_findings_zh.md`
- `docs/gameplay_input_ownership_investigation_and_plan_zh.md`
- `docs/brainstorms/2026-04-08-input-kernel-and-projection-architecture-requirements.md`
- `docs/ideation/2026-04-08-dualpad-ce3-architecture-ideation.md`
- `docs/plans/dualpad_rearchitecture/dualpad_rearchitecture_plan_zh.md`

## 实施步骤

### Step 1：先落地类型与 façade，不切行为

先建立三块最小骨架：

1. `SourceEvidenceCollector`
2. `PresentationProjection`
3. `SkyrimCompatibilitySurface`
4. `GameplayPresentationAdapter`
5. `DeviceFamilyIngressPublisher`

同时把 `InputModalityTracker` 改成 façade 目标形态：

- 保留对外入口：
  - `Install()`
  - `Register()`
  - `OnAuthoritativeMenuOpen(...)`
  - `ProcessEvent(...)`
  - `MarkSyntheticKeyboardScancode(...)`
- 但这些入口内部开始转发到新对象

此步禁止：

- 切 hook
- 删旧字段
- 改 owner 规则

完成标志：

- 能编译
- 新对象已创建并可接日志
- 旧行为完全不变

### Step 2：把原始证据从 tracker 迁到 `SourceEvidenceCollector`

把下面几类状态从 `InputModalityTracker` 移出：

- synthetic keyboard suppression window
- gamepad lease
- mouse move accumulator
- pointer signal 的原始证据
- keyboard / mouse / gamepad 最近一次显式活动

迁移原则：

1. 保留当前阈值和窗口行为，不在这一层改手感。
2. context boundary 发生时，由 `SourceEvidenceCollector` 重置窗口状态，但不决定 owner。
3. `KeyboardHelperBackend` 不再直接让 tracker 维护 suppression 表，而改成调用 collector。

此步完成后，旧 tracker 里允许暂时保留 mirror 字段，但不再新增任何新逻辑。

### Step 3：实现 `PresentationProjection` 的纯决策路径

把现有 tracker 内这几类逻辑改造成单独投影：

- gameplay / menu / cursor / pointer 的外显状态折叠
- menu open / close 的平台继承
- `epoch` 与 `dirty` 计算
- `reason` 记录

这里固定采用三段输入：

1. `SourceEvidenceSnapshot`
2. `PublishedGameplayPresentation`
3. Phase 2 resolved context

切换规则一律写死在投影层，不再散落在 tracker setter 里：

- gameplay 常态平台：取 `engineOwner`
- `Gameplay -> Menu` 初始继承：取 `menuEntryOwner`
- menu 内 keyboard / mouse / gamepad 抢占：只由 source evidence + resolved context policy 决定
- `Menu -> Gameplay`：恢复为 gameplay published presentation，不保留菜单内最后一拍 owner

此步必须加 explain 日志，至少包含：

- `ctx`
- `contextRevision`
- `deviceFamilyRevision`
- `owner`
- `navigationOwner`
- `cursorOwner`
- `pointerIntent`
- `epoch`
- `dirty`
- `reason`
- `gameplayPresentationRevision`

### Step 4：进入 shadow compare，先做结果比对

在不切 hook 的前提下，新增 shadow compare：

- legacy tracker 当前回答值
- `PublishedPresentationState` 推导值

至少比较：

- `IsUsingGamepad`
- `GamepadControlsCursor`
- menu refresh 触发条件
- gameplay -> menu 首帧继承结果

要求：

1. diff log 必须带 `contextRevision` 与 `epoch`。
2. diff log 还必须带 `deviceFamilyRevision` 与 `gameplayPresentationRevision`。
3. 若 diff 出现在以下场景，先修投影，不得继续切 hook：
   - `gameplay -> pause`
   - `gameplay -> journal`
   - `gameplay -> map`
   - menu 内 mouse move 抢占后再用手柄接管
4. 不要为过 diff 临时在 compatibility surface 加特判；应该回到 projection 层修规则。

### Step 5：先切 menu refresh

把 `RefreshMenus()` 的触发权从 tracker setter 副作用迁到 compatibility surface：

- `SkyrimCompatibilitySurface` 订阅 `PublishedPresentationState`
- 当 `dirty` 包含 platform 或 cursor 相关位时，根据 `epoch` 去重后入队一次 UI task

此步之后：

- `SetPresentationOwner()`、`SetCursorOwner()` 这类 legacy setter 允许仍存在，但不得再调 `RefreshMenus()`
- legacy refresh queue 只能保留空壳或日志

验证重点：

- menu 内 keyboard takeover 后再按手柄，不用关菜单也能回切 platform
- 同一 `epoch` 不重复刷新
- 外部状态不变时，不会无意义刷菜单

### Step 6：切 `IsUsingGamepadHook()`

将：

- `InputModalityTracker::IsUsingGamepadHook()`

改为：

- `SkyrimCompatibilitySurface::IsUsingGamepadHook()`

读取规则固定为：

- gameplay：读 `PublishedPresentationState.owner`
- menu：同样读 `PublishedPresentationState.owner`

不再允许：

- gameplay 分支重新读取 `_engineGameplayPresentationLatch`
- gameplay 分支重新拼 `LookOwner + mouse-look active`
- remap mode 下回调 legacy `ResolveIsUsingGamepad()`

切完后先只验证：

1. gameplay HUD / gameplay glyph 的平台稳定性
2. `gameplay -> pause`
3. `gameplay -> journal`
4. `gameplay -> map`

### Step 7：切 `GamepadControlsCursorHook()`

将：

- `InputModalityTracker::IsGamepadCursorHook()`

改为：

- `SkyrimCompatibilitySurface::GamepadControlsCursorHook()`

读取规则固定为：

- 只读 `PublishedPresentationState.cursorOwner`

此步不处理：

- cursor position sync
- cursor warp
- 鼠标最后位置记忆

如果出现“hook 结果对了但 UI 位置没跟上”，这不是 Phase 3 返工点，而是后续 cursor handoff 课题。

### Step 8：最后切 `BSPCGamepadDeviceHandler::IsEnabled()`

改造 `IsGamepadDeviceEnabledHook(...)`：

- 非 remap mode：保留当前直通逻辑
- remap mode：只读 `PublishedPresentationState.owner`

此步的目标不是做新行为，而是去掉旧路径里对 `ResolveIsUsingGamepad()` 的回调依赖。

验证重点：

- player remap mode
- menu remap mode
- remap mode 下 keyboard / gamepad 切换是否仍与 platform 结果一致

### Step 9：清空 legacy tracker 的决策权

只有当前 3 个条件同时成立，才能做这一步：

1. shadow compare 已通过
2. menu refresh 已切到 `epoch + dirty`
3. 3 条 hook 已全部切走

此时把 `InputModalityTracker` 清成过渡 façade：

- 不再持有：
  - `_presentationOwner`
  - `_navigationOwner`
  - `_cursorOwner`
  - `_gameplayOwner`
  - `_gameplayMenuEntrySeed`
  - `_engineGameplayPresentationLatch`
  - `_refreshQueued`
- 不再保留：
  - `PromoteToGamepad(...)`
  - `PromoteToKeyboardMouse(...)`
  - `ResolveIsUsingGamepad()`
  - `SetPresentationOwner(...)`
  - `SetCursorOwner(...)`
  - `SetNavigationOwner(...)`
  - `SetGameplayOwner(...)`
  - `ResolveOwnerPolicy(...)`

允许暂留：

- façade 转发
- 安装 hook 的壳
- 向后兼容 include 点

## 验证与观测

### 构建与基础验证

至少执行：

```powershell
xmake build DualPad
xmake build DualPadMenuContextPolicyTests
xmake run DualPadMenuContextPolicyTests
```

若 Phase 0 的 replay harness 已落地，必须回放至少 4 条资产：

1. gameplay -> pause
2. gameplay -> journal
3. gameplay -> map
4. menu 内 keyboard takeover 后 gamepad reclaim

### 手工验证矩阵

#### A. gameplay -> menu 首帧继承

依次验证：

1. gameplay 中最近一次显式输入是手柄，然后开 `Pause`
2. gameplay 中最近一次显式输入是键鼠，然后开 `Pause`
3. 同样验证 `Journal`
4. 同样验证 `Map`

预期：

- 菜单首帧平台正确
- 不出现“只有 d-pad 有反应、面键不工作、鼠标 UI 残留”

#### B. menu 内 owner 抢占与回切

依次验证：

1. 用鼠标移动或点击把菜单切到 `KeyboardMouse`
2. 不关菜单，直接再用手柄接管

预期：

- platform 可在当前菜单内回到手柄
- `epoch` 只递增一次
- 没有重复 refresh

#### C. gameplay 常态平台稳定性

依次验证：

1. 鼠标看视角后，再用右摇杆看视角
2. 只动左摇杆移动，不碰视角
3. 只按扳机 / 面键

预期：

- gameplay 平台只由 `PublishedGameplayPresentation.engineOwner` 决定
- 左摇杆仍只影响 menu-entry seed，不直接把 gameplay engine presentation 错切成手柄

#### D. remap mode

依次验证：

1. player remap mode
2. menu remap mode

预期：

- `BSPCGamepadDeviceHandler::IsEnabled()` 与 published owner 一致
- 不回调 legacy tracker 再做一次 owner 解释

### 日志与观测要求

必须新增并保留以下日志类别，直到本 slice 完成：

- `[DualPad][SourceEvidence]`
- `[DualPad][PresentationProjection]`
- `[DualPad][SkyrimCompat]`
- `[DualPad][PresentationParity]`

`[DualPad][PresentationParity]` 至少包含：

- legacy value
- projected value
- `uiContext`
- `contextRevision`
- `deviceFamilyRevision`
- `gameplayPresentationRevision`
- `epoch`
- `reason`

本 slice 完成前，不能删这些日志，只能降级为 debug。

### Shadow Compare Exit Gate

在任何一个 hook 切换前，`PresentationParity` 必须满足下面的硬门槛：

1. 必跑场景固定为：
   - Phase 0 replay 里的 `gameplay -> pause`
   - Phase 0 replay 里的 `gameplay -> journal`
   - Phase 0 replay 里的 `gameplay -> map`
   - menu 内 keyboard takeover 后 gamepad reclaim
   - live `player remap mode`
   - live `menu remap mode`
2. 允许存在的 intentional diff 只限：
   - legacy 路径不存在的 `deviceFamilyRevision`
   - legacy 路径不存在的 `gameplayPresentationRevision`
   - explain `reason` 枚举更细，但 UI-facing 结果完全相同
3. 下面任一 diff 都直接阻断 cutover：
   - `owner`
   - `cursorOwner`
   - `uiContext`
   - `epoch` 对应的 menu refresh 次数
   - `BSPCGamepadDeviceHandler::IsEnabled()` 结果
4. live parity 期间唯一允许保留的比较对象叫 `PresentationParityRecorder`。
   - 它只允许记录日志和 debug 计数
   - 不允许回答 hook
   - 不允许改 `PublishedPresentationState`
   - 不允许触发 `RefreshMenus()`
5. 当 menu refresh 和 3 条 hook 全部切走后，live runtime 里的 legacy 分支选择必须在同一轮收口里删除；
   Phase 5 开始接 runtime shell 前，只允许保留 test / debug-only parity observer。

## 退出条件

满足下面全部条件，Phase 3 才算完成：

1. `PublishedPresentationState` 已成为以下四个消费面的唯一已发布真相源：
   - `IsUsingGamepad`
   - `GamepadControlsCursor`
   - `BSPCGamepadDeviceHandler::IsEnabled`
   - menu refresh
2. `InputModalityTracker` 不再持有 presentation 决策状态，只剩 façade / forwarding 职责。
3. `SourceEvidenceCollector` 不直接产生任何 UI-facing owner 结论。
4. `PresentationProjection` 不直接读取 raw RE 事件链或 gameplay owner 内部字段。
5. `SkyrimCompatibilitySurface` 不直接读取 `GameplayOwnershipCoordinator` 或 `ContextManager`。
6. `gameplay -> pause / journal / map` 首帧平台验证通过。
7. menu 内 keyboard takeover 后 gamepad reclaim 验证通过。
8. remap mode 验证通过。
9. shadow parity 在主验证矩阵里无未解释 diff。
10. `deviceFamilyRevision` 已由 `PublishedDeviceFamilyEvidence` 正式发布，且没有任何 consumer
    通过 `PublishedPresentationState.family` 逆推它。
11. `PublishedGameplayPresentation` 已成为 `PresentationProjection` 的唯一 gameplay 输入面。
12. 若本 slice 仍暂时保留 `Phase2ResolvedContextFeedAdapter`，它已经逐字段转发
    `uiContext / contextRevision / actionSetStack / presentationPolicyId / topMenuInstanceId / identityQuality / menuStackRevision`，
    且没有任何本地 `ContextCatalog` 反查或第二份 policy owner。

只要有一条没满足，就不能宣告本 slice 结束。

### Rollback Boundary / Emergency Revert

Phase 3 的 cutover 必须按下面 4 个可回滚边界推进，禁止把所有切换绑成一个不可拆的大提交：

1. `Shadow Publish Only`
   - 新类型已落地
   - `GameplayPresentationAdapter` 与 `DeviceFamilyIngressPublisher` 已发布 seam
   - 所有 hook 与 menu refresh 仍走 legacy 路径
2. `Menu Refresh Cutover`
   - `RefreshMenus()` 已改读 `PublishedPresentationState.epoch + dirty`
   - 三条 hook 仍可逐条回退
3. `Hook-by-Hook Cutover`
   - `IsUsingGamepad`、`GamepadControlsCursor`、`BSPCGamepadDeviceHandler::IsEnabled`
     必须分开提交、分开回退
4. `Legacy Tracker Decision Removal`
   - 只有当前 3 个 hook 都已切走且 parity 通过，才允许删 tracker 决策代码

若 live smoke 失败，回退顺序固定为：

1. 先回退 `BSPCGamepadDeviceHandler::IsEnabled()`
2. 再回退 `GamepadControlsCursorHook()`
3. 再回退 `IsUsingGamepadHook()`
4. 最后才允许回退 menu refresh

回退时允许暂留的对象只有：

- `InputModalityTracker` façade
- `GameplayPresentationAdapter`
- `DeviceFamilyIngressPublisher`
- `PresentationParityRecorder`

回退时明确禁止：

- 把 owner 规则塞回 `SkyrimCompatibilitySurface`
- 重新在 setter 副作用里触发 `RefreshMenus()`
- 为了临时续命而直接让 `PresentationProjection` 读取 `GameplayOwnershipCoordinator`
- 保留新 replay / explain 基线却回退 runtime 代码；代码、golden、日志字段必须同轮回退

## 交接给下一 slice 的合同

给后续 slice，尤其是：

- `05_slice_phase4_action_graph_and_interaction_engine_zh.md`
- `06_slice_phase5_gameplay_projection_zh.md`
- `07_slice_phase6_prompt_projection_zh.md`

固定交接以下合同。

### 合同 A：稳定的 published presentation 输入面

后续任何模块如果需要 UI-facing 平台事实，只能读取：

- `PublishedPresentationState`

不得再新增：

- 直接读 `InputModalityTracker`
- 直接读 `GameplayOwnershipCoordinator`
- 直接查 `ContextManager` 再自行折叠平台

### 合同 B：稳定的 gameplay adapter seam

`PresentationProjection` 对 gameplay 侧只接受一个输入：

- `PublishedGameplayPresentation`

Phase 5 只允许替换 `GameplayPresentationAdapter` 的 producer，从 coordinator published state
切到 `GameplayPresentationPublisher`；不允许顺手改 `PresentationProjection` 的消费协议、
字段名、revision 语义或发布时间点。

### 合同 C：稳定的 source evidence seam

所有原始 keyboard / mouse / gamepad 证据，以及 synthetic suppression，统一先进入：

- `SourceEvidenceCollector`

后续 slice 不允许再往 compatibility surface 或 projection 里补新的原始事件采集逻辑。

### 合同 D：Phase 2 context seam 的 owner 不变

后续任何 slice 如果需要读取或转发 Phase 2 的上下文合同字段，只能消费：

- `ResolvedContextSnapshot`
- 或建立在它之上的只转发 seam

其中 `presentationPolicyId` 的 owner 固定仍是 Phase 2 `ContextResolver`，来源固定仍是
Phase 1 `ContextCatalog.presentationPolicyId`。Phase 4 以后允许继续消费或转发它，但不允许从
`UiContextId`、`ActionSetStack`、`ContextCatalog` 或本地 policy 表重新反查第二份值。

### 合同 E：Prompt 与 glyph 的消费方式

Phase 6 开始后，`PromptProjection / PromptService` 只能消费：

- `PublishedPresentationState.family`
- `PublishedPresentationState.deviceFamilyRevision`
- `PublishedPresentationState.uiContext`
- `PublishedPresentationState.actionSets`
- `PublishedPresentationState.epoch`

不得再把 `InputModalityTracker` 当 glyph 平台事实源，也不得从 `family` 逆推
`deviceFamilyRevision`。

### 合同 E：稳定的 device family seam

后续任何需要 `deviceFamilyRevision` 的模块都只能读取：

- `PublishedDeviceFamilyEvidence`
- `SourceEvidenceSnapshot` 中镜像的 `deviceFamilyRevision`
- `PublishedPresentationState.deviceFamilyRevision`

不允许新增：

- 私有自增计数器
- 从 `PublishedPresentationState.family` 逆推 revision
- 从 hook 结果或 glyph 结果反推 device family

### 合同 F：legacy tracker 的处置口径

在 Phase 8 之前，`InputModalityTracker` 可以继续保留文件和外部名字，
但它的角色已经固定为：

- façade
- forwarding shim
- 向后兼容安装点

不允许在后续 slice 里重新往它塞回 owner 规则、hook 规则或 refresh 规则。
