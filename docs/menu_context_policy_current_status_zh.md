# 菜单上下文策略当前实现状态

本文只描述仓库里**已经落地**的菜单上下文策略实现，不再复述早期“所有未知菜单都压回 generic `Menu`”的旧做法。

## 一句话结论

当前菜单上下文识别由三部分组成：

- [`MenuContextPolicy`](../src/input/MenuContextPolicy.h)
- [`InputContextNames`](../src/input/InputContextNames.h)
- [`DualPadMenuPolicy.ini`](../config/DualPadMenuPolicy.ini)

它们共同负责回答两个问题：

1. 某个菜单是否应该抢占 DualPad 的逻辑上下文。
2. 如果应该抢占，它应该落到哪个 `InputContext`。

## 代码入口

当前直接相关的模块是：

- [`src/main.cpp`](../src/main.cpp)
  - 在 `kDataLoaded` 时加载 `MenuContextPolicy`，然后注册 `ContextEventSink`。
- [`src/input/MenuContextPolicy.h`](../src/input/MenuContextPolicy.h)
- [`src/input/MenuContextPolicy.cpp`](../src/input/MenuContextPolicy.cpp)
  - 负责显式规则、已知菜单表和 runtime 分类器。
- [`src/input/InputContextNames.h`](../src/input/InputContextNames.h)
- [`src/input/InputContextNames.cpp`](../src/input/InputContextNames.cpp)
  - 负责把配置值、菜单别名和桥接字符串解析成统一的 `InputContext`。
- [`src/input/ContextEventSink.cpp`](../src/input/ContextEventSink.cpp)
  - 菜单开关事件进入这里，再由 `ContextManager` 和 `InputModalityTracker` 消费。
- [`config/DualPadMenuPolicy.ini`](../config/DualPadMenuPolicy.ini)
  - 当前默认策略文件。

## 运行时顺序

当前启动顺序是：

1. `main.cpp` 在 `kDataLoaded` 调 `RuntimeConfig::Load()`
2. `main.cpp` 调 `MenuContextPolicy::Load()`
3. `main.cpp` 注册 `ContextEventSink`
4. `ContextEventSink` 在菜单打开时通知 `ContextManager`
5. `InputModalityTracker`、`ScaleformGlyphBridge` 等后续模块再消费已经确定的当前上下文

这里的重点是：

- `MenuContextPolicy` 只负责“菜单该不该进入逻辑上下文、进入哪个上下文”。
- 它不直接负责 UI owner、glyph 渲染或页面级动作执行。

## 决策优先级

`MenuContextPolicy::ResolveMenuTracking(...)` 当前按下面顺序决策：

1. `Ignore` 显式忽略规则
2. `Track` 显式跟踪规则
3. `KnownMenuNameToContext(...)` 的内建已知菜单表
4. `ClassifyRuntimeMenu(...)` 的 runtime 分类
5. `unknown_menu_policy` 的兜底策略

也就是说，当前优先级是：

`显式配置 > 已知菜单表 > runtime 启发式 > 全局未知菜单策略`

这能避免两类问题：

- 某些 overlay 菜单误抢上下文
- 某些真正需要追踪的未知菜单只能继续压回 generic `Menu`

## runtime 分类器当前看什么

当菜单名不在已知表里时，`MenuContextPolicy` 会先抓一份 `MenuRuntimeSnapshot`，当前字段包括：

- `inputContextValue`
- `menuFlagsValue`
- `depthPriority`
- `hasMovie`
- `hasDelegate`

然后再做启发式分类：

- 命中 `PausesGame / UsesMenuContext / Modal` 等强信号时，优先判成 `Track`
- 命中 `AlwaysOpen + OnStack` 且没有菜单语义 flag，同时 `inputContext` 为 `kNone` 或未知值时，优先判成 `Passthrough`
- 其余情况判成 `Uncertain`

如果 `runtimeSnapshot` 已经带着强菜单型 `inputContext`，还会尝试映射到专用 `InputContext`，例如：

- `kInventory -> InventoryMenu`
- `kFavorites -> FavoritesMenu`
- `kJournal -> JournalMenu`

## 当前默认配置

[`config/DualPadMenuPolicy.ini`](../config/DualPadMenuPolicy.ini) 当前默认是：

- `unknown_menu_policy = passthrough`
- `log_unknown_menu_probe = true`
- `log_unknown_menu_decision = true`

默认忽略的菜单主要是常见 overlay / widget：

- `HUD Menu`
- `Cursor Menu`
- `Tutorial Menu`
- `TrueHUD`
- `STBActiveEffects`
- 各类状态栏 widget

这意味着当前正式口径偏保守：

- 未知菜单默认先不抢上下文
- 真要纳入逻辑上下文，再通过 `Track` 显式收编

## `InputContextNames` 当前职责

`InputContextNames` 现在不是简单的字符串工具，它承担了两类统一口径：

1. 配置解析口径
   - 例如 `FavoritesMenu`、`Favorites Menu`、`Creation Club Menu`
2. 域判断口径
   - `IsMenuOwnedContext(...)`
   - `IsGameplayDomainContext(...)`

这样做的实际价值是：

- `DualPadBindings.ini`
- `DualPadMenuPolicy.ini`
- `ScaleformGlyphBridge`

都能共享同一套上下文名字和别名规则，而不是各自维护一份字符串表。

## 与其它模块的边界

### 与 `ContextEventSink`

`ContextEventSink` 负责拿到权威菜单开关事件。

`MenuContextPolicy` 负责解释“这个菜单该不该算逻辑菜单上下文”。

### 与 `InputModalityTracker`

`InputModalityTracker` 关心的是：

- `PresentationOwner`
- 菜单平台切换
- 键鼠/手柄 lease

它不负责决定未知菜单是否属于 `InventoryMenu`、`JournalMenu` 这类逻辑上下文。

### 与 `ScaleformGlyphBridge`

`ScaleformGlyphBridge` 只消费已经确定的 `InputContext` 去查绑定和图标。

它不负责识别未知菜单，也没有页面级 broker。

## 当前边界与限制

当前实现仍有几个明确边界：

- `Main Menu`、`Loading Menu`、`Sleep/Wait Menu` 等仍统一落到 generic `Menu`
- runtime 分类器是启发式，不是游戏内部真相源镜像
- `FavoritesMenu` 现在只解决“进入 `FavoritesMenu` 上下文”，不代表已经有页面级动作语义或 execution broker
- 策略文件只解决“跟踪/忽略/映射到哪个上下文”，不解决 glyph 资源或页面渲染问题

## 相关文档

- [`current_input_pipeline_zh.md`](./current_input_pipeline_zh.md)
  - 当前输入主链路，以及菜单/表现层侧支放在哪。
- [`main_menu_glyph_current_status_zh.md`](./main_menu_glyph_current_status_zh.md)
  - `ScaleformGlyphBridge` 当前已落地的动态图标能力边界。
- [`plans/menu_context_runtime_policy_plan_zh.md`](./plans/menu_context_runtime_policy_plan_zh.md)
  - 这套策略的实施计划记录。
- [`verification/menu_context_runtime_policy_matrix_zh.md`](./verification/menu_context_runtime_policy_matrix_zh.md)
  - 菜单策略的验证矩阵。
