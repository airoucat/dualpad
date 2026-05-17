# Phase 6 Slice：PromptProjection / PromptService / ScaleformPromptAdapter

> 本文对应重构阶段里的 Phase 6。
> 这里的 Phase 6 是重构阶段编号，不等于 `.dualpad-builder/` 里的 `DP4` 或 `DP5` 工作包。

## 目标

- 把当前 `ScaleformGlyphBridge -> ParseInputContextName(...).value_or(InputContext::Menu) -> BindingManager::GetTriggerForAction(...) -> TriggerToButtonArtToken(...)` 这条 glyph 反查链，切换成 `PromptProjection -> PromptService -> ScaleformPromptAdapter`。
- 让 prompt / glyph 运行时只消费已经冻结的：
  - `CompiledContextCatalog`
  - `CompiledActionManifest` 内的 `compiled display bindings`
  - `PublishedPresentationState.family`
  - `PublishedPresentationState.uiContext`
  - `PublishedPresentationState.actionSets`
  - `PublishedPresentationState.epoch`
- 现在就冻结 3 个模块的职责边界：
  - `PromptProjection`
    - 唯一负责把当前 presentation truth 收口成可查询的 prompt scope。
  - `PromptService`
    - 唯一负责把 `actionId + scope + compiled display bindings` 解析成 `PromptDescriptor`。
  - `ScaleformPromptAdapter`
    - 唯一负责 Scaleform delegate 注册、参数解码、兼容旧 API、编码返回对象。
- 现在就冻结旧 glyph API 的兼容语义：
  - `DualPad_GetActionGlyphToken(actionId, contextName)` 继续保留。
  - `DualPad_GetActionGlyph(actionId, contextName)` 继续保留。
  - 两者都只允许做 `PromptService` 的 wrapper，不再自己做 context fallback、trigger reverse lookup 或 token 猜测。
- 现在就冻结 invalid context 的 fail-closed 行为：
  - 不再允许 `value_or(InputContext::Menu)`。
  - 不再允许“context 无效时偷偷改查当前菜单”。
  - 不再允许“specific context miss 以后再手写 fallback 到 generic Menu”。

本 slice 明确不做下面这些事：

- 不重写 `compiled display bindings` 的格式；这已经在 `02_slice_phase1_catalog_and_manifest_compiler_zh.md` 冻结。
- 不重新设计 `Action Graph`、`InteractionEngine` 或 `GameplayProjection`；这里只消费它们已经冻结的合同。
- 不恢复 `FavoritesMenu` 缺失的页面级 SWF workspace，也不把页面级 broker 逻辑混进 prompt 查询层。
- 不把 prompt 选择逻辑下放到 SWF、`BindingManager`、`ScaleformGlyphBridge` 或新的 runtime helper。

## 冻结的设计决定

### 1. 落地顺序固定

Phase 6 的实施顺序现在就定死为：

1. 先定义 `PromptProjection` 的 published scope 合同。
2. 再定义 `PromptService` 的 query / candidate / descriptor 合同。
3. 再实现 `ScaleformPromptAdapter`，同时保留旧 delegate 名字。
4. 然后把 `ScaleformGlyphBridge` 退化成 facade / forwarding shim。
5. 最后切掉 glyph 运行时对 `BindingManager`、`Trigger`、`ParseInputContextName(...).value_or(Menu)` 的依赖。

禁止反向顺序：

- 不允许先改 `ScaleformGlyphBridge` 的返回语义，再临场补 `PromptService` 规则。
- 不允许先在 adapter 里做 if/else fallback，再说后面再抽 `PromptProjection`。
- 不允许保留“specific context miss -> generic Menu retry”这类旧逻辑当长期过渡。

### 2. `PromptProjection` 的 authoritative 输入与输出固定

`PromptProjection` 的唯一 authoritative 输入固定为：

- `PublishedPresentationState.family`
- `PublishedPresentationState.uiContext`
- `PublishedPresentationState.actionSets`
- `PublishedPresentationState.epoch`
- active bundle 的 `manifestEpoch`

它不允许直接读取：

- `BindingManager`
- `InputContextNames`
- `ScaleformGlyphBridge`
- `RE::UI`
- `GameplayProjection`
- 任何 `Trigger` 或 `TriggerType`

建议固定到 `src/input_v2/projections/PromptProjection.h`：

```cpp
enum class PromptScopeState : std::uint8_t {
    Ready,
    Unavailable
};

struct PublishedPromptScope {
    PromptScopeState state;
    DeviceFamily family;
    UiContextId uiContext;
    ActionSetStack actionSets;
    std::uint32_t promptScopeRevision;
    std::uint32_t manifestEpoch;
};
```

冻结规则如下：

1. `promptScopeRevision` 固定从 `PublishedPresentationState.epoch` 派生；如果只有 `manifestEpoch` 变化，也必须额外推进。
2. `PromptProjection` 不回头读取 `ContextResolver` 的内部对象；它只吃 Phase 3 已经发布出来的 presentation truth。
3. `family / uiContext / actionSets` 变化时，跟随新的 presentation epoch 发布新的 `PublishedPromptScope`。
4. 只有 `manifestEpoch` 变化时，也必须重新发布 `PublishedPromptScope`，不能继续沿用旧 prompt scope。
5. 如果 presentation truth 当前不可用，`state` 必须显式变成 `Unavailable`，后续查询统一 fail-closed。
6. `PromptProjection` 不决定 token、label、候选优先级；它只发布 scope，不做 prompt 解析。

