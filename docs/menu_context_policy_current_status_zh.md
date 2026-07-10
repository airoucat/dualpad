# 菜单上下文与刷新当前状态

本文只描述 `input_v2` current mainline。旧 `MenuContextPolicy`、`ContextManager`、`InputModalityTracker` 和 generic menu fallback 仅是历史实现，不再拥有当前 runtime authority。

## 当前主链

菜单事实按以下路径发布：

`UiMenuObserver -> MenuInstanceRegistry -> ContextResolver -> PublishedPresentationState -> SkyrimCompatibilitySurface / PromptRuntimeOwner`

职责边界：

- `UiMenuObserver` 获取当前 stack 的 observed nodes 与 completeness。
- `MenuInstanceRegistry` 维护 stable instance ID、pointer/movie identity 和 `menuStackRevision`。
- `ContextResolver` 使用 compiled context catalog 解析 `UiContextId`、action set stack、presentation policy，并发布 by-value snapshot。
- `PresentationProjection` 决定 owner/cursor、device family、refresh eligibility 和目标菜单身份。
- `SkyrimCompatibilitySurface` 只消费已发布状态，不跨线程读取 registry 拼装第二份 truth。

`config/DualPadMenuPolicy.ini` 仍是 checked-in 配置输入，但它通过 current config/catalog compiler 进入 input-v2；不再由旧 policy singleton 独立裁决 runtime context。

## 目标绑定的菜单刷新

refresh request 捕获同一份 `PublishedPresentationState` 中的：

- menu name；
- stable instance ID；
- menu pointer / movie pointer；
- menu stack revision；
- context revision；
- presentation epoch；
- requested dirty flags。

verified UI task 只调用 `UI::GetMenu(capturedName)` 获取一个 target，并在调用前再次验证 committed target、allowlist、pointer/movie、`_root` 与 callback readiness。

执行优先级：

1. 若存在 `_root.DualPad_OnPresentationChanged`，调用 DualPad-owned callback。
2. callback 缺失但 target 位于明确安全 allowlist 且 root ready 时，只调用该 target 的一次 `RefreshPlatform()`。
3. null movie/root 进入 bounded deferred/held retry。
4. target replaced、revision/epoch 变化或 pointer mismatch 取消为 superseded。
5. Loading/Fader/MessageBox/Favorites 等 denied target 不刷新。

任何路径都不会遍历 `RE::UI::menuStack` 或刷新其它菜单。

## Unknown / degraded menu

- observer partial/unavailable 不产生稳定 refresh target。
- degraded identity 不允许 target-bound refresh，也不回退到名称猜测或全栈扫描。
- unknown menu 的 action/context 行为由 compiled catalog/policy 合同决定；presentation 层不得重新分类。
- `FavoritesMenu` 的页面级 SWF workspace 当前不在 repo 内，不得从 context tracking 推断页面 broker 已恢复。

## Favorites 边界

`Game.Favorites` synthetic XInput DPadUp 的 native route 继续默认关闭。ContextResolver 能识别 Favorites context、prompt/action facts 仍可维护，但这不证明 native 打开链或 live SWF 安全。

完成 matching dump/IDA、physical/synthetic A/B、vanilla/profile 循环和 soak 前，禁止把菜单识别成功描述为 crash 已修复。

## 验证入口

- `DualPadContextResolverTests`
- `DualPadPresentationProjectionTests`
- `DualPadInputV2Tests`
- [runtime_concurrency_contract.md](runtime_concurrency_contract.md)
- [testing/rc20_runtime_validation.md](testing/rc20_runtime_validation.md)
