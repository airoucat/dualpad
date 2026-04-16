# 菜单上下文运行时识别与策略文件方案

## 1. 问题框定

当前 `ContextManager` 的菜单上下文逻辑有一个结构性问题：

- `src/input/InputContext.cpp` 里的 `MenuNameToContext()` 对未知菜单默认返回 `InputContext::Menu`
- `ShouldTrackMenu()` 目前主要只靠一小份 passive overlay 名单做排除

这意味着：

- 原生菜单会进入明确上下文，这是对的
- 未知第三方菜单只要没有进 passive overlay 名单，就会被压成 generic `Menu`
- 一批本应透传的 HUD / overlay 会把整场会话钉在 `Menu`

最近的运行时日志已经给出直接证据：

- `BTPS Ovelay Menu`
- `BestiaryWidget`
- `oxygenMeter`
- `MinionCombatParameters`
- `AchievementWidget`
- `BTPS Menu`

这些菜单都被记录为“未知菜单 fallback 到 generic Menu context”，并且其中第一个 `BTPS Ovelay Menu` 直接把 `Gameplay -> Menu`。这会让 gameplay 侧的 `Game.Look` 失效，表现为跑图 / 战斗时右摇杆无法转镜头。

与此同时，最新 runtime probe 也表明：这些菜单并不具备典型“正式菜单”的强信号。它们大多只有 `AlwaysOpen | OnStack | RequiresUpdate | AllowSaving`，或者再加 `CustomRendering | AssignCursorToRenderer`；没有 `PausesGame`、`UsesMenuContext`、`Modal` 这类更接近“抢输入菜单”的标志。

## 2. 目标与非目标

### 目标

- 把“菜单名白名单 + 未知菜单默认进 `Menu`”改成更稳的上下文识别模型
- 新增独立策略文件，让用户可以显式配置“追踪 / 透传 / 映射到哪个上下文”
- 使用运行时 `IMenu::inputContext` 与 `menuFlags` 作为未知菜单分类的第一层证据
- 提供两个可切换的未知菜单默认策略：
  - `passthrough`：正式游玩默认
  - `track`：排查问题默认
- 保持原生已知菜单和现有 `InputContext` 语义不回归

### 非目标

- 不在这次方案里重做 `InputContext` 枚举本身
- 不在这次方案里调整 glyph attach 逻辑
- 不在这次方案里重构 `InputModalityTracker` 的 owner 策略
- 不在这次方案里解决每一个第三方菜单的专属绑定语义；这次只先解决“是否抢上下文、抢成什么上下文”

## 3. 核心决策

### 3.1 新增独立策略文件

新增：

- `config/DualPadMenuPolicy.ini`

运行时加载路径：

- `Data/SKSE/Plugins/DualPadMenuPolicy.ini`

该文件不和 `DualPadDebug.ini` 混用。原因：

- `DualPadDebug.ini` 是运行时 / 调试 toggle
- 菜单识别策略属于兼容性与行为策略，不应塞进 debug 开关

### 3.2 generic `Menu` 不再作为未知菜单的硬编码 fallback

`InputContext::Menu` 应改成显式授权语义，而不是未知菜单收容桶。

也就是说：

- 已知原生 generic 菜单，例如 `Main Menu`、`Loading Menu`、`Sleep/Wait Menu`，仍可显式映射到 `Menu`
- 未知菜单不再直接走 `Unknown -> Menu`

### 3.3 判定顺序

未知菜单的最终决策按以下顺序执行：

1. 用户策略文件显式覆盖
2. 内置已知菜单映射
3. 运行时分类器
4. `unknown_menu_policy` 默认策略

这条顺序要固定，不允许 debug fallback 绕过显式配置。

### 3.4 未知菜单默认策略可切换

策略文件中新增：

- `unknown_menu_policy = passthrough | track`

含义：

- `passthrough`
  - 未知菜单在规则不足以判断时默认透传
  - 这是正式游玩的推荐默认值
- `track`
  - 未知菜单在规则不足以判断时默认按 generic `Menu` 追踪
  - 这是排查问题时的推荐值

### 3.5 运行时分类器只做辅助判定，不单独成为真相源

分类器输入：

- `menuName`
- `IMenu::inputContext`
- `IMenu::menuFlags`
- 辅助字段：`depthPriority`、是否有 `uiMovie`、是否有 `fxDelegate`

分类器输出：

- `Track`
- `Passthrough`
- `Uncertain`

注意：

- 原生菜单的 `inputContext` / `menuFlags` 可信度高
- 第三方菜单不保证正确设置这些字段
- 因此运行时分类器只能是“高优先级证据”，不能取代显式配置