### 3. `PromptService` 的 query 合同固定

`PromptService` 是唯一允许解析 prompt 的模块。建议固定到 `src/input_v2/prompt/PromptService.h` 与 `src/input_v2/prompt/PromptTypes.h`。

查询入口固定为两种 selector：

```cpp
enum class PromptScopeSelectorKind : std::uint8_t {
    CurrentPublished,
    ExplicitContextName
};

struct PromptQuery {
    std::string_view actionId;
    PromptScopeSelectorKind selectorKind;
    std::string_view contextName;
};
```

返回合同固定为：

```cpp
enum class PromptQueryStatus : std::uint8_t {
    Ok,
    ScopeUnavailable,
    UnknownAction,
    UnknownContext,
    ContextOutOfScope,
    NoVisibleBinding,
    HiddenOnly,
    DeviceFamilyMismatch
};

enum class PromptResolutionSource : std::uint8_t {
    ExactScope,
    AncestorScope
};

struct PromptCandidate {
    std::uint32_t bindingId;
    std::string source;
    std::string token;
    std::string localizedLabel;
    std::string deviceProfile;
    std::uint16_t priority;
};

enum class PromptFallbackKind : std::uint8_t {
    None,
    AncestorScope
};

struct PromptDescriptor {
    bool ok;
    PromptQueryStatus status;
    std::optional<ActionId> action;
    std::optional<ActionSetId> resolvedSet;
    std::optional<UiContextId> resolvedContext;
    std::optional<PromptCandidate> primary;
    std::vector<PromptCandidate> alternates;
    PromptResolutionSource resolutionSource;
    PromptFallbackKind fallback;
    std::optional<std::string> deviceProfile;
    std::uint32_t promptScopeRevision;
    std::uint32_t manifestEpoch;
};
```

补充冻结规则如下：

- `PromptCandidate.source`
  - 固定表示候选项来自哪一条 compiled display binding / generated binding provenance
  - `Phase 8` 的 snapshot、docgen 和 explain 日志统一使用 `source` 命名，不再出现候选级 `origin`
- `PromptDescriptor.resolutionSource`
  - 固定表示本次 query 的 `requestedScopeAnchorIds` 与当前 `PublishedPromptScope.actionSets.scopeAnchorIds` 如何命中
  - `ExactScope`
    - `requestedScopeAnchorIds` 与当前 published `scopeAnchorIds` 长度完全一致，且每个下标的 anchor 都完全相等
  - `AncestorScope`
    - `requestedScopeAnchorIds` 是当前 published `scopeAnchorIds` 的严格前缀；也就是 `requested.size() < current.size()`，且对所有 `0 <= i < requested.size()` 都满足 `requested[i] == current[i]`
  - 它不是 candidate source；`Phase 8` 禁止把它重命名回 `origin`
- `PromptDescriptor.fallback`
  - 只允许由 `PromptService` 赋值
  - `None`
    - `primary` 来自本次 query 命中的最具体 scope anchor；没有向更早的 anchor 回退
  - `AncestorScope`
    - `primary` 不是从命中的最具体 scope anchor 直接命中，而是沿同一条 `requestedScopeAnchorIds` 链向前回退到更早的 ancestor anchor 后才命中
  - 这里的 `fallback` 只描述同一条 `scopeAnchorIds` 链内的 ancestor anchor 回退，不代表重新引入 `value_or(Menu)` 或 runtime if/else generic `Menu` retry
- `PromptDescriptor.deviceProfile`
  - 固定是 `primary.deviceProfile` 的顶层镜像；若没有 `primary`，则为空
  - snapshot / CI / generated docs 只读取这个顶层字段，不自行从 `alternates` 或外部状态逆推
- `PromptDescriptor` 不暴露顶层 `contextRevision`
  - prompt 路径的 authoritative revision 只有 `promptScopeRevision`
  - `Phase 8` 不得自行发明顶层 `contextRevision`；若需要定位 scope，只能通过 `PromptSnapshotRecord` 中的 `promptScopeRevision + resolvedContext` 组合解释
