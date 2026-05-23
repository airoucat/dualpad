# Phase 2 Slice：菜单实例真相源

## 目标

本 slice 的目标是把当前 `ContextEventSink -> ContextManager -> InputModalityTracker / ScaleformGlyphBridge` 这条“按菜单名维护上下文”的链，切换成“按菜单实例维护真相”的链，同时保持现有调用面可继续工作。

本 slice 完成后，运行时需要满足下面 4 条约束：

1. 菜单相关的 authoritative source 不再是 `ContextManager` 里的 `menuName + _passthroughMenuCounts`，而是 `UiMenuObserver -> MenuInstanceRegistry -> ContextResolver`。
2. `ContextManager` 退化为 **legacy mirror**，继续提供 `GetCurrentContext()` 和 `GetCurrentEpoch()`，但不再自己推导菜单栈。
3. `ActionSetResolver` 成为 `UiContextId -> ActionSetStack` 的唯一运行时入口，`ScaleformGlyphBridge`、后续 `PresentationProjection`、后续 `PromptProjection` 都不能再各自推导菜单语义。
4. 任何 unknown / degraded / partial case 都必须显式标记，不能继续走 `ParseInputContextName(...).value_or(InputContext::Menu)` 这种 silent fallback。

本 slice **不做**下面这些事：

- 不拆 `InputModalityTracker` 的 presentation 责任；那是 Phase 3 的范围。
- 不改 `ScaleformGlyphBridge` 的 token 反查语义；那是 Phase 6 的范围。
- 不改 gameplay ownership、`NativeButtonCommitBackend` 的 contract；本 slice 只保证它们继续吃到稳定的 legacy `InputContext + epoch`。
- 不把 `FavoritesMenu` 的页面级 broker、SWF workspace 或 prompt 语义混进本 slice。
## 当前 repo reality 缺口与 breach 边界

- 在 `Phase 2` 真正开工前，当前 repo 允许尚不存在下列 deliverables：
  - `src/input_v2/context/`
  - `target("DualPadContextResolverTests")`
- 上述目录与 target 属于本 slice 要新增的 menu-instance / resolver surface，不是当前 baseline 已兑现的现状事实。
- 只有在 `Phase 2` 被宣称 done / promoted / handoff gate 已满足后，这些入口仍缺失，才构成正式 breach。

## 冻结的设计决定

### `UiMenuObserver`

`UiMenuObserver` 的 authoritative source 固定为：**主线程上 `RE::UI` 的实时 open-menu snapshot**。`RE::MenuOpenCloseEvent` 只作为“需要刷新”的提示，不再作为上下文真相源。

`UiMenuObserver` 每次采样必须输出一份不可变快照，至少包含下面字段：

```cpp
struct ObservedMenuNode {
    std::uintptr_t menuPtr;
    std::string menuName;
    std::uint32_t menuFlagsValue;
    std::uint32_t inputContextValue;
    std::int32_t depthPriority;
    std::uintptr_t delegatePtr;
    std::uintptr_t moviePtr;
    std::uint32_t observationOrder;
};
```

`UiMenuObserver` 的 completeness 固定分成 3 档：

```cpp
enum class ObserverCompleteness {
    Complete,
    Partial,
    Unavailable
};
```

冻结规则如下：

1. `Complete`
   - 当前帧能完整枚举并读取所有 open menu。
   - 只有 `Complete` 快照允许 `MenuInstanceRegistry` 做 destructive close。
2. `Partial`
   - 当前帧能读到一部分菜单，但至少一个菜单无法完整解引用。
   - `Partial` 快照允许“更新已看到的实例”和“新增已看到的实例”，**不允许**根据缺席做 close。
3. `Unavailable`
   - 当前帧拿不到 `RE::UI` 或完全无法构造快照。
   - `Unavailable` 时保持上一份 registry / resolver 发布值，不做任何菜单关闭推断。

#### Capture and Publish Timing

`UiMenuObserver` 的采样和发布时序也在本 slice 里一次定死：

1. 采样入口固定为主线程上的单一刷新点，例如 `ContextEventSink` 驱动的 `ContextRefreshTick()`。
2. `RE::MenuOpenCloseEvent` 只允许做两件事：
   - 置 `observerDirty = true`
   - 记录最近一次事件时间戳 / 序号用于日志
3. 事件处理函数禁止直接调用 `UiMenuObserver::Capture()`，也禁止直接 mutation `ContextManager`。
4. 每个主线程 frame 最多只允许一次 `Capture()` 和一次 publish。
5. refresh tick 的执行顺序固定为：
   - `UiMenuObserver::Capture()`
   - `MenuInstanceRegistry::ReconcileAndPublish()`
   - `ContextResolver::ResolveAndPublish()`
   - `ContextManager::ApplyResolvedContext()`
6. refresh tick 的触发条件固定为：
   - `observerDirty == true`
   - 或上一份 published snapshot 非空
   - 或上一份 published snapshot 的 completeness 不是 `Complete`
