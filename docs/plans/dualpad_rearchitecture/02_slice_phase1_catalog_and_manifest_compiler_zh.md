# Phase 1：`ContextCatalog` 与 `ActionManifest` compiler 切片

## 目标

本 slice 的目标是先把当前散落在 `src/input/InputContextNames.cpp`、`src/input/MenuContextPolicy.cpp`、`src/input/BindingConfig.cpp`、`src/input/BindingManager.cpp` 里的名字真相、菜单策略真相、绑定真相收口成一个不可变的编译产物，然后再让现有运行时组件去消费这个产物。

本 slice 完成后，必须同时满足下面 3 点：

- `ContextCatalog` 成为唯一的 `UiContextId / canonicalContextName / legacyInputContext / alias / menu-name / runtime hint / action-set anchor / presentation policy` 真相源。
- `ActionManifest` 成为唯一的 `action / base set / layer / binding / display binding / output descriptor` 真相源。
- 现有 `InputContextNames`、`MenuContextPolicy`、`BindingConfig` 仍保留现有公开接口，但内部不再各自解析 `INI`，而是统一从编译后的 bundle 读取。

本 slice 明确不做下面这些事：

- 不落地 `UiMenuObserver`、`MenuInstanceRegistry`、`ContextResolver`。
- 不切 `PromptProjection / PromptService`。
- 不切 `InteractionEngine`、`GameplayProjection`、`InputKernel` 主循环。
- 不改 `ScaleformGlyphBridge` 的 API 语义，只保证它后续可读取新的 catalog / manifest 结果。
- 在 `Phase 6` 之前，任何 `status / fallback / ambiguity` 诊断都只允许留在内部 diagnostics / logs / targeted tests，不得扩展旧 SWF / delegate 返回 shape。
## 当前 repo reality 缺口与 breach 边界

- 在 `Phase 1` 真正开工前，当前 repo 允许尚不存在下列 deliverables：
  - `src/input_v2/context/`
  - `src/input_v2/actions/`
  - `src/input_v2/config/`
  - `target("DualPadManifestCompilerTests")`
- 上述目录与 target 属于本 slice 要新增的 compiler surface，不是当前 baseline 已兑现的现状事实。
- 只有在 `Phase 1` 被宣称 done / promoted / prove-out 通过后，这些入口仍缺失，才构成正式 breach。

## 冻结的设计决定

### 1. 目录与文件落点现在就固定

本 slice 的新代码固定落在下面这些 repo-relative 路径，不再临时找地方塞：

- `src/input_v2/context/ContextCatalog.h`
- `src/input_v2/context/ContextCatalog.cpp`
- `src/input_v2/actions/ActionManifest.h`
- `src/input_v2/actions/ActionManifest.cpp`
- `src/input_v2/config/LegacyIniImporter.h`
- `src/input_v2/config/LegacyIniImporter.cpp`
- `src/input_v2/config/ManifestValidator.h`
- `src/input_v2/config/ManifestValidator.cpp`
- `src/input_v2/config/AtomicConfigReloader.h`
- `src/input_v2/config/AtomicConfigReloader.cpp`

本 slice 的测试固定落在：

- `tests/input_v2/ContextCatalogTests.cpp`
- `tests/input_v2/ActionManifestTests.cpp`
- `tests/input_v2/LegacyIniImporterTests.cpp`
- `tests/input_v2/ManifestValidatorTests.cpp`
- `tests/input_v2/AtomicConfigReloaderTests.cpp`
- `tests/fixtures/input_v2/`

本 slice 允许修改的既有代码文件固定是：

- `src/input/InputContextNames.h`
- `src/input/InputContextNames.cpp`
- `src/input/MenuContextPolicy.h`
- `src/input/MenuContextPolicy.cpp`
- `src/input/BindingConfig.h`
- `src/input/BindingConfig.cpp`
- `src/main.cpp`
- `xmake.lua`

### 2. `ContextCatalog` 的职责与数据形态固定

`ContextCatalog` 固定由 `src/input_v2/context/ContextCatalog.*` 持有，且 `ContextCatalog.cpp` 内的 built-in seed 就是唯一 canonical context seed。`InputContextNames.cpp` 和 `MenuContextPolicy::KnownMenuNameToContext()` 不再保留第二份手写表。

`ContextCatalog` 的每个 entry 现在就定为至少包含下面这些字段：

- `uiContextId`
  - 唯一运行时 canonical ID。
- `canonicalContextName`
  - 唯一稳定字符串名，供 `INI`、文档、日志、调试输出、generated docs 使用。
- `legacyInputContext`
  - 指向当前 `InputContext` enum 的兼容镜像目标。
  - 这是 compatibility projection，不是 canonical 轴；它可以为空，但不允许被用来反推 `UiContextId`。
- `aliases`
  - 兼容 `INI` 节名、菜单别名、现有 SWF `contextName` 文本。