- `PromptSnapshotRecord`
  - 是 `PromptDescriptor` 的唯一派生投影，用于 `Phase 8` 的 prompt snapshot、black-box contract、docgen 和默认 CI
  - 它不是第二套 runtime truth；不得在 `Phase 8` 或脚本层再另起一套 prompt 结构
  - 字段预算固定为：
    - `actionId`
      - 固定从 `PromptQuery.actionId` 原样派生
      - 若 `PromptDescriptor.action` 有值，其字符串化结果必须与 `actionId` 一致；不一致视为合同破裂
    - `status`
      - 固定从 `PromptDescriptor.status` 派生
      - `Phase 8` 的 black-box / snapshot / CI 必须直接比较它，不能再从空 token、空候选或旧 API 的 `failureReason` 反推失败原因
    - `resolvedSet`
      - 固定从 `PromptDescriptor.resolvedSet` 派生
    - `resolvedContext`
      - 固定从 `PromptDescriptor.resolvedContext` 派生
    - `primary`
      - 固定从 `PromptDescriptor.primary` 派生
    - `alternates`
      - 固定从 `PromptDescriptor.alternates` 派生
    - `resolutionSource`
      - 固定从 `PromptDescriptor.resolutionSource` 派生
    - `fallback`
      - 固定从 `PromptDescriptor.fallback` 派生
    - `deviceProfile`
      - 固定从 `PromptDescriptor.deviceProfile` 派生
    - `promptScopeRevision`
      - 固定从 `PromptDescriptor.promptScopeRevision` 派生
    - `manifestEpoch`
      - 固定从 `PromptDescriptor.manifestEpoch` 派生
  - `PromptSnapshotRecord` 不单独存顶层 `ok`
    - `ok` 固定只保留在 `PromptDescriptor` 与旧 API wrapper 中
    - snapshot / black-box / CI 一律按 `status == PromptQueryStatus::Ok` 推导成功态，避免顶层双真相
  - 除上述 11 个字段外，`Phase 8` 禁止再引入顶层 `origin`、`contextRevision`、第二套 `deviceProfile` 或独立的 prompt runtime truth

`PromptService` 的解析顺序固定如下，后续实现不得改顺序：

1. 先读取当前 `PublishedPromptScope`。
   - 若 `state != Ready`，直接返回 `ScopeUnavailable`。
2. 再用 `CompiledActionManifest` 解析 `actionId`。
   - action 不存在时，直接返回 `UnknownAction`。
3. 再解析 selector。
   - `CurrentPublished`
     - `requestedScopeAnchorIds` 固定等于当前 `PublishedPromptScope.actionSets.scopeAnchorIds`。
     - `resolutionSource` 固定为 `ExactScope`。
   - `ExplicitContextName`
     - 先用 `CompiledContextCatalog` 解析 `contextName`。
     - 字符串无法解析时，直接返回 `UnknownContext`。
     - 解析成功后，先生成 `requestedScopeAnchorIds`：
       - 默认规则：直接使用该 context 在 catalog 中冻结好的 `scopeAnchorIds`
       - 唯一允许的 ancestor alias：`contextName == "Menu"` 时，不读 `UnknownTrackedMenuLayer`，而是固定把 `requestedScopeAnchorIds` 归一化为 `[MenuBase]`
     - `ExactScope` 的判定固定为：
       - `requestedScopeAnchorIds.size() == currentScopeAnchorIds.size()`
       - 且对所有 `i` 都满足 `requestedScopeAnchorIds[i] == currentScopeAnchorIds[i]`
     - `AncestorScope` 的判定固定为：
       - `requestedScopeAnchorIds.size() < currentScopeAnchorIds.size()`
       - 且对所有 `i` 都满足 `requestedScopeAnchorIds[i] == currentScopeAnchorIds[i]`
     - 若既不是 `ExactScope` 也不是 `AncestorScope`，直接返回 `ContextOutOfScope`
     - 命中 `ExactScope` 时，`resolutionSource = ExactScope`
     - 命中 `AncestorScope` 时，`resolutionSource = AncestorScope`
4. 再按 `requestedScopeAnchorIds` 的固定顺序枚举候选。
   - 枚举顺序固定为“从 `requestedScopeAnchorIds` 的最后一个 anchor 开始，依次向前到第一个 anchor”，也就是“先最具体，后最通用”
   - 第一层产生可显示候选的 anchor 记为 `matchedAnchor`
   - 若 `matchedAnchor` 恰好是 `requestedScopeAnchorIds` 的最后一个 anchor，则 `fallback = None`
   - 若 `matchedAnchor` 早于最后一个 anchor，则 `fallback = AncestorScope`
5. 再按 `DeviceFamily` 过滤 `compiled display bindings`。
   - 若 matched scope chain 上至少命中过 1 条 compiled display binding，但全部仅因 `DeviceFamily` 不匹配而被过滤，则固定返回 `DeviceFamilyMismatch`。
   - 这一步一旦命中 `DeviceFamilyMismatch`，不得再继续降级成 `HiddenOnly` 或 `NoVisibleBinding`。
6. 再按“可显示”过滤。
   - 至少存在 1 条 family-compatible 候选，且这些候选全部是 `Hidden`，返回 `HiddenOnly`。
   - 既没有命中 `DeviceFamilyMismatch`，也没有命中 `HiddenOnly`，且最终没有任何可显示候选时，返回 `NoVisibleBinding`。
7. 最后做 deterministic 选主候选。
   - 先按 `priority` 降序。
   - 再按 `bindingId` 升序做稳定 tie-break。
   - `primary` 以外的按同一顺序进入 `alternates`。

这里的关键冻结点是：

- generic `Menu` fallback 不再由 runtime 手写 `if (context != Menu) retry Menu`。
- specific menu prompt 覆盖 generic menu prompt 的能力，完全由 `scopeAnchorIds` 的前缀命中规则与 display binding priority 表达。
- `PromptService` 不再回头看 `Trigger`，也不再把 `Axis / Gesture` 临时猜成 button token。

### 4. 旧 glyph API 的兼容策略现在就定清

兼容策略固定为“保留 delegate 名字，替换内部真相源”。

#### `DualPad_GetActionGlyphToken`

- 输入：
  - `actionId`
  - `contextName`