7. 下游查询路径只允许读取最后一份 published snapshot 或其派生结构；禁止在 `ScaleformGlyphBridge`、`InputModalityTracker`、`ContextResolver`、`ActionSetResolver`、`ContextManager` 查询链路内懒采样。
8. 同一 frame 内若收到多次 open / close event，只能合并成一次 capture；不得在同一 frame 内重入二次采样。

### `MenuInstanceRegistry`

`MenuInstanceRegistry` 的职责固定为：**把 observer 的菜单节点快照，变成带稳定 `MenuInstanceId` 的实例栈**。它不负责上下文判定，不负责 action set 判定，不负责 glyph。

`MenuInstanceId` 的分配优先级固定如下：

1. `menuPtr`
   - 只要非零且当前快照仍可见，`menuPtr` 是首选身份锚点。
2. `delegatePtr` / `moviePtr` 指纹
   - 仅当 `menuPtr` 抖动，但 `menuName + delegatePtr + moviePtr` 在连续两次快照间可唯一匹配时，允许视作同一实例延续。
3. 降级身份
   - 如果 `menuPtr == 0`，且 `delegatePtr` / `moviePtr` 也不足以形成唯一匹配，只能进入降级模式，绝不把 `menuName` 当实例 ID。

实例身份质量固定为：

```cpp
enum class MenuIdentityQuality {
    StablePointer,
    FingerprintRebound,
    DegradedIdentity
};
```

`MenuInstanceRegistry` 发布的结构必须至少带上：

```cpp
struct ReconciledMenuStack {
    std::vector<TrackedMenuInstance> trackedMenus;
    std::vector<TrackedMenuInstance> passthroughOverlays;
    std::uint32_t menuStackRevision;
};
```

冻结规则如下：

1. 相同 `menuName` 的多个实例必须允许并存；不能再用菜单名计数推导当前 top。
2. `DegradedIdentity` 只能在当前 published revision 内使用，不能跨 revision 复用为长期稳定实例。
3. 对 `Partial` 快照，registry 必须保留“上次已知但本次缺席”的实例，直到下一次 `Complete` 快照确认其关闭。
4. top menu 的排序规则固定为：
   - 先按 `depthPriority` 降序
   - 再按 `firstSeenRevision` 降序
   - 最后按 `MenuInstanceId` 稳定排序
5. `MenuInstanceRegistry` 不直接输出 legacy `InputContext`，只输出实例栈、实例身份质量和原始 runtime facts。
6. `menuStackRevision` 的 authoritative owner 固定是 `MenuInstanceRegistry`。
7. `menuStackRevision` 只在 publish 新 `ReconciledMenuStack` 时递增，且递增条件固定为：
   - tracked menu 新增 / 确认关闭
   - passthrough overlay 新增 / 确认关闭
   - top tracked menu 改变
   - 已发布实例的 `MenuInstanceId`、`MenuIdentityQuality`、`depthPriority`、`firstSeenRevision`、tracked / overlay 分类发生变化
   - published shape、identity、top tracked menu 或 tracked / overlay classification 等实际发布语义发生变化
8. `Unavailable` 快照不发布新 stack，因此不得推进 `menuStackRevision`。
9. `Partial` 快照允许推进 `menuStackRevision`，但原因只能是“新看到或更新了已看到的实例”；不得因为缺席推断 close。
10. `lastSeenRevision` 是 bookkeeping / diagnostic 字段，仅用于调试和观测实例最近一次被看到的时间点；纯 `lastSeenRevision` 更新不参与 `menuStackRevision` 推进，也不得被下游当作发布语义变化。

### `ContextResolver`

`ContextResolver` 是菜单 / 上下文 / legacy mirror 的唯一决策器。它的输入只允许来自：

- `ContextCatalog`（Phase 1 产物）
- `MenuInstanceRegistry`
- gameplay base facts（当前仍可沿用 `ContextManager::DetectGameplayContext()` 的结果）

`ContextResolver` 明确不允许直接读取：

- `ScaleformGlyphBridge`
- `BindingManager`
- `InputModalityTracker`
- `MenuContextPolicy`
- `InputContextNames`

`ContextResolver` 的输出固定为：

```cpp
struct ResolvedContextSnapshot {
    HostMode hostMode;
    GameplaySubstate gameplaySubstate;
    UiContextId uiContextId;
    std::optional<MenuInstanceId> topMenuInstanceId;
    MenuIdentityQuality identityQuality;
    std::uint32_t menuStackRevision;
    ActionSetStack actionSetStack;
    PresentationPolicyId presentationPolicyId;
    std::uint32_t contextRevision;
    InputContext legacyInputContext;
    std::uint32_t legacyContextEpoch;
};
```

冻结规则如下：