- `menuNames`
  - RE 菜单名列表，例如 `FavoritesMenu`、`JournalMenu`。
- `runtimeFingerprintHints`
  - 当前 `MenuContextPolicy` 里已有的 `inputContextValue` / `menuFlagsValue` / `hasMovie` / `hasDelegate` 一类线索，统一进 catalog entry。
- `family`
  - 明确区分 gameplay、tracked menu、passthrough overlay、special menu，禁止再用 `InputContext` 单个 enum 混装所有语义。
- `defaultActionSetId`
  - 固定表示“该 context 的 base set anchor”，不是完整 runtime action set。
  - 当前 repo 在本 slice 只允许 `GameplayBase` 与 `MenuBase` 两个 base set；不得再为每个 context 生成 `<CanonicalContextName>.Base`。
- `defaultLayerIds`
  - 该 context 激活时要叠加的固定 layer 列表。
- `scopeAnchorIds`
  - 供 Phase 2 resolver、Phase 6 prompt scope、docgen、日志统一使用的 scope anchor 链。
  - 规则固定为“先 `defaultActionSetId`，后 `defaultLayerIds`”，不得在运行时从 `legacyInputContext` 或菜单名反推。
- `presentationPolicyId`
  - 先只落到 catalog 字段，不在本 slice 内切运行时 owner 决策。

`ContextCatalog` 还要带一份全局 catalog metadata，用来承载：

- `unknownMenuPolicy`
- `ignoreRules`
- `trackRules`

这 3 项来源于 `config/DualPadMenuPolicy.ini`，但它们编译后只存在于 `ContextCatalog`，不再由 `MenuContextPolicy` 直接维护第二份 `_config` 真相。

#### Canonical Identifier Family

命名轴现在就冻结成下面 3 层，不允许 Phase 2 或后续 slice 再各自发明第二张表：

- `UiContextId`
  - 唯一运行时 canonical ID。
- `canonicalContextName`
  - 唯一稳定字符串名，与 `UiContextId` 一一对应。
- `legacyInputContext`
  - 兼容镜像目标；它不是 canonical，不允许被拿来反解 `UiContextId`、`scopeAnchorIds`、`defaultLayerIds`。

内建 seed 里必须至少写死下面这张映射表，Phase 2 只能引用它，不能再重新解释：

| `UiContextId` | `canonicalContextName` | `legacyInputContext` | `defaultActionSetId` | `defaultLayerIds` | `scopeAnchorIds` |
| --- | --- | --- | --- | --- | --- |
| `UiContextId::None` | `Gameplay` | `InputContext::Gameplay` | `GameplayBase` | `[]` | `[GameplayBase]` |
| `UiContextId::Inventory` | `InventoryMenu` | `InputContext::InventoryMenu` | `MenuBase` | `[InventoryLayer]` | `[MenuBase, InventoryLayer]` |
| `UiContextId::Journal` | `JournalMenu` | `InputContext::JournalMenu` | `MenuBase` | `[JournalLayer]` | `[MenuBase, JournalLayer]` |
| `UiContextId::Map` | `MapMenu` | `InputContext::MapMenu` | `MenuBase` | `[MapLayer]` | `[MenuBase, MapLayer]` |
| `UiContextId::Favorites` | `FavoritesMenu` | `InputContext::FavoritesMenu` | `MenuBase` | `[FavoritesLayer]` | `[MenuBase, FavoritesLayer]` |
| `UiContextId::Book` | `BookMenu` | `InputContext::BookMenu` | `MenuBase` | `[BookLayer]` | `[MenuBase, BookLayer]` |
| `UiContextId::Console` | `Console` | `InputContext::Console` | `MenuBase` | `[ConsoleLayer]` | `[MenuBase, ConsoleLayer]` |
| `UiContextId::UnknownTrackedMenu` | `Menu` | `InputContext::Menu` | `MenuBase` | `[UnknownTrackedMenuLayer]` | `[MenuBase, UnknownTrackedMenuLayer]` |
| `UiContextId::PassthroughOverlay` | `PassthroughOverlay` | `nullopt` | `nullopt` | `[]` | `[]` |

上表之外如需新增 context，必须同时新增：

- `UiContextId`
- `canonicalContextName`
- `legacyInputContext`
- `defaultActionSetId`
- `defaultLayerIds`
- `scopeAnchorIds`

不允许只加字符串别名，不加 canonical ID；也不允许只加 `UiContextId`，把字符串名留给实现阶段临时猜。

### 3. `ActionManifest` 的职责与数据形态固定

`ActionManifest` 固定由 `src/input_v2/actions/ActionManifest.*` 持有，且 `ActionManifest.cpp` 内的 built-in action registry 是唯一 action metadata 真相源。`config/DualPadBindings.ini` 只负责声明“哪些 legacy section 绑定了哪些动作”，不允许在运行时再反推 action contract。