- 实现：
  - 固定包装成 `PromptQuery{ actionId, ExplicitContextName, contextName }`
  - 调用 `PromptService`
  - 如果 `descriptor.ok && descriptor.primary.has_value()`，返回 `primary.token`
  - 否则返回空字符串 `""`
- 兼容口径：
  - 不抛异常
  - 不返回 `null`
  - 不再 fallback 到 `Menu`

#### `DualPad_GetActionGlyph`

- 输入：
  - `actionId`
  - `contextName`
- 输出对象必须继续保留现有字段：
  - `ok`
  - `buttonArtToken`
  - `semanticId`
  - `contextName`
- 允许新增但不得删改的诊断字段：
  - `failureReason`
  - `resolvedContextId`
  - `resolvedActionSetId`
  - `resolutionSource`
  - `fallback`
  - `deviceProfile`
  - `manifestEpoch`
  - `promptScopeRevision`
- 返回规则固定为：
  - `descriptor.ok == true`
    - `ok = true`
    - `buttonArtToken = primary.token`
  - `descriptor.ok == false`
    - `ok = false`
    - `buttonArtToken = ""`
    - `failureReason = status`

#### 新增的 richer API

本 slice 直接冻结两个新 delegate：

- `DualPad_GetActionGlyphCandidates`
- `DualPad_GetPromptDescriptor`

它们都只允许返回 `PromptService` 的原生结构化结果，不得再走旧 object shape 的压缩路径。

### 5. `ScaleformPromptAdapter` 与 `ScaleformGlyphBridge` 的边界固定

`ScaleformPromptAdapter` 是新的真正 delegate handler，建议固定到：

- `src/input_v2/adapters/ScaleformPromptAdapter.h`
- `src/input_v2/adapters/ScaleformPromptAdapter.cpp`

它的职责固定为：

- 注册旧 delegate 名字与新 delegate 名字
- 从 `FxDelegateArgs` 解码参数
- 调用 `PromptService`
- 按旧 API 或新 API 的 shape 编码返回值
- 记录 prompt 查询日志

它不允许做：

- context fallback
- action set 选择策略重写
- `BindingManager` reverse lookup
- token 翻译猜测

`ScaleformGlyphBridge` 在 Phase 6 之后固定退化为 compatibility facade：

- 继续保留：
  - `RegisterInitialMenus()`
  - `OnMenuOpened(...)`
  - `Accept(...)`
- 但内部只允许 forward 到 `ScaleformPromptAdapter`
- 下列逻辑必须退出 runtime path：
  - `ResolveActionToken(...)`
  - `TriggerToButtonArtToken(...)`

### 6. invalid context 的 fail-closed 行为固定

Phase 6 之后，prompt 路径上所有 context 解析失败都必须显式暴露，不允许 silent fallback。

冻结为下面 3 类：

1. `UnknownContext`
   - `contextName` 不在 `CompiledContextCatalog` 里。
   - 旧 API：
     - token API 返回 `""`
     - descriptor API 返回 `ok=false`
2. `ContextOutOfScope`
   - `contextName` 是合法 canonical context，但它归一化后的 `requestedScopeAnchorIds` 既不是当前 `PublishedPromptScope.actionSets.scopeAnchorIds` 的完全相等链，也不是严格前缀链。
   - 典型例子：
     - 当前在 `JournalMenu`，却拿 `InventoryMenu` 去查。
   - 旧 API：
     - token API 返回 `""`
     - descriptor API 返回 `ok=false`
3. `ScopeUnavailable`
   - presentation 还没发布出 ready scope，或者 active bundle 不可用。
   - 旧 API：
     - token API 返回 `""`
     - descriptor API 返回 `ok=false`

### 6A. prompt 失败状态判定表现在冻结

`PromptService` 的失败状态不再允许由实现者临场解释；判定顺序与优先级固定如下：

1. `ScopeUnavailable`
   - `PublishedPromptScope.state != Ready`，或者 active bundle / action set scope 尚未发布完成。
   - 这是唯一允许在 action / context 解析前直接返回的失败态。
2. `UnknownAction`
   - `PromptQuery.actionId` 不在 compiled action manifest 中。
3. `UnknownContext`
   - `ExplicitContextName` 无法解析成 catalog 中存在的 canonical context。
4. `ContextOutOfScope`
   - canonical context 存在，但归一化后的 `requestedScopeAnchorIds` 既不是当前 published `scopeAnchorIds` 的完全相等链，也不是严格前缀链。
   - canonical context 没有任何 `scopeAnchorIds`，或归一化结果为空链（empty-scope / no-scope context），也一律按 `ContextOutOfScope` fail-closed；不得把空链偷换成当前 scope、generic `Menu` 或任意默认 anchor。
5. `DeviceFamilyMismatch`
   - action 与 scope 已经命中，且在 visibility 过滤前至少存在 1 条 compiled display binding，但全部仅因 `DeviceFamily` 不匹配而被过滤。
   - 该状态优先于 `HiddenOnly` / `NoVisibleBinding`，因为此时失败原因已经被唯一锁定为 family mismatch。
6. `HiddenOnly`
   - 至少存在 1 条 family-compatible 候选，但这些候选全部被标记为 `Hidden`。
   - 该状态只在 `DeviceFamilyMismatch` 未命中时允许出现。