1. 内部 canonical 输出永远是 `UiContextId`，不是 `InputContext`。
2. `menuStackRevision` 必须直接来自 `MenuInstanceRegistry` 的 published stack，`ContextResolver` 不得自行合成。
3. unknown tracked menu 的内部输出固定为 `UiContextId::UnknownTrackedMenu`。
4. unknown passthrough overlay 的内部输出固定为 `UiContextId::PassthroughOverlay`，它只存在于观测栈里，不参与 top context 决策。
5. `legacyInputContext` 只能由 resolver 在最后一步镜像生成；除兼容层外，运行时其它模块不再自己做字符串解析或 `value_or(Menu)`。
6. `legacyInputContext` 的镜像规则必须直接引用 Phase 1 的 `UiContextId / canonicalContextName / legacyInputContext` 合同，不允许 Phase 2 再写第二张映射表。
7. `legacyContextEpoch` 保持当前 contract：
   - gameplay 子状态之间的切换，例如 `Gameplay -> Sneaking`，**不推进** legacy epoch；
   - 菜单域和 gameplay 域之间切换，或菜单上下文从 `InventoryMenu -> JournalMenu` 这类切换，推进 legacy epoch。
8. 如果 top menu 处于 `DegradedIdentity`，且存在同名碰撞或 catalog 无法唯一判定，resolver 必须输出 `UiContextId::UnknownTrackedMenu`，不能猜成 `FavoritesMenu`、`JournalMenu` 等专用上下文。
9. `presentationPolicyId` 的 authoritative owner 固定是 `ContextResolver`；它的唯一来源固定为
   Phase 1 `ContextCatalog` 上与本次 canonical `UiContextId` 对应的 `presentationPolicyId` 字段。
10. `presentationPolicyId` 必须与 `UiContextId / ActionSetStack` 在同一次 publish 中一起冻结并写入
    `ResolvedContextSnapshot`；下游模块只允许转发这个已发布字段，不允许再从 `UiContextId`、
    `ContextCatalog` 或任何局部 policy 表反查第二份值。
11. `contextRevision` 的 authoritative owner 固定是 `ContextResolver`。
12. `contextRevision` 只在 publish 新 `ResolvedContextSnapshot` 时递增，且递增条件固定为下面任一项变化：
    - `hostMode`
    - `gameplaySubstate`
    - `uiContextId`
    - `topMenuInstanceId`
    - `identityQuality`
    - `menuStackRevision`
    - `actionSetStack`
    - `presentationPolicyId`
    - `legacyInputContext`
    - `legacyContextEpoch`

### `ActionSetResolver`

`ActionSetResolver` 的职责固定为：**把 `ResolvedContextSnapshot` 映射成 `ActionSetStack`**。它不负责 menu instance identity，也不负责 legacy mirror。

`ActionSetStack` 的结构在本 slice 固定为：

```cpp
struct ActionSetStack {
    ActionSetId baseSetId;
    std::vector<ActionLayerId> activeLayerIds;
    std::vector<ScopeAnchorId> scopeAnchorIds;
};
```

`ActionSetResolver` 必须直接引用 Phase 1 的 `defaultActionSetId / defaultLayerIds / scopeAnchorIds` 合同，不允许再从 legacy `InputContext`、菜单名、旧 `BindingManager` 推回 action set。

冻结规则如下：

1. gameplay 无菜单时：
   - `baseSetId = GameplayBase`
   - `activeLayerIds = gameplaySubstate 对应的固定 layer`
   - `scopeAnchorIds = [GameplayBase, ...gameplaySubstate layers]`
2. tracked menu 命中 catalog 时：
   - `baseSetId = catalog.defaultActionSetId`
   - `activeLayerIds = catalog.defaultLayerIds`
   - `scopeAnchorIds = catalog.scopeAnchorIds`
3. `UnknownTrackedMenu` 时：
   - `baseSetId = MenuBase`
   - `activeLayerIds = [UnknownTrackedMenuLayer]`
   - `scopeAnchorIds = [MenuBase, UnknownTrackedMenuLayer]`
   - 不允许猜测性叠加 `FavoritesLayer`、`MapLayer`、`JournalLayer`
4. passthrough overlay 永远不改变 base set，也不叠加 layer。
5. 本 slice 内只允许 **top tracked menu** 决定 action set；底层菜单不参与 layer 叠加。多菜单层叠的 richer policy 留给后续 slice，不在本阶段扩 scope。
6. 当前 repo 在本 slice 只允许两类 base set：
   - `GameplayBase`
   - `MenuBase`
   任何新的 base set 都不能在本 slice 的实现阶段临时发明。

### 迁移顺序

迁移顺序在本 slice 内固定为：

1. 先落 `UiMenuObserver`
2. 再落 `MenuInstanceRegistry`
3. 再落 `ContextResolver`
4. 再落 `ActionSetResolver`
5. 最后把 `ContextManager` 切成 legacy mirror

禁止反向顺序：

- 不允许先改 `InputModalityTracker` 再倒逼 resolver 补设计。
- 不允许先改 `ScaleformGlyphBridge` 让它直接读 `RE::UI`。
- 不允许在 `ContextManager` 里继续追加 name-only 菜单栈逻辑作为“过渡”。