`ActionManifest` 现在就定为包含下面这些表：

- `actions`
- `actionSets`
- `actionLayers`
- `bindings`
- `displayBindings`
- `outputDescriptors`
- `policies`
- `touchpadConfig`
- `legacyBindingProjection`

其中关键冻结点如下：

- `actions`
  - 必须显式有 `id`、`valueKind`、`contract`、`outputDescriptorId`、`promptHintId`、`domain`。
  - `unknown action` 直接 load fail，不允许像当前 `BindingManager` 一样默默接收任意字符串。
- `actionSets`
  - 在本 slice 只表示 runtime base set registry。
  - 当前 repo 只允许 `GameplayBase` 与 `MenuBase` 两个 base set；不得再物化 `Gameplay.Base`、`InventoryMenu.Base`、`JournalMenu.Base` 这类 per-context set。
- `actionLayers`
  - 固定表示 runtime 可叠加 layer。
  - 它既包含 gameplay substate layer，也包含 menu context layer，例如 `CombatLayer`、`SneakLayer`、`InventoryLayer`、`JournalLayer`、`UnknownTrackedMenuLayer`。
- `bindings`
  - 每条 binding 必须带规范化后的 `controlPath`、`interactionSpec`、`actionId`、`baseSetId`、`layerId`、`deviceFamily`。
  - `baseSetId` 必填，`layerId` 可空；不得再出现 per-context set id。
- `displayBindings`
  - 由 compiler 从 compiled binding 规则预编译生成。
  - 同样固定归属到 `baseSetId + optional layerId`，不允许留给运行时 `glyph reverse lookup` 再猜。
- `touchpadConfig`
  - 从 `DualPadBindings.ini` 的 `[Touchpad]` 编译进 manifest，不再由 `BindingConfig` 单独持有解析真相。

#### ActionSet Architecture Freeze

本 slice 把动作集架构一次定死成 `Base Set + Layer Stack`，不再同时保留“每 context 一个 `.Base` set”和“`MenuBase + Layer`”两种模型。

固定规则如下：

- `defaultActionSetId`
  - 只表示 base set anchor。
- `defaultLayerIds`
  - 表示该 context 激活时需要叠加的固定 layer 列表。
- `scopeAnchorIds`
  - 表示文档、日志、prompt scope、generated docs 统一使用的 scope anchor 链。
- runtime `ActionSetStack`
  - 只能由 `baseSetId + activeLayerIds + scopeAnchorIds` 组成。

当前 repo 的正式模型固定为：

- 无 tracked menu 时：
  - `GameplayBase + gameplaySubstate layers`
- tracked menu 命中 catalog 时：
  - `MenuBase + catalog-declared layers`
- unknown tracked menu 时：
  - `MenuBase + UnknownTrackedMenuLayer`
- passthrough overlay：
  - 不拥有 action set，也不叠加 layer

legacy `Inherit=` 也在本 slice 里收口，不留给运行时再 merge：

- `Inherit=ParentContext`
  - 不再创建新的 per-context base set。
  - compiler 负责把继承链展平到 `baseSetId + layerId` 归属下的最终 compiled bindings / display bindings。
  - runtime 不再持有 `parentSetId` 级联逻辑。

#### LegacyBindingProjection Contract

`legacyBindingProjection` 现在就冻结成受约束的单向投影，不是“老系统继续补逻辑的缓冲区”。

它至少必须包含下面这些信息：

```cpp
struct LegacyBindingProjection {
    std::uint64_t manifestEpoch;
    std::vector<ProjectedLegacyBinding> bindings;
    std::vector<ProjectedLegacyDisplayBinding> displayBindings;
    ProjectedTouchpadConfig touchpadConfig;
};
```

冻结规则如下：

- `manifestEpoch`
  - 必填，且必须与同一 `CompiledActionManifest` 的 epoch 完全一致。
- `bindings` / `displayBindings`
  - 只能从 compiled manifest 单向序列化出来，不允许在 projection 阶段新增 lookup、fallback、merge、优先级推理。
- `touchpadConfig`
  - 只能直接搬运 compiled manifest 中已定型的配置。
- `BindingManager`
  - 只能消费 projection，不允许再读取原始 `INI`、再做 trigger 解析、再从 `legacyInputContext` 或菜单名反推 binding truth。

下面这些行为在 projection 层一律禁止：

- 重新解析 `DualPadBindings.ini`
- 生成第二套 runtime reverse lookup 真相
- 依据 legacy `InputContext` 临时做 scope fallback
- 补“看上去更兼容”的字符串别名推理

本 slice 必须补 parity tests，证明：

- `BindingManager` 看到的所有 legacy binding 都来自 `legacyBindingProjection`
- projection 不会在 compiled manifest 之外再长出第二 authority
- `manifestEpoch` 不匹配时，projection 直接视为非法