7. `NoVisibleBinding`
   - action / scope / family 全部已通过，但最终没有任何可显示候选，且不满足 `DeviceFamilyMismatch` 或 `HiddenOnly`。
   - 典型情形包括：matched scope chain 上根本没有 compiled display binding，或者只有不参与显示的候选。

旧 API 返回矩阵固定如下：

- `status == Ok`
  - token API 返回 `primary.token`
  - descriptor API 返回 `ok=true`
- `status != Ok`
  - token API 返回 `""`
  - descriptor API 返回 `ok=false`
  - `failureReason` 固定等于 `status`

这里要特别冻结两条禁止规则：

- 禁止 `ParseInputContextName(contextName).value_or(InputContext::Menu)`。
- 禁止“invalid context 时改用当前 published uiContext 继续查”。

## 前置依赖

开始 Phase 6 之前，下面这些前置合同必须已经落地；缺任何一项，都先回对应 slice 补齐，不要在本阶段临时发明替代方案：

1. `docs/plans/dualpad_rearchitecture/02_slice_phase1_catalog_and_manifest_compiler_zh.md`
   - 已经稳定提供：
     - `CompiledContextCatalog`
     - `CompiledActionManifest`
     - `compiled display bindings`
     - `manifestEpoch`
   - 并且 `ambiguous display binding -> compile fail` 已经成立。
2. `docs/plans/dualpad_rearchitecture/03_slice_phase2_menu_instance_truth_zh.md`
   - 已经稳定提供：
     - `UiContextId`
     - `ActionSetStack`
     - `legacyInputContext`
     - `legacyContextEpoch`
   - 且“除兼容层外不再自己做 `value_or(Menu)`”已经成立。
3. `docs/plans/dualpad_rearchitecture/04_slice_phase3_presentation_split_zh.md`
   - 已经稳定提供：
     - `PublishedPresentationState.family`
     - `PublishedPresentationState.uiContext`
     - `PublishedPresentationState.actionSets`
     - `PublishedPresentationState.epoch`
   - 且 Phase 3 的合同 D 已经固定：Phase 6 只能消费这些 presentation 字段。
4. `docs/plans/dualpad_rearchitecture/05_slice_phase4_action_graph_and_interaction_engine_zh.md`
   - 已经稳定提供：
     - `DisplayBinding`
     - `CompiledActionGraph`
   - 且 glyph / prompt 路径不再依赖 `BindingManager::GetTriggerForAction(...)` 作为新真相源。
5. `docs/plans/dualpad_rearchitecture/06_slice_phase5_gameplay_projection_zh.md`
   - 已经把 gameplay owner / gate / helper side effect 从 prompt 解析路径上剥离。
   - Prompt 运行时不需要也不允许读取 gameplay projection 的内部决策对象。

如果上面任何一项未完成，本 slice 不得临时补第二套 prompt truth。

## 涉及代码与文档

### 当前代码入口

- `src/input/glyph/ScaleformGlyphBridge.h`
- `src/input/glyph/ScaleformGlyphBridge.cpp`
- `src/input/InputContextNames.h`
- `src/input/InputContextNames.cpp`
- `src/input/BindingManager.h`
- `src/input/BindingManager.cpp`
- `src/main.cpp`
- `src/input/ContextEventSink.cpp`

### 本 slice 新增代码

- `src/input_v2/prompt/PromptTypes.h`
- `src/input_v2/prompt/PromptTypes.cpp`
- `src/input_v2/projections/PromptProjection.h`
- `src/input_v2/projections/PromptProjection.cpp`
- `src/input_v2/prompt/PromptService.h`
- `src/input_v2/prompt/PromptService.cpp`
- `src/input_v2/adapters/ScaleformPromptAdapter.h`
- `src/input_v2/adapters/ScaleformPromptAdapter.cpp`

### 本 slice 需要修改的现有代码

- `src/input/glyph/ScaleformGlyphBridge.h`
- `src/input/glyph/ScaleformGlyphBridge.cpp`
- `src/main.cpp`
- `src/input/ContextEventSink.cpp`
- `xmake.lua`

### 本 slice 新增测试

- `tests/PromptProjectionTests.cpp`
- `tests/PromptServiceTests.cpp`
- `tests/ScaleformPromptAdapterTests.cpp`
- `tests/replay/golden/phase6_prompt/`

测试 target 不再新建第三套 prompt 专用可执行文件，固定继续扩到 `DualPadInputV2Tests`。

### 必须对照的文档

- `docs/plans/dualpad_rearchitecture/README_zh.md`
- `docs/plans/dualpad_rearchitecture/02_slice_phase1_catalog_and_manifest_compiler_zh.md`
- `docs/plans/dualpad_rearchitecture/03_slice_phase2_menu_instance_truth_zh.md`
- `docs/plans/dualpad_rearchitecture/04_slice_phase3_presentation_split_zh.md`
- `docs/plans/dualpad_rearchitecture/05_slice_phase4_action_graph_and_interaction_engine_zh.md`
- `docs/main_menu_glyph_current_status_zh.md`

如需历史背景，再回看 `docs/plans/dualpad_rearchitecture/dualpad_rearchitecture_plan_zh.md`。

