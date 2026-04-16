# 绑定 / 动作语义层审查记录（2026-04-10）

## 审查范围

本次只看输入链路里“绑定 / 动作语义”这一层：

- `src/input/BindingConfig.*`
- `src/input/BindingManager.*`
- `src/input/Trigger.h`
- `src/input/Action.h`
- `src/input/InputContext.h`
- `src/input/InputContextNames.*`

必要时把下游消费者一起交叉核对：

- `src/input/glyph/ScaleformGlyphBridge.cpp`
- `src/input/backend/ActionBackendPolicy.cpp`
- `src/input/backend/FrameActionPlanner.cpp`
- `config/DualPadBindings.ini`

## 前提假设

这次审查默认按当前仓库现状判断：

- 重点不是事件生成，而是“触发器如何被配置、注册、反查、再解释成动作”
- 重点不是执行后端，而是“动作 ID 在绑定层是否具有稳定、可验证、可诊断的语义”
- 当前项目仍在持续调整动作表面，尤其要防：
  - 同一 action 多重绑定下的反查不稳定
  - 旧 action 名 / typo 被静默接受
  - 配置语义和用户直觉不一致

## 结论摘要

- 这一层没有看到 `P0`
- 这轮有两个 `P2`：
  - action 反查在多重绑定下不稳定
  - 配置文件接受任意 action 字符串，但未知 action 后续会被静默丢弃
- 另外有一个 `P3`：
  - 当前配置语义实际上是“声明 + fallback 叠加”，不是很多人直觉里的“完整声明式覆盖”

## 具体问题

### 1. `GetTriggerForAction(...)` 在多重绑定下是不稳定反查

`BindingManager::GetTriggerForAction(...)` 当前实现是：

- 遍历当前 context 的 `unordered_map`
- 找到第一个 `action == actionId` 的 trigger 就直接返回

这个语义在“同一 action 只有一个绑定”时没问题，但当前仓库配置里已经有多处一对多：

- `Game.Move` 同时绑在 `LeftStickX / LeftStickY`
- `Game.Look` 同时绑在 `RightStickX / RightStickY`
- `Menu.LeftStick` 同时绑在 `LeftStickX / LeftStickY`
- `Favorites.LeftStick` 同时绑在 `LeftStickX / LeftStickY`
- `Book.PreviousPage` 同时绑在 `Button:DpadLeft` 和 `Gesture:TpSwipeLeft`

这会带来两个问题：

1. 反查结果不是稳定语义，只是当前哈希表的偶然第一命中
2. 下游如果只支持某些 trigger 类型，就可能把“其实可显示”的 action 反查成“不可显示”的 trigger

当前仓库里，`ScaleformGlyphBridge::ResolveActionToken(...)` 正是这么用的。

因此像 `Book.PreviousPage` 这种 action：

- 如果反查先拿到 `DpadLeft`，UI 可以显示 token
- 如果反查先拿到 `TpSwipeLeft`，`TriggerToButtonArtToken(...)` 会直接返回空

于是 glyph/prompt 会变成“有时有图标，有时没有，取决于反查拿到了哪条绑定”。

受影响文件：

- `src/input/BindingManager.cpp`
- `src/input/glyph/ScaleformGlyphBridge.cpp`
- `config/DualPadBindings.ini`

当前判断：

- 这是 `P2`
- 它不是 hash 容器本身的 bug
- 而是“动作 -> trigger 反查”缺少稳定优先级规则

### 2. 配置层完全不校验 action ID，未知动作会在后面被静默吞掉

`BindingConfig::ParseBinding(...)` 现在的行为是：

- trigger 只要能解析成功
- `value` 就原样存进 `binding.actionId`

它并不会检查：

- 这个 action 是否在正式 `Action` surface 里
- 是否能被 `ActionBackendPolicy` / native descriptor / plugin surface 接住

而后续运行时里：

- `ActionBackendPolicy::Decide(...)` 对未知 action 返回 `PlannedBackend::None`
- `FrameActionPlanner::PlanResolvedEvent(...)` 对不可 dispatch 的 action 直接 `return false`