### Shadow Compare Exit Gate

本 slice 必须先走 shadow compare，再切 authoritative。shadow compare 的退出门槛现在就定死：

1. 必跑场景固定为：
   - gameplay -> menu -> gameplay
   - `InventoryMenu`
   - `JournalMenu`
   - `MapMenu`
   - `FavoritesMenu`
   - 常见 HUD / passthrough overlay
   - same-name duplicate menu
   - `Partial`
   - `Unavailable`
2. 允许的 intentional difference 白名单只包括：
   - unknown tracked menu 统一落到 `UiContextId::UnknownTrackedMenu`
   - degraded identity 时不再猜专用菜单上下文
   - passthrough overlay 不再把自身抬升成 active tracked menu
3. 字段级 compare gate 也在本节冻结，任一 replay / live compare 都必须至少记录并比对：
   - `topMenuInstanceId`
   - `identityQuality`
   - `menuStackRevision`
   - `uiContextId`
   - `ActionSetStack`
   - `presentationPolicyId`
   - `legacyInputContext`
   - `legacyContextEpoch`
   - `contextRevision`
4. 这些字段的 diff 语义也在这里固定：
   - `legacyInputContext`、`legacyContextEpoch` 是 hard parity fields；除第 2 条白名单直接要求的最小变化外，不允许出现任何受控差异。
   - `menuStackRevision`、`contextRevision` 是 revision parity fields；必须与同一轮 observed stack / canonical context 变化一一对应，不允许跳号、漏推或额外抖动，intentional difference 不能豁免 revision 语义。
   - `presentationPolicyId` 是 publish parity field；它必须与同一轮 canonical `UiContextId` 对应的 `ContextCatalog.presentationPolicyId` 完全一致，并与 `UiContextId / ActionSetStack` 在同一次 publish 中一起冻结。它不属于 intentional difference 白名单，不能以 unknown tracked fallback、degraded identity 或 passthrough overlay 作为豁免理由；只要出现独立 diff、滞后更新，或与同轮 `UiContextId` 不一致，一律视为未解释 diff。
   - `topMenuInstanceId`、`identityQuality`、`uiContextId`、`ActionSetStack` 只允许在第 2 条白名单覆盖的场景里出现受控差异，且 compare 日志必须明确标出触发的是哪一条白名单；除此之外一律视为未解释 diff。
5. 任一未解释 diff 都阻断 authoritative cutover。
6. cutover 后，旧 `ContextManager` 推导路径只允许保留在测试或 debug-only compare 中，不得再给 runtime 正式消费。
7. live shadow compare path 不得跨出本 slice；在本 slice 退出条件成立前，必须已经降到 test/debug only。

### Rollback Boundary / Emergency Revert

本 slice 的回滚边界也在这里固定，不允许到 cutover 失败时临场拍板：

1. 安全回滚边界 A
   - `UiMenuObserver` / `MenuInstanceRegistry` / `ContextResolver` / `ActionSetResolver` 已落地
   - 但 `ContextManager` 仍是正式 authority
   - 新路径只做日志和 replay 对比
2. 安全回滚边界 B
   - authoritative cutover 已完成
   - `ContextManager` 已退化为 mirror
   - 旧 authority 已删除
3. 若 live smoke 在 cutover 后失败，只允许回滚到边界 A；不得在边界 B 上重新启用 `_menuStack`、`_passthroughMenuCounts` 或 `OnMenuOpen/OnMenuClose` 形成第三套 authority。
4. 允许暂留的 shim 只有：
   - `ContextManager::ApplyResolvedContext()`
   - debug-only compare logging
5. 回滚时必须同时回滚：
   - 对应 replay 基线
   - 对应日志字段
   - 对应文档中的 intentional difference 白名单

## 前置依赖

开始本 slice 之前，下面内容必须已经存在；缺任何一项，都先回上一阶段补齐，不要在本阶段临时发明替代方案：

1. `docs/plans/dualpad_rearchitecture/02_slice_phase1_catalog_and_manifest_compiler_zh.md` 已落地的 Phase 1 产物：
   - `ContextCatalog`
   - `ActionManifest`
   - `LegacyIniImporter`
2. `ContextCatalog` 已正式发布下面这些字段：
   - `UiContextId`
   - `canonicalContextName`
   - `legacyInputContext`
   - `presentationPolicyId`
   - `defaultActionSetId`
   - `defaultLayerIds`
   - `scopeAnchorIds`
3. `ContextCatalog` 里已经有这些 canonical 项：
   - `UiContextId::None`
   - `UiContextId::Inventory`
   - `UiContextId::Journal`
   - `UiContextId::Map`
   - `UiContextId::Favorites`
   - `UiContextId::Book`
   - `UiContextId::Console`
   - `UiContextId::UnknownTrackedMenu`
   - `UiContextId::PassthroughOverlay`