## 实施步骤

### 1. 先落 `PromptTypes`，冻结 query / descriptor / status

- 在 `src/input_v2/prompt/PromptTypes.*` 里一次性定义：
  - `PromptScopeState`
  - `PublishedPromptScope`
  - `PromptScopeSelectorKind`
  - `PromptQuery`
  - `PromptQueryStatus`
  - `PromptResolutionSource`
  - `PromptCandidate`
  - `PromptDescriptor`
  - `PromptSnapshotRecord`
- 这一阶段只写类型、`ToString(...)`、debug dump、序列化辅助，不接 runtime。
- 此步禁止接触：
  - `BindingManager`
  - `ScaleformGlyphBridge`
  - `RE::UI`

产物标准：

- 后续模块不需要再临时发明 `UnknownContext`、`ContextOutOfScope` 之类的错误码名字。
- 旧 API 和新 API 的返回 shape 已经被这些类型覆盖。

### 2. 实现 `PromptProjection`，只发布 scope，不做解析

- 在 `src/input_v2/projections/PromptProjection.*` 中实现：
  - `PublishedPromptScope BuildPromptScope(const PublishedPresentationState&, std::uint32_t manifestEpoch);`
  - `const PublishedPromptScope& GetPublishedPromptScope() const;`
- 固定输入只有：
  - `PublishedPresentationState`
  - active bundle 的 `manifestEpoch`
- 固定发布规则：
  - `family / uiContext / actionSets` 变化时，跟随新的 presentation epoch 推进 `promptScopeRevision`
  - 仅 `manifestEpoch` 变化时，也必须推进 `promptScopeRevision`
  - presentation 未 ready 时发布 `Unavailable`
- 此步不允许开始查 action、查 display binding、翻译 token。

### 3. 实现 `PromptService`，把 prompt 解析规则完全收口

- 在 `src/input_v2/prompt/PromptService.*` 中按下面顺序实现，不得跳步：
  1. `ResolveCurrentScope()`
  2. `ResolveAction(...)`
  3. `ResolveRequestedContext(...)`
  4. `EnumerateVisibleCandidates(...)`
  5. `BuildPromptDescriptor(...)`
- 先做 `CurrentPublished`，再做 `ExplicitContextName`。
- `ExplicitContextName` 的校验规则现在就定死：
  - context 字符串解析失败 -> `UnknownContext`
  - context 能解析，但归一化后的 `requestedScopeAnchorIds` 不是当前 `scopeAnchorIds` 的完全相等链或严格前缀链 -> `ContextOutOfScope`
  - context 是 `Menu` 这类 generic 祖先 scope 时，固定只按 `[MenuBase]` 与当前 `scopeAnchorIds` 做前缀比较
- deterministic 候选排序不能后补：
  - `priority` 降序
  - `bindingId` 升序

此步必须把下面这些旧 runtime 分支收口进 service，而不是留给 adapter 临场处理：

- exact set 与 parent set 的优先关系
- visible 与 hidden 的判定
- family mismatch 的失败语义
- `primary` 与 `alternates` 的排序

### 4. 实现 `ScaleformPromptAdapter`，保留旧名字，新增 rich delegate

- 在 `src/input_v2/adapters/ScaleformPromptAdapter.*` 中实现：
  - `RegisterInitialMenus()`
  - `OnMenuOpened(...)`
  - `Accept(...)`
  - 4 个 handler：
    - `DualPad_GetActionGlyphToken`
    - `DualPad_GetActionGlyph`
    - `DualPad_GetActionGlyphCandidates`
    - `DualPad_GetPromptDescriptor`
- handler 固定流程：
  1. 解码参数
  2. 构造 `PromptQuery`
  3. 调 `PromptService`
  4. 按旧 API 或新 API 编码返回值
  5. 打统一 explain 日志

此步禁止：

- 在 adapter 里自己解析 action set stack
- 在 adapter 里自己做 context fallback
- 在 adapter 里访问 `BindingManager`

### 5. 先做 shadow compare，再做 authoritative cutover

切换顺序现在就定死：

1. 先把 `ScaleformPromptAdapter` 注册进现有 menu delegate。
2. 先保留旧 `ScaleformGlyphBridge` 结果作为对照值。
3. 同一次 query 内并行算：
   - old path
   - new `PromptService` path
4. 只记录 diff，不先切返回值。
5. 影子对照稳定后，再切到新 path 返回值。
6. 切完以后，旧 path 只允许留在测试或临时 parity helper 中，不再进入 runtime 主路径。

必须记录的 diff 字段：

- `actionId`
- `requestedContext`
- `oldToken`
- `newToken`
- `status`
- `resolvedContext`
- `resolvedSet`
- `manifestEpoch`
- `promptScopeRevision`

### 6. 把 `ScaleformGlyphBridge` 退化成 facade，并删掉 reverse lookup 主路径

- `src/input/glyph/ScaleformGlyphBridge.*` 在 Phase 6 结束前固定变成 facade：
  - `RegisterInitialMenus()` -> forward
  - `OnMenuOpened(...)` -> forward
  - `Accept(...)` -> forward
- 下面两段逻辑必须退出 runtime path：
  - `ResolveActionToken(...)`
  - `TriggerToButtonArtToken(...)`