结果就是：

- typo action
- 旧文档里的过期 action
- 已经从正式支持面撤掉的 action

都可以被成功加载进绑定表，但触发后不会真正生成计划动作。

更糟的是，这条路径默认没有“这是未知 action surface”的强诊断；表现通常只是：

- 绑定存在
- 输入命中
- 但动作什么也不做

当前项目里这是实风险，因为：

- 动作表面最近一直在变
- `Favorites` / `Open*` / combo-native 这类边界还在调整
- 很容易出现“配置里看起来像对的字符串，实际上已经不在正式支持面”

受影响文件：

- `src/input/BindingConfig.cpp`
- `src/input/backend/ActionBackendPolicy.cpp`
- `src/input/backend/FrameActionPlanner.cpp`

当前判断：

- 这是 `P2`
- 它不是执行层问题
- 而是绑定层没有把“动作语义真相源”收紧

### 3. 当前配置文件语义实际上是“叠加 fallback”，不是完整覆盖

`BindingConfig::ParseIniFile(...)` 在成功读完 INI 之后，仍然会无条件调用：

- `BindingManager::ApplyStandardFallbackBindings()`

而 fallback 的加入条件只是：

- 当前 context 下“不存在同一 trigger 的绑定”

这意味着用户配置的实际语义是：

- “声明我写的绑定”
- 然后再把所有没被同 trigger 显式覆盖的标准 fallback 补回来

它不是很多人直觉里的：

- “INI 里写了什么，最终就只剩什么”

这个语义会带来两个后果：

1. 省略某个默认 trigger 并不等于 unbind
2. 如果用户想把某个默认动作彻底撤掉，当前没有显式的“删除绑定”语法

这不一定是错，但它属于当前绑定层的隐含规则，而且配置头注释没有把这件事讲透。

受影响文件：

- `src/input/BindingConfig.cpp`
- `src/input/BindingManager.cpp`

当前判断：

- 这是 `P3`
- 主要是配置语义不够显式
- 对日常主线不是立刻炸，但会持续制造“为什么我明明没写，这个键还在生效”的理解成本

## 非阻塞但值得记住的观察

### 1. Context 名称解析目前比旧版本集中一些

当前 `BindingConfig` 已经不再自己维护一套 `StringToContext(...)` 真相源，而是走：

- `ParseInputContextName(...)`

这比旧状态好，至少避免了“配置解析”和“运行时 menu/context 解析”继续双轨漂移。

这轮没有把 context alias 漂移列为问题。

### 2. 动作表面虽然集中在 `Action.h`，但它仍不是正式校验入口

现在 `Action.h` 更像：

- 约定集
- 共享常量列表

还不是：

- 配置加载时的强校验白名单

后续如果要进一步收紧动作语义，优先级应该放在：

- “配置层能不能认出未知 action”

而不是单纯继续往 `Action.h` 里加常量。

## 建议调整顺序

### 第一批：先让 action 反查稳定下来

1. 给 `GetTriggerForAction(...)` 增加稳定优先级
2. 明确同一 action 多重绑定时谁是 primary binding
3. 不要再让 glyph/prompt 依赖 `unordered_map` 的偶然命中顺序

### 第二批：给 action surface 增加装载期校验

1. 配置加载时识别未知 / 过期 action
2. 至少输出强诊断
3. 最好把“未知 action 是否允许加载”变成显式策略

### 第三批：把 fallback 语义写透，或补 unbind 机制

1. 如果决定保留 additive fallback，就把规则写进配置文档
2. 如果希望配置更可预期，就加显式 unbind / disable 语法
3. 至少别继续让“省略 = 仍然生效”保持隐式

## 当前建议口径

后续如果有人继续改这一层，建议默认接受这两个原则：

- `BindingConfig` 不该只是“字符串搬运工”
- `BindingManager` 不该把“动作 -> trigger 反查”当成无语义的容器遍历

换句话说：

- 绑定层不只是把配置存起来
- 它应该承担“动作语义稳定化”和“配置诊断前移”的职责