### 4. legacy 语法的 lowering 规则现在就冻结

`LegacyIniImporter` 只负责把 legacy `INI` 解析成 AST 和 source span，不做 runtime mutation。语义 lowering 固定如下：

- `[ContextSection]`
  - 先保留 raw section name，真正的 canonical 解析放在 compile 阶段由 `ContextCatalog` 完成。
- `Inherit=ParentContext`
  - 编译成 compile-time inheritance edge，由 compiler 展平成最终的 `bindings` / `displayBindings`。
  - runtime 不再做 set merge 或 layer merge。
- `Button:Cross=...`
  - 编译成 digital `controlPath=Button/Cross` + `interaction=Press`。
- `Hold:Circle=...`
  - 编译成 digital `controlPath=Button/Circle` + `interaction=Hold`。
- `Tap:TouchpadClick=...`
  - 编译成 digital `controlPath=Button/TouchpadClick` + `interaction=Tap`。
- `Combo:L1+R1=...`
  - 编译成 `unordered chord interaction`。
  - 所有成员对称；binding key 用规范化后的按钮顺序去重。
- `Layer:L1+Square=...`
  - 编译成“带精确 chord 约束的主路径 interaction”。
  - 右侧最后一个 token 固定作为 `primary controlPath`，其余 token 编译进 `requiredChordPaths[]`。
  - 这是非顺序语义，不允许再写成 `ordered chord interaction`。
  - 它不会创建 runtime `action layer`。
- `Gesture:TpSwipeLeft=...`
  - 编译成 `controlPath=TouchGesture/TpSwipeLeft` + `interaction=Gesture`。
- `Axis:LeftStickX=...`
  - 编译成 `controlPath=Axis/LeftStickX` + `interaction=Value`。

`Combo` 与 `Layer` 的差异在本 slice 一次定死：

| 语法 | 运行时语义 | 是否顺序敏感 | 是否有 primary path | 是否创建 runtime layer |
| --- | --- | --- | --- | --- |
| `Combo:A+B` | 对称 chord | 否 | 否 | 否 |
| `Layer:A+B` | 主路径 + required chord 约束 | 否 | 是，固定取最后一个 token | 否 |

下面这些规则一并冻结：

- `Combo:*` 只能是两个数字按钮。
- `Layer:*` 只能包含数字按钮；重复 token 直接 load fail。
- 任意 `FN + face button` 组合继续直接 load fail。
- 同一 `baseSetId + layerId` 下，同一规范化 binding key 指向多个 action 时直接 load fail。
- 同一 `actionId` 在同一 `baseSetId + layerId` 下如果生成多个可显示的 `displayBinding`，且没有显式优先级来源，则直接 load fail。

### 5. 编译顺序固定，不允许实现时临场改流水线

本 slice 的 compiler 流水线固定为下面这个顺序：

1. `LegacyIniImporter`
   - 读取 `config/DualPadBindings.ini`
   - 读取 `config/DualPadMenuPolicy.ini`
   - 产出 `LegacyImportBundle`
2. `ManifestValidator::ValidateImportedAst()`
   - 先做 AST 级校验，尽早拦截格式和重复项
3. `ContextCatalog::Compile()`
   - built-in context seed + legacy menu policy AST -> `CompiledContextCatalog`
4. `ActionManifest::Compile()`
   - built-in action registry + `CompiledContextCatalog` + legacy bindings AST -> `CompiledActionManifest`
5. `ManifestValidator::ValidateCompiledBundle()`
   - 做 catalog ID 映射、base set / layer、display binding、projection contract 等交叉校验
6. `AtomicConfigReloader::Promote()`
   - 只有前 5 步全部成功后才允许替换 active bundle

任何代码都不允许绕过这个顺序去直接“边 parse 边改 singleton”。

### 6. `ManifestValidator` 的责任边界固定

`ManifestValidator` 固定拆成两阶段：

- `ValidateImportedAst()`
  - 负责语法级和 source-level 约束。
- `ValidateCompiledBundle()`
  - 负责跨表、跨 base set、跨 layer、跨 projection 的一致性约束。

必须覆盖的失败条件现在就锁死：

- unknown action -> load fail
- duplicate `UiContextId` -> load fail
- duplicate `canonicalContextName` -> load fail
- duplicate context alias -> load fail
- duplicate menu name to different context -> load fail
- `defaultActionSetId` 指向未知 base set -> load fail
- `defaultLayerIds` 指向未知 layer -> load fail
- `scopeAnchorIds` 与 `defaultActionSetId + defaultLayerIds` 不一致 -> load fail
- 同一规范化 binding key 冲突 -> load fail
- ambiguous visible display binding -> load fail
- `Layer` / `Combo` 语义不满足冻结规则 -> load fail
- `legacyBindingProjection.manifestEpoch` 与 manifest epoch 不一致 -> load fail
- 同名 action 被编译出互相矛盾的 contract / valueKind / output descriptor -> load fail