- `ParseInputContextName(...).value_or(InputContext::Menu)` 必须从 glyph / prompt 路径删掉。

这一步的目标不是“老文件名必须消失”，而是“老文件名即使还在，也不再承载 prompt 真相”。

## 验证与观测

### 构建与自动化测试

必须执行的命令：

```powershell
xmake build DualPadInputV2Tests
xmake run DualPadInputV2Tests
xmake build DualPad
```

`DualPadInputV2Tests` 必须至少覆盖：

- `PromptProjectionTests`
  - presentation ready -> `PublishedPromptScope::Ready`
  - presentation unavailable -> `PublishedPromptScope::Unavailable`
  - `family / uiContext / actionSets` 变化会推进 `promptScopeRevision`
  - 仅 `manifestEpoch` 变化也会推进 `promptScopeRevision`
- `PromptServiceTests`
  - exact context 命中
  - generic `Menu` ancestor scope 命中
  - invalid context -> `UnknownContext`
  - valid but inactive context -> `ContextOutOfScope`
  - hidden-only -> `HiddenOnly`
  - no visible binding -> `NoVisibleBinding`
  - unknown action -> `UnknownAction`
  - 当前 scope 与请求 scope 完全相等时 `resolutionSource = ExactScope`
  - 请求 scope 是当前 scope 严格前缀时 `resolutionSource = AncestorScope`
  - 候选从更早的 scope anchor 回退命中时 `fallback = AncestorScope`
  - `deviceProfile` 顶层镜像与 `primary.deviceProfile` 一致
  - priority tie-break deterministic
- `ScaleformPromptAdapterTests`
  - `DualPad_GetActionGlyphToken` 成功时返回主 token
  - `DualPad_GetActionGlyphToken` 失败时返回空字符串
  - `DualPad_GetActionGlyph` 失败时 `ok=false` 且 `buttonArtToken=""`
  - `DualPad_GetActionGlyph` 会附带 `failureReason`
  - rich API 返回完整 candidate 列表
- repo-owned `Interface/startmenu.swf` smoke
  - 旧 `DualPad_GetActionGlyphToken` 仍能被现有 SWF 调用并消费单 token string
  - 旧 `DualPad_GetActionGlyph` 仍能被现有 SWF 调用并消费兼容 descriptor
  - 旧 SWF 不依赖新增 rich prompt 字段，也不会因为失败态字段扩展破坏现有显示路径

### replay / shadow compare

必须用 Phase 0 的 replay barrier 或等价 shadow compare 覆盖至少这些场景：

- `tests/replay/golden/phase0/03_main_menu_glyph`
- `tests/replay/golden/phase0/04_journal_confirm_cancel`
- `tests/replay/golden/phase0/05_map_cursor_zoom_open_journal`
- `tests/replay/golden/phase0/07_book_page_lr`
- generic `Menu` ancestor scope 命中
- invalid context fail-closed

此处不要求恢复 `FavoritesMenu` 页面级 SWF workspace；如果只需要验证 catalog / manifest / prompt candidate，本 slice 允许用自动化测试覆盖，不把缺失 workspace 假装成已恢复。

若本 slice 增加 prompt 专用 replay 资产，固定落在 `tests/replay/golden/phase6_prompt/<scenario>/`，并与 `tests/replay/golden/phase0/<scenario>/` 复用同名 scenario。

### Shadow Compare Exit Gate

- shadow compare 不是长期缓冲层，必须在本 slice 内完成收口；不得把 live parity path 带到 `Phase 7`。
- authoritative cutover 之前，下面这些场景必须全部跑过 shadow compare：
  - Main Menu
  - Journal
  - Map
  - Book
  - generic `Menu` ancestor scope
  - invalid context fail-closed
- 只允许以下 intentional difference：
  - invalid `contextName` 不再落到旧 `Menu` fallback，而是稳定返回 `UnknownContext` / `ok=false` / `buttonArtToken=""`
  - valid 但 inactive 的 context 不再偷偷命中 generic `Menu`，而是稳定返回 `ContextOutOfScope`
  - candidate 顺序从旧 runtime 的隐式顺序收紧为 `priority` 降序、`bindingId` 升序
- 以下 diff 一律阻断 cutover：
  - 同一 `actionId + requestedContext + family` 下主 token 无解释漂移
  - `resolvedContext` / `resolvedSet` 与当前 `PublishedPromptScope` 不一致
  - `promptScopeRevision` 或 `manifestEpoch` 回退、缺失或不推进
  - explain 日志仍依赖 `BindingManager`、`Trigger` 或 `value_or(Menu)` 才能解释结果
- cutover PR 必须同时完成：
  - 旧 path 从 live return path 退出
  - parity helper 只保留在 test / debug trace
  - intentional difference 文档化并纳入测试断言

## Rollback Boundary / Emergency Revert

- 本 slice 的落地边界固定分两步：
  1. `PromptProjection / PromptService / ScaleformPromptAdapter` 落地，并启用 shadow compare，但 runtime 返回值仍以旧 path 为准。
  2. authoritative cutover，把 live return path 切到 `PromptService`，同时把旧 path 降为 test / debug-only parity helper。
