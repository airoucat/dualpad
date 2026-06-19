# RC20 菜单平台刷新方案外部审查材料

本文用于交给网页 GPT 审查当前 DualPad RC20 热修方案。重点不是请它重新写补丁，而是判断当前建模是否足够正式，以及是否应该进一步重构为更清晰的 `MenuRefreshEligibility` 合同。

## 可直接复制的提示词

```text
你现在作为 C++ / Skyrim SKSE / 输入运行时架构审查者，帮我审查 DualPad 项目的一次 RC20 热修。

仓库：airoucat/dualpad
当前分支：codex/dp5-rc20-menu-native-hotfix
重点文件：
- src/input_v2/context/ContextCatalog.cpp
- src/input_v2/context/ContextResolver.cpp
- src/input_v2/presentation/PresentationProjection.h
- src/input_v2/presentation/PresentationProjection.cpp
- src/input_v2/presentation/SkyrimCompatibilitySurface.cpp
- tests/input_v2/PresentationProjectionTests.cpp
- config/DualPadDebug.ini

背景：
1. 主菜单中按 Triangle / Y 触发 Menu.Confirm 后，旧问题是首个按下会导致焦点下移。
2. IDA / 实机日志证明 DualPad 没有发 DPadDown：Triangle raw 是 mask=0x00000008，生成 Menu.Confirm，native 输出 xinputButtons=0x1000；没有 Menu.ScrollDown，也没有 xinputButtons=0x0002。
3. IDA 还证明 Skyrim 菜单平台刷新会走 menu->RefreshPlatform() -> _root.SetPlatform(...)，因此旧基线需要在 presentation owner / policy 改变后刷新已打开菜单的平台状态。
4. 旧链路中 InputModalityTracker 会触发 RefreshMenus()；后续重构后这条 RefreshPlatform 调用断开，恢复 RefreshMenusIfNeeded() 后主菜单功能曾恢复。
5. 最近又出现 Favorites 打开时闪退。为了防止 Gameplay / degraded context 下排队的 UI task 在菜单半初始化时调用 RefreshPlatform，补丁曾把 UnknownTrackedMenu 排除出 refresh，但这导致主菜单首次下移回归。
6. 后来发现 ContextCatalog 把 Main Menu、Credits Menu、Loading Menu 等稳定 generic Menu 映射到 UiContextId::UnknownTrackedMenu。也就是说 UnknownTrackedMenu 不等同于 degraded，它同时承担 stable generic Menu 与 degraded fallback 两种语义。

当前补丁：
- PublishedPresentationState 增加：
  - menu::ObserverCompleteness menuObserverCompleteness
  - bool menuIdentityDegraded
- PresentationProjection 从 ResolvedContextSnapshot 转发这两个字段。
- SkyrimCompatibilitySurface 的刷新 gate 改为：
  - uiContextId 必须是 menu presentation context（包含 UnknownTrackedMenu）
  - menuObserverCompleteness 必须是 Complete
  - menuIdentityDegraded 必须是 false
- DoRefreshMenus() 执行时再次检查状态；如果不稳定就清 _refreshQueued 并 debug log。
- 遍历 RE::UI::menuStack 时跳过 nullptr 和 uiMovie == nullptr，避免对半初始化 menu 调 RefreshPlatform。
- DualPadDebug.ini 关闭高频 input/action/route trace，避免右摇杆卡顿和 queue backlog。

请重点判断：
1. 当前把 menuObserverCompleteness / menuIdentityDegraded 塞进 PublishedPresentationState 是否合理？是否泄漏 context 层细节到 presentation 层？
2. SkyrimCompatibilitySurface 里维护 IsMenuPresentationContext(UiContextId) 白名单是否不够好？是否应该改成上游发布明确的 MenuRefreshEligibility enum？
3. 更好的建模是否应为：
   enum class MenuRefreshEligibility {
       NotMenu,
       StableTrackedMenu,
       ObserverPartial,
       ObserverUnavailable,
       IdentityDegraded,
       PassthroughOverlay
   };
   然后 SkyrimCompatibilitySurface 只消费 StableTrackedMenu，而不再知道 UiContextId 枚举？
4. 或者是否应拆分 UiContextId，把 stable generic Menu 与 degraded fallback 分开，例如 GenericMenu / DegradedUnknownMenu？
5. 对 Favorites 闪退，跳过 nullptr / uiMovie == nullptr 与执行时二次检查是否是正确的 hardening？如果 queued refresh 执行时 unstable，应该 clear queue、retry，还是保留 pending epoch？
6. DualPadDebug.ini 默认关闭高频日志是否应该和本补丁一起提交，还是应该独立为 chore / playtest config？
7. 请指出当前方案中最可能隐藏的 bug、竞态或长期维护风险，并给出你推荐的最小正式化改法和测试清单。

约束：
- 不做 haptics / vibration。
- 不做图标视觉资源。
- 不做 release packaging 大改。
- 不恢复 legacy InputModalityTracker authority。
- 不改变 runtime phase。
- 不用硬编码 Triangle / Cross 特例绕过 resolver。
- 不吞掉真实 translate_failed。
- 保持 input_v2 是正式 runtime mainline。

请输出：
- 结论：当前补丁是否可接受合入？
- 推荐：保持当前方案、改成 MenuRefreshEligibility、还是拆 UiContextId？
- 理由：按边界、语义、竞态、安全性分别说明。
- 最小后续 PR 计划。
- 必须补的测试。
```