### 7. atomic swap 与 `last-known-good` 的边界固定

`AtomicConfigReloader` 是本 slice 唯一允许持有 active bundle 的组件，固定放在 `src/input_v2/config/AtomicConfigReloader.*`。

atomic swap 现在就定为“内存 bundle 指针切换”，不是磁盘 rename：

- scratch compile 全程只在内存里构建 `CompiledConfigBundle`
- active state 固定为 `std::shared_ptr<const CompiledConfigBundle>`
- 所有消费者只拿只读快照，不允许拿可变引用
- promote 时只在极小临界区内完成指针替换

`last-known-good` 也现在就定死：

- 内存内必须同时保留
  - `activeBundle`
  - `lastKnownGoodBundle`
- 磁盘上只持久化成功 promote 后的 bundle
  - 文件名常量固定为 `DualPad.Manifest.lkg.json`
  - 放置位置由 `AtomicConfigReloader` 解析为“与部署后的 `DualPadBindings.ini` / `DualPadMenuPolicy.ini` 同目录”
- compile 失败时：
  - 如果当前已有 `activeBundle`，直接保持现状，不替换
  - 如果是启动期且没有 `activeBundle`，优先回退到磁盘 `last-known-good`
  - 只有“配置文件缺失”这一类非损坏场景允许走 built-in defaults；“配置文件存在但非法”不允许悄悄回退到默认值掩盖问题

`manifestEpoch` 在本 slice 里也一起收口：

- 每次成功 promote 后递增
- `CompiledContextCatalog`、`CompiledActionManifest`、`legacyBindingProjection` 必须共享同一 `manifestEpoch`
- 后续 slice 只能消费这个 epoch，不能各自再引入第二个 config revision

### 8. 兼容层策略固定

Phase 1 不是把旧组件删掉，而是把它们降格为 compatibility facade。具体冻结如下：

- `InputContextNames::ParseInputContextName()`
  - 改为走 `ContextCatalog` 的 alias index
  - 不再保留第二套 hard-coded 名称映射
- `MenuContextPolicy::Load()/Reload()`
  - 改为读取 active bundle 里的 catalog metadata
  - 不再直接解析 `config/DualPadMenuPolicy.ini`
- `BindingConfig::Load()/Reload()`
  - 改为触发 `AtomicConfigReloader`
  - 然后把 `ActionManifest` 的 `legacyBindingProjection` materialize 回 `BindingManager`
  - 不再直接解析 `config/DualPadBindings.ini`
- `BindingManager`
  - 本 slice 继续保留，作为 Phase 4 / Phase 6 之前的兼容查询后端
  - 但它不再是 binding truth，只能消费 projection
- `ScaleformGlyphBridge`
  - 本 slice 不改 API，只继续消费 `BindingManager`

## 前置依赖

- `docs/plans/dualpad_rearchitecture/01_slice_phase0_freeze_and_replay_barrier_zh.md` 的 replay / diff / golden trace barrier 已存在，否则本 slice 不应开始做 runtime 切换。
- 当前 repo truth 仍以 `README.md`、`src/ARCHITECTURE.md`、`docs/current_input_pipeline_zh.md`、`docs/main_menu_glyph_current_status_zh.md` 为准，Phase 1 只能做“真相收口”，不能偷做后续 projection 切换。
- 当前 legacy 输入面必须原样保留：
  - `config/DualPadBindings.ini`
  - `config/DualPadMenuPolicy.ini`
  - `src/input/BindingManager.*`
  - `src/input/MenuContextPolicy.*`
  - `src/input/glyph/ScaleformGlyphBridge.*`
- 如需追踪拆分前总纲或旧口径差异，再回看 `docs/plans/dualpad_rearchitecture/dualpad_rearchitecture_plan_zh.md`；它不是本 slice 的执行前置。

## 涉及代码与文档

### 新增代码

- `src/input_v2/context/ContextCatalog.*`
  - `UiContextId` 映射、`canonicalContextName`、alias index、menu-name index、runtime hints、action-set anchor、scope anchor、presentation policy。
- `src/input_v2/actions/ActionManifest.*`
  - canonical action registry、compiled base sets、compiled layers、compiled bindings、compiled display bindings、legacy binding projection。
- `src/input_v2/config/LegacyIniImporter.*`
  - legacy `INI` -> AST + diagnostics + source spans。
- `src/input_v2/config/ManifestValidator.*`
  - AST 校验 + compiled bundle 校验。
- `src/input_v2/config/AtomicConfigReloader.*`
  - scratch compile、atomic promote、last-known-good 恢复、active bundle 快照。

### 修改既有代码

- `src/input/InputContextNames.*`
  - 从手写 alias map 改成 catalog facade。