4. `ActionManifest` 里已经有这些 base set / layer：
   - `GameplayBase`
   - `MenuBase`
   - `UnknownTrackedMenuLayer`
   - 当前已知菜单对应的 layer，例如 `InventoryLayer`、`JournalLayer`、`MapLayer`、`FavoritesLayer`
   - 当前 gameplay substate 对应的 layer，例如 `CombatLayer`、`SneakLayer`
5. Phase 0 的 replay / diff barrier 已可用，至少覆盖：
   - gameplay -> menu -> gameplay
   - `InventoryMenu`
   - `JournalMenu`
   - `MapMenu`
   - `FavoritesMenu`
   - 常见 HUD / overlay 菜单
6. 现有 `MenuContextPolicy` 行为文档已作为对照输入固定：
   - `docs/menu_context_policy_current_status_zh.md`

## 涉及代码与文档

### 新增代码

- `src/input_v2/context/MenuObservation.h`
  - 定义 observer / registry 共享的 snapshot、completeness、identity quality 结构。
- `src/input_v2/context/UiMenuObserver.h`
- `src/input_v2/context/UiMenuObserver.cpp`
  - 主线程 UI 采样器。
- `src/input_v2/context/MenuInstanceRegistry.h`
- `src/input_v2/context/MenuInstanceRegistry.cpp`
  - 菜单实例 diff、实例 ID 分配、top 排序、`menuStackRevision` 发布。
- `src/input_v2/context/ResolvedContext.h`
  - `ResolvedContextSnapshot`、`LegacyContextMirrorState`、`ActionSetStack`。
- `src/input_v2/context/ContextResolver.h`
- `src/input_v2/context/ContextResolver.cpp`
  - `MenuInstanceRegistry + ContextCatalog + gameplay base facts -> ResolvedContextSnapshot`。
- `src/input_v2/context/ActionSetResolver.h`
- `src/input_v2/context/ActionSetResolver.cpp`
  - `ResolvedContextSnapshot -> ActionSetStack`。
- `tests/input_v2/MenuInstanceRegistryTests.cpp`
- `tests/input_v2/ContextResolverTests.cpp`
- `tests/input_v2/ActionSetResolverTests.cpp`

### 修改代码

- `src/input/ContextEventSink.cpp`
  - 不再直接把 `MenuOpenCloseEvent` 当 authoritative context mutation；改成“置 dirty + 驱动刷新”。
- `src/input/InputContext.h`
- `src/input/InputContext.cpp`
  - `ContextManager` 退化为 legacy mirror，删除 name-only stack authority。
- `src/input/HidReader.cpp`
  - 如果 `ContextManager` API 保持不变，这里只需要跟随 include 或初始化路径变化，不改语义。
- `src/input/backend/NativeButtonCommitBackend.cpp`
  - 同上，继续吃 legacy `InputContext + epoch`，不改语义。
- `xmake.lua`
  - 新增 `MenuInstanceRegistry` / `ContextResolver` / `ActionSetResolver` 测试目标。

### 只读对照，不在本 slice 改语义

- `src/input/MenuContextPolicy.h`
- `src/input/MenuContextPolicy.cpp`
- `src/input/InputContextNames.h`
- `src/input/InputContextNames.cpp`
- `src/input/InputModalityTracker.h`
- `src/input/InputModalityTracker.cpp`
- `src/input/glyph/ScaleformGlyphBridge.h`
- `src/input/glyph/ScaleformGlyphBridge.cpp`

### 当前执行输入

- `docs/authoritative-baseline/README.md`
- `docs/authoritative-baseline/work-packages/README.md`
- `.dualpad-builder/feature_list.json`
- `.dualpad-builder/sprint_plan.json`
- `docs/menu_context_policy_current_status_zh.md`
- `docs/current_input_pipeline_zh.md`
- `docs/plans/dualpad_rearchitecture/02_slice_phase1_catalog_and_manifest_compiler_zh.md`
- `src/ARCHITECTURE.md`

如需背景材料，可额外回看：

- `docs/gameplay_input_ownership_investigation_and_plan_zh.md`
- `docs/plans/menu_context_runtime_policy_plan_zh.md`
- `docs/plans/dualpad_rearchitecture/dualpad_rearchitecture_plan_zh.md`

## 实施步骤

### 1. 先定义 Phase 2 的共享数据模型

先在 `src/input_v2/context/MenuObservation.h` 和 `src/input_v2/context/ResolvedContext.h` 定义纯数据结构，不接 Skyrim hook，不接旧逻辑。需要一次性把下面这些类型定清：

```cpp
enum class ObserverCompleteness;
enum class MenuIdentityQuality;

struct ObservedMenuNode;
struct ObservedMenuSnapshot;

using MenuInstanceId = std::uint64_t;

struct TrackedMenuInstance;
struct ReconciledMenuStack;

struct ActionSetStack;
struct ResolvedContextSnapshot;
struct LegacyContextMirrorState;
```

这个步骤的要求是把下面 4 层明确拆开：

1. `UiMenuObserver` 看到的原始事实
2. `MenuInstanceRegistry` 维护的实例真相
3. `ActionSetResolver` 发布的 base set + layer stack
4. `ContextResolver` 发布的 canonical 语义结论