## 4. 策略文件草案

建议格式：

```ini
[Policy]
unknown_menu_policy = passthrough
log_unknown_menu_probe = true
log_unknown_menu_decision = true

[Track]
Main Menu = Menu
Loading Menu = Menu
InventoryMenu = InventoryMenu
Journal Menu = JournalMenu
FavoritesMenu = FavoritesMenu
MessageBoxMenu = MessageBoxMenu

[Ignore]
HUD Menu = true
Fader Menu = true
Cursor Menu = true
LoadWaitSpinner = true
```

语义：

- `[Track]`
  - `MenuName = ContextName`
  - 允许把未知菜单显式映射到 `Menu`、`JournalMenu`、`InventoryMenu` 等
- `[Ignore]`
  - `MenuName = true`
  - 无论 runtime 分类结果如何，都不抢上下文

约束：

- `ContextName` 必须复用 `BindingConfig.cpp` 当前可识别的上下文名
- 非法上下文名在加载时打 warning，并忽略该条规则

## 5. 运行时分类规则草案

### 5.1 明确 Track 的强信号

满足任一条件时，未知菜单优先判为 `Track`：

- `menuFlags` 含 `kPausesGame`
- `menuFlags` 含 `kUsesMenuContext`
- `menuFlags` 含 `kModal`
- `inputContext` 命中原生强业务语义：
  - `kConsole`
  - `kInventory`
  - `kFavorites`
  - `kMap`
  - `kJournal`
  - `kLockpicking`
  - `kFavor`

### 5.2 明确 Passthrough 的强信号

满足以下组合时，未知菜单优先判为 `Passthrough`：

- `menuFlags` 含 `kAlwaysOpen`
- `menuFlags` 含 `kOnStack`
- 不含 `kPausesGame`
- 不含 `kUsesMenuContext`
- 不含 `kModal`
- `inputContext` 为 `kNone` 或未知扩展值

最近抓到的这批 overlay 基本都落在这一类。

### 5.3 Uncertain 的处理

如果运行时信息不足以判定：

- 输出 `Uncertain`
- 再由 `unknown_menu_policy` 决定最终行为

这能满足两种模式：

- 正式游玩：`passthrough`
- 排查问题：`track`

## 6. 实施单元

### Unit 1: 菜单策略文件与配置解析

**目标**

- 引入独立菜单策略文件
- 支持 `unknown_menu_policy`、`Track`、`Ignore`、日志开关

**涉及文件**

- `src/input/MenuContextPolicy.h`（新）
- `src/input/MenuContextPolicy.cpp`（新）
- `src/input/BindingConfig.cpp`
- `xmake.lua`
- `config/DualPadMenuPolicy.ini`（新）

**关键决定**

- 不把这套配置塞进 `RuntimeConfig`
- `MenuContextPolicy` 作为独立组件，避免继续把 `InputContext.cpp` 变成配置解析器

**测试文件**

- `tests/input/MenuContextPolicyTests.cpp`（新）

**测试场景**

- 能正确读取 `unknown_menu_policy=passthrough`
- 能正确读取 `unknown_menu_policy=track`
- `Track` 段能把菜单名解析为目标 `InputContext`
- `Ignore` 段能正确标记透传
- 非法 `ContextName` 会被拒绝并打 warning
- 缺失配置文件时回退到安全默认值

### Unit 2: 运行时分类器

**目标**

- 从 `IMenu` 运行时对象读取 `inputContext` 与 `menuFlags`
- 给未知菜单产出 `Track / Passthrough / Uncertain`

**涉及文件**

- `src/input/MenuContextPolicy.h`
- `src/input/MenuContextPolicy.cpp`
- `src/input/InputContext.cpp`

**关键决定**

- 直接读取 `menu->menuFlags.underlying()` 与 `menu->inputContext.underlying()`
- 不使用 `IMenu.h` 中可疑的便捷 accessor 作为规则基础

**测试文件**

- `tests/input/MenuRuntimeClassificationTests.cpp`（新）

**测试场景**

- `PausesGame + UsesMenuContext` 菜单被判为 `Track`
- `AlwaysOpen + OnStack + 非 PausesGame` 菜单被判为 `Passthrough`
- `kFavorites / kJournal / kMap` 等原生 `inputContext` 被判为 `Track`
- `kNone` 或未知扩展 `inputContext` 在弱 flags 下被判为 `Passthrough` 或 `Uncertain`
- 最近 probe 到的特征集：
  - `0x00000942 + inputContext=18/19`
  - `0x00018842 + inputContext=18`
  均不应被判为 `Track`

### Unit 3: ContextManager 集成与 fallback 收口

**目标**