- `src/input/MenuContextPolicy.*`
  - 从直接解析 `INI` 改成 compiled catalog facade。
- `src/input/BindingConfig.*`
  - 从直接解析 `INI` 改成 compiled manifest facade。
- `src/main.cpp`
  - 启动顺序改为先加载 `RuntimeConfig`，再加载 `AtomicConfigReloader`，再启动 legacy facade。
- `xmake.lua`
  - 增加 `DualPadManifestCompilerTests` 目标。

### 新增测试与夹具

- `tests/input_v2/ContextCatalogTests.cpp`
- `tests/input_v2/ActionManifestTests.cpp`
- `tests/input_v2/LegacyIniImporterTests.cpp`
- `tests/input_v2/ManifestValidatorTests.cpp`
- `tests/input_v2/AtomicConfigReloaderTests.cpp`
- `tests/fixtures/input_v2/valid/`
- `tests/fixtures/input_v2/invalid/`

### 参考文档

- `docs/current_input_pipeline_zh.md`
- `docs/backend_routing_decisions.md`
- `docs/main_menu_glyph_current_status_zh.md`
- 如需历史背景，再回看 `docs/plans/dualpad_rearchitecture/dualpad_rearchitecture_plan_zh.md`。

## 实施步骤

### 步骤 1：先把 `ContextCatalog` 与 `ActionManifest` 的 seed 写出来

先创建：

- `src/input_v2/context/ContextCatalog.h`
- `src/input_v2/context/ContextCatalog.cpp`
- `src/input_v2/actions/ActionManifest.h`
- `src/input_v2/actions/ActionManifest.cpp`

具体要求：

- 把当前 `InputContext` 暴露给外部的稳定字符串名完整搬进 `ContextCatalog.cpp` 的 built-in seed。
- 把当前 `InputContextNames.cpp` 和 `MenuContextPolicy::KnownMenuNameToContext()` 里已有 alias / menu name 全部统一搬进 seed，不允许保留两份列表。
- 把 `UiContextId <-> canonicalContextName <-> legacyInputContext` 的正式映射表直接写进 seed，而不是到 Phase 2 再补。
- 把 `defaultActionSetId / defaultLayerIds / scopeAnchorIds` 也一并写进 seed，保证 Phase 2 可以直接消费。
- 把当前正式 action surface 的 metadata 明确写进 `ActionManifest.cpp` 的 built-in action registry。
- `ActionManifest` 的 built-in base set 只允许写 `GameplayBase`、`MenuBase`；menu 与 gameplay 的差异靠 layer 表达，不得再往 manifest 里塞 per-context `.Base`。

此步骤结束时，先不要接运行时，只保证 seed 是完整、可枚举、可校验的。

### 步骤 2：实现 `LegacyIniImporter`，把 legacy 文件解析成 AST

创建：

- `src/input_v2/config/LegacyIniImporter.h`
- `src/input_v2/config/LegacyIniImporter.cpp`

要求：

- 只解析，不做 runtime mutation。
- 每条 AST 记录都要带 source file、section、line number。
- `DualPadBindings.ini` 和 `DualPadMenuPolicy.ini` 的 parser 都放在这里。
- 当前 `BindingConfig.cpp` 里的 trigger 语法解析逻辑整体迁到这里；`BindingConfig` 本体不再保留 `ParseIniFile`、`ParseBinding`、`ParseTrigger` 作为真正真相实现。
- 当前 `MenuContextPolicy::ParseConfig()` 改为调用 importer；保留这个 API 只是为了兼容现有测试和外部调用。

此步骤结束时，必须已经能从 fixture 读出：

- touchpad 配置
- context section
- inherit 关系
- binding 记录
- menu policy 记录

### 步骤 3：实现 `ContextCatalog::Compile()`

在 `src/input_v2/context/ContextCatalog.*` 中实现 compile 逻辑：

- 输入是 built-in seed + imported menu policy AST。
- 输出是 `CompiledContextCatalog`。
- `trackRules` 只能映射到已知 `UiContextId`。
- `ignoreRules` 进入 catalog metadata。
- duplicate alias、duplicate menu name、unknown target context 直接 fail。
- `canonicalContextName` 唯一性、`legacyInputContext` 合法性、`defaultActionSetId / defaultLayerIds / scopeAnchorIds` 合法性在这里完成第一次静态装配。

这一阶段就把下面这两个旧入口改成 facade：

- `src/input/InputContextNames.cpp`
- `src/input/MenuContextPolicy.cpp` 的 `KnownMenuNameToContext()`

要求：

- `ParseInputContextName()` 不再读手写 `if/else` 链，而是直接读 compiled catalog 或 builtin catalog fallback。
- `KnownMenuNameToContext()` 只做 catalog 查询，不再维护第二套 known menu 表。

### 步骤 4：实现 `ActionManifest::Compile()`