禁止再把菜单名、实例身份、legacy fallback、action set 混进同一个结构。

### 2. 落 `UiMenuObserver`，但先只做事实采样，不做语义判定

在 `src/input_v2/context/UiMenuObserver.*` 实现 `Capture()`，固定要求如下：

1. 只能在主线程 refresh tick 调用。
2. 直接枚举 `RE::UI` 当前 open menu。
3. 每个 `ObservedMenuNode` 都记录 `menuPtr / menuName / flags / inputContext / depth / delegatePtr / moviePtr / observationOrder`。
4. 如果单个菜单无法完整读取，整次采样标成 `Partial`，但仍返回已成功采样到的节点。
5. `ContextEventSink` 只负责：
   - 记录“菜单有变化”的 dirty 信号
   - 在 open / close 事件后要求下一次 refresh tick 刷新 observer
   - 不再自己维护 context stack
6. 所有正式消费者只读 published snapshot，不允许查询路径懒采样。

### 3. 落 `MenuInstanceRegistry`，先把“名字栈”替换成“实例栈”

在 `src/input_v2/context/MenuInstanceRegistry.*` 中实现 `ReconcileAndPublish(const ObservedMenuSnapshot&)`，固定规则如下：

1. 同一 `menuPtr` 连续出现时，必须保持同一个 `MenuInstanceId`。
2. `menuPtr` 变化，但 `menuName + delegatePtr + moviePtr` 形成唯一匹配时，允许 `FingerprintRebound`。
3. 只有 `menuName` 相同绝不算同一实例。
4. `Partial` 快照不允许 close 缺席实例。
5. `Unavailable` 快照不允许发布新 stack，也不允许推进 `menuStackRevision`。
6. 输出时必须带上：
   - `MenuInstanceId`
   - `menuName`
   - `depthPriority`
   - `MenuIdentityQuality`
   - `firstSeenRevision`
   - `lastSeenRevision`
   - `menuStackRevision`
   - observer 原始 runtime facts
7. `lastSeenRevision` 只作为 bookkeeping / diagnostic 输出；如果其它发布字段未变化，单纯刷新 `lastSeenRevision` 不允许推进 `menuStackRevision`。

这一步完成后，先用纯测试验证下面场景：

1. 同名菜单重复打开
2. 同名菜单交错关闭
3. pointer rebinding
4. partial snapshot 不误关
5. overlay + tracked menu 混合存在
6. `menuStackRevision` 只在允许的 publish 边界推进

### 4. 落 `ContextResolver`，让 canonical 输出从 `InputContext` 变成 `UiContextId`

在 `src/input_v2/context/ContextResolver.*` 中实现：

```cpp
ResolvedContextSnapshot Resolve(
    const ReconciledMenuStack& menuStack,
    GameplaySubstate gameplaySubstate,
    const ContextCatalog& catalog,
    const ActionSetResolver& actionSetResolver);
```

固定决策顺序如下：

1. 先从 `ReconciledMenuStack` 过滤掉 `PassthroughOverlay`
2. 再选出 top tracked menu
3. 用 `ContextCatalog` 同时做 canonical `UiContextId + presentationPolicyId` 判定
4. 再调用 `ActionSetResolver`
5. 最后才镜像出 `legacyInputContext + legacyContextEpoch`

固定 fallback 规则如下：

1. catalog 明确命中时，发布对应 `UiContextId`
2. runtime facts 很强，但 catalog 没有条目时，发布 `UiContextId::UnknownTrackedMenu`
3. `DegradedIdentity` 且同名冲突时，发布 `UiContextId::UnknownTrackedMenu`
4. 没有 tracked menu 时，回到 gameplay / console host mode

固定 legacy mirror 规则如下：

1. 必须直接引用 Phase 1 的 `legacyInputContext` 映射
2. `UiContextId::UnknownTrackedMenu -> InputContext::Menu`
3. `UiContextId::PassthroughOverlay` 永远不单独镜像成 active legacy context
4. gameplay host mode 在无 tracked menu 时继续镜像为 `InputContext::Gameplay`

同时把 `presentationPolicyId` 与 `contextRevision` 的发布时点写死在这里：两者都只能随
`ResolvedContextSnapshot` 一起更新，不允许 Phase 3 或后续 slice 再各自维护第二份上下文 policy /
revision。

### 5. 落 `ActionSetResolver`，但本阶段只认 top tracked menu

在 `src/input_v2/context/ActionSetResolver.*` 中把动作集规则写死成纯函数，不在 runtime 再猜：

1. gameplay：
   - `GameplayBase + gameplaySubstate layers`
2. tracked menu 命中 catalog：
   - `catalog.defaultActionSetId + catalog.defaultLayerIds`
3. unknown tracked menu：
   - `MenuBase + UnknownTrackedMenuLayer`
4. passthrough overlay：
   - 不改 base set，不叠加 layer

这个阶段不要做：