## 当前问题摘要

本轮要审查的是菜单平台刷新（menu platform refresh）和菜单上下文稳定性边界。

主菜单首次下移不是按键翻译错成 Down。IDA 与实机日志都指向：`Menu.Confirm` 输出的是 native A / Accept (`0x1000`)，不是 D-pad Down (`0x0002`)。旧基线依赖 `menu->RefreshPlatform()` 把当前 presentation owner 同步给已打开菜单；这条链断开后，主菜单首次 `Menu.Confirm -> XInput A` 会被 SWF / Scaleform 侧以不正确的平台状态消费。

Favorites 打开闪退的怀疑点不同：Gameplay 或 degraded context 下排队的异步 UI refresh 可能在菜单半初始化阶段执行，遍历 menu stack 并调用 `RefreshPlatform()`。因此需要防止 unstable / degraded 状态触发菜单刷新，但不能把稳定 generic Menu 一起挡掉。

## 关键事实

### IDA / 游戏侧事实

- IDA 证据显示菜单平台刷新会进入类似 `sub_140ECD970` 的路径：
  - 检查当前是否使用 gamepad 平台。
  - 调用菜单的 `_root.SetPlatform(...)`。
  - 更新菜单自己的平台状态位。
- 这说明 `menu->RefreshPlatform()` 是正确方向，不是临时绕路。
- IDA 证据显示 gameplay 侧 `BSWin32GamepadDevice::Poll` 仍走 `XInputGetState` current-state 语义。
- controlmap / IDA mask 证据：
  - `0x1000` = Cross / A / Accept。
  - `0x0002` = D-pad Down。
  - `0x8000` = Triangle / Y。

### 实机日志事实

- Triangle raw：`mask=0x00000008`。
- Runtime plan：`RuntimePlanAction action=Menu.Confirm phase=Press`。
- Native output：`xinputButtons=0x1000`。
- 未出现：
  - `Menu.ScrollDown`
  - `xinputButtons=0x0002`
  - D-pad Down raw。
- `suppress_menu_confirm_native_output_probe=true` 时，下移消失，但 Triangle 也没有确认功能。
- `route_menu_confirm_keyboard_enter_probe=true` 时，功能可执行且不下移，但这不是正式方案，因为它改走 keyboard Enter 路径。
- `trace_menu_confirm_focus_probe` 一度否定了“native A 同帧触发 presentation owner 翻转”的假设；后续旧基线对比发现问题更像是已打开菜单没有收到平台刷新。

### 旧基线差异

旧链路中 `InputModalityTracker` 会触发：