在 `src/input_v2/actions/ActionManifest.*` 中实现 compile 逻辑：

- 输入是 built-in action registry + compiled catalog + imported bindings AST。
- 输出是 `CompiledActionManifest`。

固定编译规则：

- 不再为每个 context 生成 `<CanonicalContextName>.Base`。
- 只生成 runtime base sets 与 runtime layers。
- `Inherit=` 在 compile 阶段展平，不把 merge 逻辑留到运行时。
- legacy `Button/Hold/Tap/Combo/Layer/Gesture/Axis` 全部按前面冻结的 lowering 规则下沉到 `bindings` 和 `displayBindings`。
- `displayBinding` 预编译时就要判定“可显示”还是“隐藏”。
- `ActionManifest` 必须同时产出一份 `legacyBindingProjection`，供 `BindingConfig` 重新 materialize 到旧 `BindingManager`。

此步骤不允许把 prompt scope、display 归属、scope anchor 规则留给 Phase 6 再想；Phase 1 就要把 base set、layer、scope anchor 的 contract 定住。

### 步骤 5：实现 `ManifestValidator`

创建并接入：

- `src/input_v2/config/ManifestValidator.h`
- `src/input_v2/config/ManifestValidator.cpp`

先做 `ValidateImportedAst()`，再做 `ValidateCompiledBundle()`。

测试必须覆盖：

- unknown action
- duplicate `UiContextId`
- duplicate `canonicalContextName`
- duplicate alias
- duplicate menu name
- invalid base set anchor
- invalid layer reference
- invalid `scopeAnchorIds`
- same normalized binding key collision
- ambiguous visible display binding
- `FN + face button`
- `Combo:*` 不是两键
- `Layer:*` 违反非顺序 chord 规则
- projection epoch 不匹配

validator 不允许只吐 warning；上面这些都必须 hard fail。

### 步骤 6：实现 `AtomicConfigReloader`

创建：

- `src/input_v2/config/AtomicConfigReloader.h`
- `src/input_v2/config/AtomicConfigReloader.cpp`

固定职责：

- 统一拿配置路径
- 发起 importer / validator / compiler 流水线
- 维护 `activeBundle`
- 维护 `lastKnownGoodBundle`
- 处理启动期恢复和运行期 reload

固定实现要求：

- scratch compile 在锁外进行
- promote 在锁内只做指针切换和 epoch 更新
- 磁盘 `last-known-good` 写入是 promote 成功后的附带动作
- 序列化失败只影响持久化，不允许回滚已经成功的 active bundle
- `legacyBindingProjection.manifestEpoch` 必须跟 promote 后的 bundle epoch 一致

启动顺序固定改成：

1. `RuntimeConfig::Load()`
2. `AtomicConfigReloader::LoadOrRecover()`
3. `MenuContextPolicy::Load()`
4. `BindingConfig::Load()`
5. 其他输入系统初始化

其中第 3、4 步本质上只是在从 active bundle 同步兼容视图。

### 步骤 7：把 legacy facade 全部改成消费编译产物

修改：

- `src/input/BindingConfig.h`
- `src/input/BindingConfig.cpp`
- `src/input/MenuContextPolicy.h`
- `src/input/MenuContextPolicy.cpp`
- `src/input/InputContextNames.h`
- `src/input/InputContextNames.cpp`

要求：

- `BindingConfig::Load()/Reload()` 只负责触发 reloader 并把 `legacyBindingProjection` materialize 到 `BindingManager`。
- `BindingConfig::GetTouchpadConfig()` 只读 manifest 内编译好的 touchpad 配置。
- `MenuContextPolicy` 不再维护直接来源于文件的 `_config` 真相；可以保留 facade 缓存，但来源必须是 active bundle。
- `ParseInputContextName()` 对外表现保持兼容，但实际查的是 catalog。
- `BindingManager` 只能消费 projection，不允许再补 trigger parse / fallback / merge。

这一步做完后，仓库里就只剩一条 config truth 链：

`DualPadBindings.ini / DualPadMenuPolicy.ini -> LegacyIniImporter -> ContextCatalog / ActionManifest -> legacy facade`

### 步骤 8：补齐测试目标并做一次最小 runtime 集成

修改 `xmake.lua`：

- 新增 `DualPadManifestCompilerTests`
- 把 `tests/input_v2/*.cpp` 纳入编译

修改 `src/main.cpp`：

- 接上 `AtomicConfigReloader`
- 确保数据加载顺序正确

最小集成要求：

- 构建通过
- 新测试通过
- 现有 `DualPadMenuContextPolicyTests` 不因为 facade 化而回归
- 插件启动时能在日志里明确看到
  - compile 成功
  - active bundle promote
  - 使用 `last-known-good` 恢复
  - compile 失败但保留旧 active bundle

## 验证与观测

必须执行的命令：