- 多菜单 layer merge
- prompt 级 fallback
- glyph device profile 选择

这样做的目的，是先把“谁决定 action set”锁死，再把复杂叠层留给后续 slice。

### 6. 把 `ContextManager` 改成 legacy mirror，而不是继续做菜单 authority

在 `src/input/InputContext.*` 里改 `ContextManager`，固定做法如下：

1. 删除或弃用内部 authority 字段：
   - `_menuStack`
   - `_passthroughMenuCounts`
   - 任何基于 `menuName` 的 push / pop 逻辑
2. 保留 gameplay base facts 更新能力：
   - `DetectGameplayContext()`
   - `UpdateGameplayContext()`
3. 新增一个明确入口，例如：

```cpp
void ApplyResolvedContext(const LegacyContextMirrorState& state);
```

4. `GetCurrentContext()` 和 `GetCurrentEpoch()` 继续保留，继续给：
   - `HidReader.cpp`
   - `InputModalityTracker.cpp`
   - `NativeButtonCommitBackend.cpp`

这一步的关键不是 API 改名，而是 ownership 改变：

- `ContextManager` 以后只发布 mirror
- authoritative 决策已经移到 `ContextResolver`

### 7. 先做 shadow compare，再做 authoritative cutover

本 slice 不允许直接“一把梭切换”，必须先走 shadow compare。

固定做法如下：

1. 保留一份旧 `ContextManager` 推导结果，只用于对比日志，不再给下游消费。
2. 在 `ContextEventSink.cpp` 和主线程刷新点打印：
   - old legacy context / epoch
   - new legacy mirror context / epoch
   - resolver 的 `UiContextId / MenuInstanceId / MenuIdentityQuality / menuStackRevision / presentationPolicyId / contextRevision`
3. 用 Phase 0 replay 资产比对 `Shadow Compare Exit Gate` 里规定的全部场景。
4. 满足 gate 后，再把下游唯一读取源切到新 mirror。
5. cutover 成功后，live compare path 立即降到 test/debug only，并在本 slice 结束前清掉运行时依赖。

### 8. cutover 后立刻清掉本 slice 范围内的旧 authority

切换完成后，本 slice 必须在同一轮里清理这些旧 authority，避免双真相源继续存活：

1. `ContextEventSink::ProcessEvent(MenuOpenCloseEvent)` 不再调用 `ContextManager::OnMenuOpen/OnMenuClose`
2. `ContextManager` 不再对菜单名做 push / pop
3. 新增代码路径中禁止直接调：
   - `MenuContextPolicy::DecideMenuTracking(...)`
   - `ParseInputContextName(...)`
4. 所有新代码路径禁止出现：

```cpp
value_or(InputContext::Menu)
```

这一条必须在 code review 里当 hard failure 处理。

## 验证与观测

### 自动测试

至少新增并跑通下面 5 类测试：

1. `tests/MenuContextPolicyTests.cpp`
   - 继续确保旧配置解析与 unknown-menu 策略行为未被误改。
2. `tests/input_v2/MenuInstanceRegistryTests.cpp`
   - 验证 stable pointer、pointer rebound、same-name duplicate、partial snapshot、`menuStackRevision` 规则。
3. `tests/input_v2/ContextResolverTests.cpp`
   - 验证 top tracked menu 选择、unknown tracked fallback、`presentationPolicyId` publish parity、legacy epoch 规则、`contextRevision` 规则。
4. `tests/input_v2/ActionSetResolverTests.cpp`
   - 验证 gameplay / menu / unknown tracked / passthrough overlay 的 action set 输出。
5. `tests/input_v2/UiMenuObserverTests.cpp`
   - 验证 dirty-only event handling、每 frame 最多一次 publish、禁止查询路径懒采样。

建议的构建与运行入口固定为：

```powershell
xmake build DualPadMenuContextPolicyTests
xmake run DualPadMenuContextPolicyTests
xmake build DualPadContextResolverTests
xmake run DualPadContextResolverTests
```

其中 `DualPadContextResolverTests` 需要在 `xmake.lua` 中新增，包含：

- `tests/input_v2/UiMenuObserverTests.cpp`
- `tests/input_v2/MenuInstanceRegistryTests.cpp`
- `tests/input_v2/ContextResolverTests.cpp`
- `tests/input_v2/ActionSetResolverTests.cpp`
- 必要的 `src/input_v2/context/*.cpp`

### 回放 / 对比验证

必须拿 Phase 0 的回放资产做对比，至少看下面字段：

1. legacy `InputContext`
2. legacy `contextEpoch`
3. canonical `UiContextId`
4. `topMenuInstanceId`
5. `MenuIdentityQuality`
6. `menuStackRevision`
7. `ActionSetStack`
8. `presentationPolicyId`
9. `contextRevision`

### 运行时观测

本 slice 需要新增统一日志前缀，例如：

- `[DualPad][ContextV2][Observer]`
- `[DualPad][ContextV2][Registry]`
- `[DualPad][ContextV2][Resolver]`