```text
ShouldRefreshMenus()
  -> RefreshMenus()
  -> SKSE UI task
  -> menu->RefreshPlatform()
  -> _root.DualPad_OnPresentationChanged()
```

后续 runtime closeout 删除旧 tracker authority 时，`input_v2` 保留了 `SkyrimCompatibilitySurface::ShouldRefreshMenus()` / refresh 语义，但一度没有把它接回 stable presentation publish 路径。恢复 `DualPadRuntime::PublishStablePresentationSurface() -> RefreshMenusIfNeeded()` 后，主菜单功能恢复过。

## 当前代码片段

### `ContextCatalog.cpp`

`UiContextId::UnknownTrackedMenu` 同时代表 generic `Menu` 兼容上下文。它包含稳定的主菜单名称：

```cpp
entry({ UiContextId::UnknownTrackedMenu, "Menu", Legacy::Menu,
    kMenuBase,
    { "UnknownTrackedMenuLayer" },
    { kMenuBase, "UnknownTrackedMenuLayer" },
    {
        "Menu",
        "Main Menu",
        "Loading Menu",
        "Credits Menu",
        "Crafting Menu",
        "TitleSequence Menu",
        "Sleep/Wait Menu",
        "Kinect Menu",
        "SafeZoneMenu",
        "StreamingInstallMenu",
    },
    {
        "Main Menu",
        "Loading Menu",
        "Credits Menu",
        "Crafting Menu",
        "TitleSequence Menu",
        "Sleep/Wait Menu",
        "Kinect Menu",
        "SafeZoneMenu",
        "StreamingInstallMenu",
    } }),
```

因此不能把 `UnknownTrackedMenu` 直接等同于 degraded unknown。

### `ContextResolver.cpp`

resolver 在以下情况也会输出 `UnknownTrackedMenu`：

```cpp
auto resolved = ContextCatalog::ResolveMenuName(catalog, top.menuName);
if (!resolved || top.identityQuality == menu::MenuIdentityQuality::DegradedIdentity) {
    resolved = UiContextId::UnknownTrackedMenu;
}
next.uiContextId = *resolved;

// observer partial / unavailable
next.hostMode = HostMode::Menu;
next.identityQuality = menu::MenuIdentityQuality::DegradedIdentity;
next.menuIdentityDegraded = true;
next.uiContextId = UiContextId::UnknownTrackedMenu;
```

这导致 `UnknownTrackedMenu` 有两种语义：

- 稳定 generic Menu，例如 Main Menu。
- degraded / partial / unresolved fallback。

### 当前 `PresentationProjection` 改动

```cpp
struct PublishedPresentationState
{
    context::UiContextId uiContextId{ context::UiContextId::None };
    menu::ObserverCompleteness menuObserverCompleteness{ menu::ObserverCompleteness::Complete };
    bool menuIdentityDegraded{ false };
    actions::ActionSetStack actionSetStack;
    context::PresentationPolicyId presentationPolicyId;
    // ...
};
```

```cpp
next.uiContextId = contextSnapshot.uiContextId;
next.menuObserverCompleteness = contextSnapshot.menuObserverCompleteness;
next.menuIdentityDegraded = contextSnapshot.menuIdentityDegraded;
```

### 当前 `SkyrimCompatibilitySurface` gate

```cpp
bool IsRefreshableMenuPresentation(const PublishedPresentationState& state)
{
    return IsMenuPresentationContext(state.uiContextId) &&
        state.menuObserverCompleteness == menu::ObserverCompleteness::Complete &&
        !state.menuIdentityDegraded;
}
```

当前问题是 `SkyrimCompatibilitySurface` 仍维护了 `IsMenuPresentationContext(UiContextId)` 白名单。它能工作，但可能不是最好的边界。

### 当前 crash hardening