```powershell
xmake build DualPadManifestCompilerTests
xmake run DualPadManifestCompilerTests
xmake build DualPadMenuContextPolicyTests
xmake run DualPadMenuContextPolicyTests
xmake build DualPad
```

必须覆盖的测试场景：

- `UiContextId <-> canonicalContextName <-> legacyInputContext` 映射表完整且唯一
- `defaultActionSetId / defaultLayerIds / scopeAnchorIds` 对齐
- 不再出现任何 `<CanonicalContextName>.Base` per-context set
- `Layer` 为非顺序 chord 约束，`Combo` 为对称 chord
- valid bindings + valid menu policy -> compile 成功，生成 `activeBundle`
- invalid action id -> compile fail，保留旧 `activeBundle`
- duplicate alias -> compile fail
- invalid base set / invalid layer / invalid scope anchor -> compile fail
- ambiguous display binding -> compile fail
- projection epoch 与 manifest epoch 一致
- `BindingManager` 只消费 projection，不再自行解析 `INI`
- 启动期配置损坏 + 磁盘存在 `last-known-good` -> 成功恢复
- 启动期配置损坏 + 无 `last-known-good` -> 明确失败，不偷偷回退默认值
- 配置文件缺失 -> 允许 built-in defaults 成功启动

必须观察的日志点：

- `AtomicConfigReloader` 开始 scratch compile
- imported AST 校验结果
- catalog compile 成功 / 失败
- manifest compile 成功 / 失败
- promote 的 `manifestEpoch`
- `legacyBindingProjection.manifestEpoch`
- `last-known-good` 写入成功 / 失败
- reload 失败时保留旧 active bundle 的说明

## 退出条件

本 slice 只有同时满足下面这些条件才算完成：

- `ContextCatalog` 与 `ActionManifest` 已经落在 `src/input_v2/`，并由 `AtomicConfigReloader` 统一产出 active bundle。
- `UiContextId / canonicalContextName / legacyInputContext` 的正式映射表已经进入 compiled catalog，Phase 2 不再需要重建。
- `defaultActionSetId / defaultLayerIds / scopeAnchorIds` 已经稳定发布，且 `ActionSet` 架构只剩 `Base Set + Layer Stack` 一种模型。
- `InputContextNames`、`MenuContextPolicy`、`BindingConfig` 已经变成 facade，不再直接解析 `INI`。
- 当前 legacy 运行时仍能通过 `BindingManager`、`MenuContextPolicy`、`ScaleformGlyphBridge` 正常工作。
- `legacyBindingProjection` 已绑定 `manifestEpoch`，且有 parity tests 证明它不是第二 authority。
- `unknown action`、`duplicate alias`、`invalid base set / layer / scope anchor`、`ambiguous display binding`、`last-known-good` 恢复都有自动化测试。
- `xmake build DualPadManifestCompilerTests`
  - 通过
- `xmake run DualPadManifestCompilerTests`
  - 通过
- `xmake build DualPadMenuContextPolicyTests`
  - 通过
- `xmake run DualPadMenuContextPolicyTests`
  - 通过
- `xmake build DualPad`
  - 通过

## 交接给下一 slice 的合同

交给 `03_slice_phase2_menu_instance_truth_zh.md` 时，必须已经稳定提供下面这些合同，下一 slice 不允许回头重新定义：

- `AtomicConfigReloader::GetActiveBundle()`
  - 能拿到只读、不可变的 compiled config snapshot。
- `CompiledContextCatalog`
  - 已经能回答：
    - `UiContextId -> canonicalContextName`
    - `canonicalContextName -> UiContextId`
    - `UiContextId -> legacyInputContext`
    - `UiContextId -> defaultActionSetId`
    - `UiContextId -> defaultLayerIds`
    - `UiContextId -> scopeAnchorIds`
    - alias -> `UiContextId`
    - menu name -> `UiContextId`
    - unknown menu policy / ignore / track overlay
- `CompiledActionManifest`
  - 已经能回答：
    - base sets
    - action layers
    - compiled bindings
    - compiled display bindings
    - `legacyBindingProjection`
- `legacyBindingProjection`
  - 已绑定 `manifestEpoch`
  - 只允许单向 materialize 到 `BindingManager`
  - 不允许在 Phase 2 再往里补第二套推理逻辑
- `manifestEpoch`
  - 成功 promote 后稳定递增
  - Phase 2 之后的 `contextRevision` / `menuStackRevision` / boundary key 可以直接把它当成 config 边界输入之一
- 兼容层边界
  - `BindingManager` 仍存在，但不再是 binding truth
  - `MenuContextPolicy` 仍存在，但不再是 menu policy truth
  - Phase 2 只能新增 `UiMenuObserver` / `MenuInstanceRegistry` / `ContextResolver` 去消费 `CompiledContextCatalog`，不能再把真相写回旧手写表
