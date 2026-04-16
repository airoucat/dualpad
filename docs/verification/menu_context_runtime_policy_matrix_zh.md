# 菜单上下文运行时策略验证矩阵

## 目标

验证以下行为在当前实现中成立：

- 未知菜单默认不再直接进入 generic `Menu`
- `unknown_menu_policy=passthrough` 时，未知 overlay 不抢占 gameplay 上下文
- `unknown_menu_policy=track` 时，可回到旧的“未知菜单统一进 `Menu`”排查模式
- 已知原生菜单上下文不回归
- 显式 `[Track]` / `[Ignore]` 规则能覆盖 runtime 分类结果

## 前置准备

1. 确认已部署：
   - `DualPad.dll`
   - `DualPadBindings.ini`
   - `DualPadMenuPolicy.ini`
2. 打开日志：
   - `DualPadMenuPolicy.ini` 中保留
     - `log_unknown_menu_probe = true`
     - `log_unknown_menu_decision = true`
3. 主要观察日志：
   - `C:/Users/xuany/Documents/My Games/Skyrim Special Edition/SKSE/DualPad.log`

## 场景矩阵

### 场景 1：仅原生菜单

- 配置：
  - `unknown_menu_policy = passthrough`
- 操作：
  - 依次打开 `Main Menu`、`InventoryMenu`、`Journal Menu`、`FavoritesMenu`、`MessageBoxMenu`
- 预期：
  - `Main Menu` / `Loading Menu` 进入 `Menu`
  - `InventoryMenu` 进入 `InventoryMenu`
  - `Journal Menu` 进入 `JournalMenu`
  - `FavoritesMenu` 进入 `FavoritesMenu`
  - `MessageBoxMenu` 进入 `MessageBoxMenu`
  - 菜单关闭后上下文恢复正常

### 场景 2：原生菜单 + 已知常驻 overlay

- 配置：
  - `unknown_menu_policy = passthrough`
  - `[Ignore]` 保持默认 shipped 列表
- 操作：
  - 进入正常 gameplay
  - 让 `HUD Menu` / `TrueHUD` / `STBActiveEffects` 等常驻
  - 观察右摇杆视角与 gameplay 动作
- 预期：
  - gameplay 保持 `Gameplay`
  - `Game.Look` 正常
  - 日志可见 passthrough / ignore 决策，不应看到这些 overlay 把上下文钉进 `Menu`

### 场景 3：原生菜单 + 未知第三方 overlay

- 配置：
  - `unknown_menu_policy = passthrough`
- 操作：
  - 启用一个不在 `[Ignore]`、也不在 `[Track]` 的第三方 overlay
  - 进入 gameplay，观察其常驻时的输入表现
- 预期：
  - 若 runtime flags 呈现 `AlwaysOpen + OnStack + 非 PausesGame/UsesMenuContext/Modal`
    - 该菜单被判为 `Passthrough`
  - gameplay 不被切到 `Menu`
  - 日志包含：
    - `Unknown menu runtime probe ...`
    - `classified as Passthrough`

### 场景 4：切换为排查模式

- 配置：
  - `unknown_menu_policy = track`
- 操作：
  - 保持同一未知第三方 overlay 常驻
- 预期：
  - 同一未知菜单在 `Uncertain` 时回退为 `policy track`
  - 日志包含：
    - `classified as Uncertain -> policy track`
  - 可复现旧行为：未知菜单进入 generic `Menu`

### 场景 5：显式 `[Track]` 覆盖未知菜单

- 配置：
  - `unknown_menu_policy = passthrough`
  - `[Track]`
    - `SomeUnknownMenu = JournalMenu`
- 操作：
  - 打开 `SomeUnknownMenu`
- 预期：
  - 无论 runtime 分类结果如何，都按 `JournalMenu` 入栈
  - 日志显示显式 track 决策

### 场景 6：显式 `[Ignore]` 覆盖未知菜单

- 配置：
  - `unknown_menu_policy = track`
  - `[Ignore]`
    - `SomeUnknownMenu = true`
- 操作：
  - 打开 `SomeUnknownMenu`
- 预期：
  - 即使默认策略为 `track`，该菜单也不进入 `_menuStack`
  - 日志显示显式 ignore 决策

## 建议记录项

- 菜单名
- `inputContext` 原始值
- `menuFlags` 原始值与 flag 描述
- 最终决策：
  - `Track -> <Context>`
  - `Passthrough`
  - `Uncertain -> policy passthrough`
  - `Uncertain -> policy track`
- gameplay 是否保持 `Game.Look`

## 回归判定

满足以下条件可视为本轮通过：

- 未知 overlay 在 `passthrough` 模式下不再把 gameplay 钉进 `Menu`
- `track` 模式下仍能作为排查回退路径
- 已知原生菜单上下文保持原行为
- 显式 `[Track]` / `[Ignore]` 规则优先级高于 runtime 分类与默认策略