```cpp
const auto stateAtStart = surface.GetCommittedState();
if (!IsRefreshableMenuPresentation(stateAtStart)) {
    std::scoped_lock lock(surface._mutex);
    surface._refreshQueued = false;
    logger::debug("[DualPad][SkyrimCompat] menu_refresh_skipped reason=unstable_menu_context ...");
    return;
}

if (auto* ui = RE::UI::GetSingleton(); ui) {
    for (auto& menu : ui->menuStack) {
        if (!menu) {
            ++skippedNotReady;
            continue;
        }
        if (!menu->uiMovie) {
            ++skippedNotReady;
            continue;
        }
        menu->RefreshPlatform();
        ++refreshed;
        if (NotifyMenuPresentationChanged(*menu)) {
            ++notified;
        }
    }
}
```

## 当前测试覆盖

新增 / 扩展的测试点：

- `StableGenericMenu_QueuesPlatformRefresh`
- `ObserverDegradedUnknownMenu_DoesNotQueuePlatformRefresh`
- `GameplayOwnerDirty_DoesNotQueueMenuPlatformRefresh`
- observer completeness / degraded identity 从 `ContextResolver` 经 `PresentationProjection` 传到 compat surface。
- `SwitchingMenuContextWithSameOwner_QueuesPlatformRefresh`
- `RefreshQueueUnavailable_DoesNotConsumeEpoch`

已跑过：

- `xmake run -y DualPadPresentationProjectionTests`
- `xmake run -y DualPadContextResolverTests`
- `xmake run -y DualPadGameplayProjectionTests`
- `xmake run -y DualPadNativeButtonCommitTests`
- `xmake run -y DualPadManifestCompilerTests`
- `xmake run -y DualPadInputV2Tests`
- `xmake build -y DualPad`
- `git diff --check`
- `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`

## 当前方案的疑点

我认为当前方案可以解释现象并通过测试，但不够优雅，主要原因是：

1. `SkyrimCompatibilitySurface` 不应维护 `UiContextId` 的菜单白名单。
2. `PublishedPresentationState` 直接暴露 `menuObserverCompleteness` 和 `menuIdentityDegraded`，可能把 context 层细节泄漏到 presentation 层。
3. `UnknownTrackedMenu` 被用于 stable generic Menu 和 degraded fallback，语义过载。
4. 现在的 gate 是组合逻辑，未来新增 context / overlay 时容易漏测。

## 候选改进方向

### 方案 A：保留当前方案

优点：改动小，已经通过测试。  
缺点：compat surface 继续知道 `UiContextId` 分类，长期维护不够清晰。

### 方案 B：新增 `MenuRefreshEligibility`（推荐）

由 `ContextResolver` 或 `PresentationProjection` 产出明确合同：

```cpp
enum class MenuRefreshEligibility : std::uint8_t
{
    NotMenu = 0,
    StableTrackedMenu,
    ObserverPartial,
    ObserverUnavailable,
    IdentityDegraded,
    PassthroughOverlay
};
```

`SkyrimCompatibilitySurface` 只判断：

```cpp
state.menuRefreshEligibility == MenuRefreshEligibility::StableTrackedMenu
```

优点：边界最清楚；compat surface 不再知道 context catalog 细节。  
缺点：需要调整 presentation state、投影测试和诊断日志。

### 方案 C：拆分 `UiContextId`

把 `UnknownTrackedMenu` 拆成类似：

- `GenericMenu`
- `DegradedUnknownMenu`

优点：从根上消除语义过载。  
缺点：波及 catalog、action set、prompt、docs、generated facts，短期改动面更大。

## 希望网页 GPT 给出的判断

请判断：

- 当前补丁是否足够安全，可以先合入？
- 如果要正式化，方案 B 是否比当前方案更合理？
- `MenuRefreshEligibility` 应该由 `ContextResolver` 直接产出，还是由 `PresentationProjection` 从 context facts 派生？
- queued UI task 执行时发现 unstable context，应该清 `_refreshQueued`，还是保留 pending epoch 等下一次稳定后重试？
- `uiMovie == nullptr` 时跳过是否足够，是否还需要检查 menu flags / opening state？
- `DualPadDebug.ini` 默认关闭高频日志是否应与修复放在同一提交，还是拆成单独提交？