必须能从日志直接回答下面问题：

1. 当前 top tracked menu 是谁
2. 它的 `MenuInstanceId` 是多少
3. 它是 `StablePointer`、`FingerprintRebound` 还是 `DegradedIdentity`
4. 当前 canonical `UiContextId` 是什么
5. 当前 `menuStackRevision` / `contextRevision` 是多少
6. 当前 legacy mirror 为什么是这个 `InputContext`

### 手工验证

至少做下面手工验证：

1. 打开 `InventoryMenu`，确认：
   - resolver 命中 `UiContextId::Inventory`
   - legacy mirror 为 `InputContext::InventoryMenu`
2. 打开 `JournalMenu`，确认：
   - resolver 命中 `UiContextId::Journal`
   - legacy mirror 为 `InputContext::JournalMenu`
3. 打开 `FavoritesMenu`，确认：
   - resolver 命中 `UiContextId::Favorites`
   - legacy mirror 为 `InputContext::FavoritesMenu`
4. 打开 HUD / overlay 菜单，确认：
   - observer 能看到
   - resolver 不会把它们提升成 active tracked menu
5. gameplay -> menu -> gameplay 往返，确认：
   - legacy epoch 只在允许的边界推进
   - `menuStackRevision` 只在允许的 publish 边界推进
   - `InputModalityTracker` 没有因为 context 抖动异常切平台

## 退出条件

本 slice 只有在下面条件全部满足时才算完成：

1. `UiMenuObserver`、`MenuInstanceRegistry`、`ContextResolver`、`ActionSetResolver` 已落地。
2. 菜单实例身份已经由 `MenuInstanceId` 驱动，当前运行时不再以菜单名作为实例真相源。
3. `ContextManager` 已退化为 legacy mirror，不再维护 `_menuStack`、`_passthroughMenuCounts` 这类 authority 状态。
4. `menuStackRevision` 已成为正式发布字段，且 authoritative owner 明确为 `MenuInstanceRegistry`。
   - 推进条件只包含 published shape、identity、top tracked menu、tracked / overlay classification 等实际发布语义变化。
   - `lastSeenRevision` 是 bookkeeping / diagnostic 字段，不参与 revision parity。
5. `UiMenuObserver` 已按固定时序工作：事件只标脏、每主线程 frame 最多发布一次、查询路径禁止懒采样。
6. 下游现有调用面仍可继续使用：
   - `ContextManager::GetCurrentContext()`
   - `ContextManager::GetCurrentEpoch()`
7. unknown tracked menu 在内部是显式 sentinel，而不是 silent generic `Menu`。
8. `Partial` / `Unavailable` 观测不会造成伪 close 或错误 top 切换。
9. shadow compare 已满足 `Shadow Compare Exit Gate`，live compare path 已降到 test/debug only。
10. 回放和手工验证已证明：
    - 受支持菜单的 legacy 行为没有回归
    - intentional difference 只出现在文档声明过的 unknown / degraded / passthrough overlay 场景

## 交接给下一 slice 的合同

交给 Phase 3 时，下面这些合同必须已经稳定，下一 slice 不允许重写或绕过：

1. `ContextResolver` 是唯一 canonical 上下文与 `presentationPolicyId` 发布者。
2. `ActionSetResolver` 是唯一 `UiContextId -> ActionSetStack` 发布者。
3. `ActionSetStack` 的结构已经固定为：
   - `baseSetId`
   - `activeLayerIds`
   - `scopeAnchorIds`
4. `ContextManager` 只保留 legacy mirror API，不再恢复任何 runtime authority。
5. `InputModalityTracker` 在 Phase 3 只能消费：
   - legacy mirror
   - 或新的 published `ResolvedContextSnapshot`
   不能自己再去读 `RE::UI` 或 `MenuContextPolicy`。
6. `ScaleformGlyphBridge` 在 Phase 3 之前继续吃 legacy `InputContext`，但不得新增新的 context 推导逻辑。
7. canonical 输出里必须保留下面字段，供 Phase 3 和后续 Phase 7 直接消费：
   - `UiContextId`
   - `topMenuInstanceId`
   - `MenuIdentityQuality`
   - `menuStackRevision`
   - `ActionSetStack`
   - `presentationPolicyId`
   - `contextRevision`
8. `menuStackRevision` 只能来自 `MenuInstanceRegistry`；Phase 3 以后可以转发它，但不得自行合成。
9. `presentationPolicyId` 只能来自 Phase 2 已发布的 `ResolvedContextSnapshot`；Phase 3 以后允许转发和消费，但不得再通过 `ContextCatalog`、`UiContextId` 或本地 policy 表反查。
10. `UiMenuObserver` 的采样时序已经固定；Phase 3 不得再改成查询路径懒采样。
11. legacy 兼容 contract 继续保持：
    - `GetCurrentContext()`
    - `GetCurrentEpoch()`
    直到 Phase 6/Phase 8 的 cutover / cleanup 再统一收口。
