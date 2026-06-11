# DP5-RC20 U4 Config / Prompt / Menu / Glyph Contract

本文记录 DP5-RC20 U4 的 config / prompt / menu / glyph contract closure 结论。它不是新的 runtime phase，不改变 `src/input_v2/` mainline，不改变旧 `DualPad_GetActionGlyphToken` 单 token 返回，也不恢复 `FavoritesMenu` SWF workspace。

## 覆盖边界

U4 只关闭用户可见矩阵的解释和诊断缺口：

- config：schema、reload、migration / failure path 继续沿 U3 fail-closed gate，不新增 runtime 配置主线。
- prompt：`PromptRuntimeOwner -> PromptService` 继续是 repo-owned prompt authority；degraded / unavailable / hidden / mismatch 都必须可诊断。
- menu：`docs/generated/context_catalog_zh.md`、`docs/generated/action_sets_zh.md`、`docs/generated/prompt_matrix_zh.md` 与 `docs/generated/policies_zh.md` 是可枚举事实。
- glyph：本轮冻结 glyph/icon contract 字段，不生产最终视觉资产。

## zero direct binding 分类

`zero direct binding` 不等于 bug。U4 按下面分类解释 generated context catalog 中 direct binding count 为 0 的上下文：

| 分类 | 上下文 | 结论 |
| --- | --- | --- |
| inherited | `BarterMenu`、`Book`、`ContainerMenu`、`GiftMenu`、`InventoryMenu` 派生菜单、`MagicMenu`、`StatsMenu`、`TweenMenu` 等 | 从 `MenuBase` 或当前 action set stack 继承通用菜单行为；prompt resolve 可沿 scope anchor 做 `AncestorScope` fallback。 |
| inherited gameplay substate | `Combat`、`Sneaking`、`Riding`、`VampireLord`、`Werewolf`、`Bleedout`、`Death`、`KillMove`、`Ragdoll` | 这些是 gameplay / substate context，不要求每个 context 自带 direct binding；`Combat` 有 live detector，其余 substate 当前只作为 catalog / resolver / replay / legacy mirror 能力保留，进入实机 QA 前不得写成已证明的用户可见 live detector。 |
| pass-through | passthrough overlay | live overlay 判定命中时不抢占 gameplay owner，不发明 generic menu prompt；`unknown_menu_policy=passthrough` 目前只作为 parsed metadata / QA required 字段保留。 |
| ignored | `HUD Menu`、`Fader Menu`、`Cursor Menu`、`Mist Menu`、`Tutorial Menu`、`LoadWaitSpinner`、`TrueHUD` 等 | 明确由 ignore policy 排除，不进入 prompt / action ownership。 |
| unsupported | repo 未拥有页面级 workspace 的特殊页面能力 | 例如 `FavoritesMenu` 的页面级 SWF patch workspace 未恢复；repo 只拥有当前 action / prompt contract，不声明页面级 broker 已恢复。 |
| bug：当前无 | 当前无 | 当前 generated facts 中没有必须新增 direct binding 才能闭环的 bug 类缺口；后续若发现用户可见 wrong prompt，再单独开 blocker。 |

## unknown menu 与 ignored menu

当前 checked-in policy 仍生成 `unknown_menu_policy=passthrough` metadata，但文档必须区分 live implemented、parsed-only reserved 和 QA required：

| 项 | 当前分类 | 说明 |
| --- | --- | --- |
| `unknown_menu_policy=passthrough` | parsed-only reserved / QA required | `LegacyIniImporter` 与 `ContextCatalog` 会解析并保存该字段；当前 live `MenuInstanceRegistry` 未读取 `unknownMenuPolicy`，未知菜单行为仍由 known menu、explicit `[Track]` key、ignore rules 与 overlay flags 决定。不得把该字段写成已证明的用户可见 runtime switch。 |
| `[Track]` key-side menu name | live implemented | `MenuInstanceRegistry` 会用 `trackRules.contains(menuName)` 阻止该菜单被当作 passthrough overlay。 |
| `[Track]` value-side context mapping | parsed-only reserved | `ContextCatalog` 会校验 target context；当前 `ContextResolver` 不读取该 target 值来改写 resolved context。 |
| `log_unknown_menu_probe` / `log_unknown_menu_decision` | parsed-only reserved | 字段会解析到 metadata；当前 runtime 没有读取它们驱动 live logging。 |
| ignored menu rules | live implemented | `ignoreRules.contains(menuName)` 直接进入 passthrough / ignored 路径，不发布新的 menu prompt scope。 |
| `Combat` gameplay substate | live implemented | `TESCombatEvent` 经 `ContextEventSink -> ContextRefreshTick -> ContextResolver` 更新 combat context。 |
| `Sneaking` / `Riding` / `Werewolf` / `VampireLord` / `Death` / `Bleedout` / `Ragdoll` / `KillMove` | QA required / reserved | catalog、resolver 与 replay 能表示这些 substate；除现有 legacy / replay 输入外，本轮不声明完整 live detector 已实机证明。 |
| `FavoritesMenu` page workspace | non-restore workspace | 当前 repo 只拥有 generated action / prompt facts；页面级 SWF workspace 未恢复。 |