- 若第 1 步失败，允许直接回退新增模块接线，但不得为了解围把 `value_or(Menu)` 或 `BindingManager` 逻辑继续塞回新模块。
- 若第 2 步 live smoke 失败，允许整块回退第 2 步提交，让 runtime 暂时回到旧返回路径；禁止保留“新旧路径同时 live 返回”的混合状态。
- rollback 只允许回到上一个明确 checkpoint；不允许新增长期 runtime 开关，也不允许把 parity helper 重新提升为正式 authority。
- `Phase 7` 开始前，`PromptProjection / PromptService` 的合同必须已经稳定；若仍依赖 emergency revert 才能运行，说明 `Phase 6` 未完成，必须回本 slice 返工。

### explain 日志

每次 prompt 查询至少要输出：

- `actionId`
- `selectorKind`
- `requestedContext`
- `publishedUiContext`
- `resolvedContext`
- `resolvedSet`
- `family`
- `status`
- `resolutionSource`
- `fallback`
- `deviceProfile`
- `primaryToken`
- `candidateCount`
- `manifestEpoch`
- `promptScopeRevision`

### 失败观测

必须单独统计并可搜索下面这些失败原因：

- `UnknownContext`
- `ContextOutOfScope`
- `UnknownAction`
- `ScopeUnavailable`
- `NoVisibleBinding`
- `HiddenOnly`
- `DeviceFamilyMismatch`

## 退出条件

本 slice 只有同时满足下面这些条件才算完成：

- `PromptProjection` 已成为 prompt scope 的唯一运行时发布入口。
- `PromptService` 已成为 prompt / glyph 候选解析的唯一运行时入口。
- `ScaleformPromptAdapter` 已成为真正的 Scaleform delegate handler。
- `DualPad_GetActionGlyphToken` 与 `DualPad_GetActionGlyph` 仍可继续被现有 SWF 调用，但内部已经只走 `PromptService`。
- glyph / prompt 路径中不再存在：
  - `BindingManager::GetTriggerForAction(...)`
  - `TriggerToButtonArtToken(...)`
  - `ParseInputContextName(...).value_or(InputContext::Menu)`
- invalid context 已经 fail-closed，不再自动落到 generic `Menu` 或当前 published scope。
- `DualPadInputV2Tests` 通过。
- repo-owned `Interface/startmenu.swf` smoke 已执行并记录，证明旧 SWF 仍能消费旧 delegate shape。
- shadow compare 已执行、通过 `Shadow Compare Exit Gate`，并对 intentional difference 有文档化解释。
- 如发生 cutover 回退，回退边界符合 `Rollback Boundary / Emergency Revert`，且未把 mixed runtime 留在主路径。

如果仍然需要在 adapter 或 bridge 里自己补 context fallback、trigger reverse lookup 或 token 猜测，则本 slice 不算完成。

## 交接给下一 slice 的合同

交给 `docs/plans/dualpad_rearchitecture/08_slice_phase7_ingress_and_resync_zh.md` 时，下面这些合同必须已经稳定；下一 slice 不允许重写或绕过：

1. `PublishedPromptScope`
   - 只由 `PromptProjection` 发布。
   - 它只消费：
     - `PublishedPresentationState.family`
     - `PublishedPresentationState.uiContext`
     - `PublishedPresentationState.actionSets`
     - `PublishedPresentationState.epoch`
     - `manifestEpoch`
2. `PromptDescriptor`
  - 结构、状态码、候选排序、主候选选取规则已经固定。
  - `Phase 8` 的 prompt snapshot / black-box / CI 只能消费 `PromptSnapshotRecord`
  - `PromptSnapshotRecord` 固定只允许包含：
    - `actionId`
    - `status`
    - `resolvedSet`
    - `resolvedContext`
    - `primary`
     - `alternates`
     - `resolutionSource`
    - `fallback`
    - `deviceProfile`
    - `promptScopeRevision`
    - `manifestEpoch`
  - `actionId` 固定从 `PromptQuery.actionId` 派生，其余字段固定从 `PromptDescriptor` 派生；`ok` 只允许由 `status == Ok` 推导，不得再发明顶层 `origin`、`contextRevision` 或第二个 `ok`
   - Phase 7 不得再改 `priority -> bindingId` 的 deterministic tie-break。
3. invalid context 语义
   - `UnknownContext` 与 `ContextOutOfScope` 已区分清楚。
   - Phase 7 不得重新引入 `value_or(Menu)` 或 current-scope auto fallback。
4. 旧 glyph API 的兼容边界
   - 旧 API 只是 wrapper。
   - 新 API 才暴露完整 descriptor / candidate 结构。
   - Phase 7 不得再把 rich 结果压回 trigger reverse lookup。
5. `ScaleformGlyphBridge`
   - 已经是 facade，而不是 prompt truth owner。
   - Phase 7 不得再往里面补新的解析逻辑。
6. Ingress / resync 的后续工作只允许影响：
   - 何时 republish `PublishedPromptScope`
   - `manifestEpoch` / `promptScopeRevision` 的一致性
   - replay / recovery 下 prompt query 的可重复性
   但不得改变 prompt 的解析语义。

如果 Phase 7 发现 prompt 行为需要回退到 `BindingManager`、`Trigger` 或 `InputContextNames` 才能跑通，说明不是 ingress 问题，而是 Phase 6 合同没有收口，必须回到本 slice 返工。