- 用新的判定顺序替换 `Unknown -> Menu`
- 让 generic `Menu` 改为显式命中语义

**涉及文件**

- `src/input/InputContext.cpp`
- `src/input/InputContext.h`

**关键决定**

- `MenuNameToContext()` 不再单独承担未知菜单 fallback 责任
- `OnMenuOpen()` 需要先问策略层“是否追踪、追踪成什么上下文”
- 仅当策略层明确返回 `Track(Menu)` 时，未知菜单才进入 generic `Menu`

**测试文件**

- `tests/input/InputContextMenuTrackingTests.cpp`（新）

**测试场景**

- 已知原生菜单仍映射到现有上下文
- 显式 `Ignore` 菜单不会进 `_menuStack`
- 显式 `Track` 菜单会按指定上下文进入 `_menuStack`
- 未知菜单在 `unknown_menu_policy=passthrough` 下不进入 `_menuStack`
- 未知菜单在 `unknown_menu_policy=track` 下进入 generic `Menu`
- 菜单关闭后 `_menuStack` 恢复行为与当前一致

### Unit 4: 诊断日志与模式切换

**目标**

- 让“正式游玩”和“排查问题”使用同一套实现，只切策略
- 保留足够日志，能解释每个未知菜单的分类过程

**涉及文件**

- `src/input/MenuContextPolicy.cpp`
- `src/input/InputContext.cpp`
- `config/DualPadMenuPolicy.ini`

**关键决定**

- 保留 `Unknown menu runtime probe`
- 新增最终决策日志：
  - `classified as Track`
  - `classified as Passthrough`
  - `classified as Uncertain -> policy passthrough`
  - `classified as Uncertain -> policy track`

**测试文件**

- `tests/input/MenuContextPolicyLoggingTests.cpp`（新）

**测试场景**

- `log_unknown_menu_probe=false` 时不打 probe 日志
- `log_unknown_menu_decision=true` 时会输出最终分类日志
- `unknown_menu_policy` 切换后，仅 fallback 结果变化，已知菜单和显式配置不变

## 7. 测试与验证策略

### 7.1 Characterization-first 验证

这次变更先以行为回归为主，不先追求 UI 全覆盖自动化。

优先验证：

- 游戏启动后有第三方 HUD overlay 常驻时，仍能保持 `Gameplay`
- 右摇杆在跑图 / 战斗里恢复 `Game.Look`
- `MessageBoxMenu`、`InventoryMenu`、`Journal Menu`、`FavoritesMenu` 继续正确抢上下文
- 把 `unknown_menu_policy` 切成 `track` 后，能稳定复现“未知菜单全进 `Menu`”的旧行为

### 7.2 建议新增的手工验证矩阵

建议新增：

- `docs/verification/menu_context_runtime_policy_matrix_zh.md`

场景至少包括：

- 仅原生菜单
- 原生菜单 + 已知 overlay
- 原生菜单 + 未知 overlay
- `unknown_menu_policy=passthrough`
- `unknown_menu_policy=track`
- `Track` 覆盖未知菜单
- `Ignore` 覆盖未知菜单

## 8. 风险与回滚

### 风险

- 某些未知但确实该抢上下文的 mod 菜单，第一次可能被判成 passthrough
- 第三方菜单可能把 `menuFlags/inputContext` 设得很奇怪，导致 runtime 分类不稳定
- 如果把规则写死在 `InputContext.cpp`，后续会再次回到补丁森林

### 缓解

- 显式配置优先级最高
- `unknown_menu_policy=track` 保留为快速排查模式
- probe 和最终决策日志默认可开

### 回滚

- 行为级回滚只需把 `unknown_menu_policy` 切回 `track`
- 若分类器本身有问题，可临时退回“只用显式配置 + 已知菜单映射”

## 9. 推荐顺序

1. 先做 Unit 1 和 Unit 2
   - 先把策略文件和 runtime 分类器独立出来
2. 再做 Unit 3
   - 把 `Unknown -> Menu` 从 `ContextManager` 里拿掉
3. 最后做 Unit 4
   - 把诊断日志和模式切换收完整

这样做的好处是：

- 先建立新判定层
- 再替换旧 fallback
- 最后再优化可观测性

## 10. 计划完成标准

达到以下条件时，这份方案算完成：

- 未知菜单不再默认进入 generic `Menu`
- 有独立 `DualPadMenuPolicy.ini`
- 正式游玩和排查问题可以只靠配置切换默认策略
- 最近这批 overlay 在正式模式下不会再把 gameplay 钉进 `Menu`
- 右摇杆跑图 / 战斗视角恢复
- 已知原生菜单上下文不回归