因此：

- 未知 overlay 不得被文档描述成一定会进入 generic `Menu`；当前 release 行为需要按 live `MenuInstanceRegistry` 条件解释。
- `track` 仍可作为排查配置存在，但只有 key-side tracking 是 live 行为；value-side mapping 仍是 reserved。
- ignored menu 命中后应保持 ignored：不发布新的 menu prompt scope，不制造 `MenuBase` fallback，不输出 dirty action。

这些事实由 `docs/generated/policies_zh.md` 枚举，U4 static gate 会检查 `unknown_menu_policy=passthrough` 和关键 ignored menu marker。

## FavoritesMenu non-restore workspace

`FavoritesMenu` 在当前 repo 中有可生成的 action / prompt facts：

- `Favorites.Accept`、`Favorites.Cancel`、`Favorites.Up`、`Favorites.Down` 和若干 game/menu action 具有 visible button prompt。
- `Favorites.LeftStick` 的 `Axis:LeftStickX` / `Axis:LeftStickY` 是 action fact，但不会进入 visible prompt matrix。

这不表示页面级 SWF workspace 已恢复。U4 继续执行 `non-restore workspace` 决策：如果后续要改 `FavoritesMenu` 页面逻辑，第一步仍是恢复 SWF 页面源码、patch workspace 和 artifact inventory。

## conflict detection

当前 conflict detection 的 release 边界如下：

- repeated triggers：同 action set 内重复 graph shape 对不同 action 或 combo duplicate fail compile；同一 action 的 idempotent alias collapse 允许去重。
- ambiguous visible prompts：同 action / action set / display priority / display mode 冲突 fail compile。
- ambiguous chords：combo duplicate 按 duplicate binding 处理，malformed chord required path 已 fail-closed。
- axis/button collisions：axis / gesture prompt 默认 hidden；button visible prompt 由 display binding priority 和 mode 决定，不把 axis prompt 混进 visible prompt matrix。

## prompt fail-closed 矩阵

| 状态 | 触发条件 | 行为 |
| --- | --- | --- |
| `PromptScopeState::Unavailable` / `ScopeUnavailable` | presentation truth 缺失、scope 未 ready、graph epoch 与 prompt scope epoch 不一致 | fail closed；legacy token 为空，descriptor `ok=false`。 |
| `UnknownAction` | action id 不在 compiled graph | fail closed；不从旧 binding reverse lookup 猜 glyph。 |
| `UnknownContext` | context name 无法由 catalog alias 解析 | fail closed；不回落到 generic `Menu`。 |
| `ContextOutOfScope` | 请求 context 不在当前 prompt scope anchor 链 | fail closed；不跨菜单偷用 prompt。 |
| `NoVisibleBinding` | 有 action 但没有 visible display binding | fail closed；不发明 token。 |
| `HiddenOnly` | 只有 hidden 或 legacy non-renderable binding | fail closed；不返回不可渲染 glyph。 |
| `DeviceFamilyMismatch` | 当前 device family 与 display binding profile 不匹配 | fail closed；不跨平台返回错误 glyph。 |

## glyph/icon contract

U4 冻结内部 glyph/icon contract 字段：

| 字段 | 来源 | 规则 |
| --- | --- | --- |
| glyph id | display binding token | 成功 resolve 时等于 primary candidate token；failure 时为空。 |
| platform id | display binding device profile | 例如 `DualSense`、`Gamepad`、`XInput`、`KeyboardMouse`。 |
| button semantic name | display binding localized label / token | 用于 UI 可读按钮语义，不是 action id authority。 |
| fallback text | display binding localized label / token | 图标 asset 缺失时可展示的文字 fallback。 |
| asset lookup path | deterministic contract path | `Interface/Exported/DualPad/Glyphs/<platform id>/<glyph id>.svg`；本轮不要求实际文件存在。 |
| missing icon behavior | prompt candidate / descriptor | 成功候选为 `fallback_text`；失败 descriptor 为 `fail_closed_empty_token`。 |
| debug reason | prompt status | 成功为 `Ok`；missing / unavailable glyph 使用 `ScopeUnavailable`、`HiddenOnly`、`DeviceFamilyMismatch` 等状态名。 |

这些字段进入内部 `PromptCandidate` / `PromptLegacyGlyphDescriptor` diagnostics。`ScaleformPromptAdapter` 的旧 GFx object 暂不新增字段，避免把 U4 误变成旧 SWF 返回 shape 扩展；旧 `buttonArtToken` 成功仍返回 primary token，失败仍为空。

## 非目标

- 完整 visual icon artwork production 是非目标。
- 不恢复 `BindingManager`、trigger reverse lookup、menu fallback authority 或旧 `FavoritesMenu` workspace。
- 不新增 runtime phase。
- 不改变 `input_v2` mainline、canonical targets、replay root 或旧 SWF token API。
