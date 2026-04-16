# owner 仲裁 / gameplay 注入协调层审查记录（2026-04-10）

## 审查范围

本次只看输入链路中“owner 仲裁 / gameplay 注入协调”这一层：

- `src/input/injection/GameplayOwnershipCoordinator.*`
- `src/input/InputModalityTracker.*`
- `src/input/GameplayKbmFactTracker.*`
- `src/input/injection/PadEventSnapshotProcessor.*`
- `src/input/injection/SourceBlockCoordinator.*`
- `src/input/backend/NativeButtonCommitBackend.*`
- `src/input/injection/UpstreamGamepadHook.*`

必要时交叉核对：

- `src/input/InputContext.*`
- `docs/current_input_pipeline_zh.md`
- `docs/phase2_gameplay_presentation_owner_minimal_plan_zh.md`
- `docs/phase1_phase4_code_review_findings_zh.md`

## 前提假设

这次审查默认按当前仓库现状判断：

- 不把 UI 菜单 owner 仲裁和 gameplay owner 仲裁混成一个问题
- 重点只看 gameplay-domain 下：
  - KBM / Gamepad owner 切换
  - gameplay presentation seed / latch
  - digital gate 与 transient cancel
  - mixed-source sprint contributor
- 这轮不扩展到 vibration / glyph / gameplay 动作语义本身

## 结论摘要

- 这一层有两个明确的 `P1`
  - 菜单退出时会用已在菜单期 reset 过的 coordinator 状态重种 gameplay presentation
  - keyboard sprint contributor 根本没有真正接进 poll commit FSM
- 另外有一个 `P2`
  - digital ownership 只认裸 `Gameplay`，没有真正覆盖 gameplay domain 子上下文
- 这轮里最实的风险不是“owner 架构概念不清”，而是：
  - 某些设计上已经拆开的状态，在运行时衔接点重新丢失

## 具体问题

### 1. 菜单退出时，会从已 reset 的 coordinator 状态重种 gameplay presentation

当前链路里有一条危险组合：

1. `GameplayOwnershipCoordinator::UpdateDigitalOwnership(...)` / `ApplyOwnership(...)` 在非 gameplay context 下都会调用 `ResetForNonGameplay()`
2. `ResetForNonGameplay()` 会把：
   - `publishedGameplayPresentationOwner`
   - `publishedGameplayMenuEntryOwner`
   - 所有 presentation lease
   全部清回 `KeyboardMouse`
3. `InputModalityTracker::ApplyGameplayMenuInheritance(...)` 在进入菜单时还会额外 `Reset()` 掉 `GameplayKbmFactTracker`
4. 随后 `InputModalityTracker::ReconcileContextState()` 在 `Menu -> Gameplay` 切回时，立刻调用：
   - `gameplayOwnership.RefreshPublishedGameplayPresentation(context)`
   - `SyncGameplayPresentationFromCoordinator(context, epoch, "exit-menu")`

问题就在这里：

- 退出菜单这一拍，coordinator 里保留下来的 gameplay presentation 事实，已经在菜单期被 reset 掉了
- `GameplayKbmFactTracker` 也在菜单进入时清零了
- 如果玩家关闭菜单后还没来得及产生新的 gameplay hint
- `engineGameplayPresentationLatch` 和 `gameplayMenuEntrySeed` 就会被 deterministically 重种成 `KeyboardMouse`

结果是：

- `ResolveIsUsingGamepad()` 会在菜单刚关掉时回落成键鼠
- 直到下一次新的 gameplay 输入 hint 才会再翻回 Gamepad

这和当前 Phase 2 文档里“tracker 只做 adapter，不重新制造 gameplay presentation 事实”的目标是冲突的。

受影响文件：

- `src/input/injection/GameplayOwnershipCoordinator.cpp`
- `src/input/InputModalityTracker.cpp`
- `src/input/GameplayKbmFactTracker.cpp`

当前判断：

- 这是 `P1`
- 因为它会直接影响 menu close 后的 gameplay presentation 稳定性
- 而且是当前结构自己制造出来的确定性错误，不是偶发 race

### 2. keyboard sprint contributor 从来没有真正进入 poll commit FSM

当前 sprint handoff 设计明显预留了 mixed-source held contributor：

- `PollCommitCoordinator`
- `HeldContributor::Gamepad`
- `HeldContributor::KeyboardMouse`

而 `NativeButtonCommitBackend::CommitPollState()` 里也会专门读：

- `GameplayKbmFactTracker::GetFacts().IsKeyboardMouseSprintActive()`

但真正把外部 held contributor 同步进 commit FSM 的地方是：

- `NativeButtonCommitBackend::SyncExternalHeldContributors(...)`

这里当前实现却是：

- `constexpr bool kbmSprintHeld = false;`
- 然后永远把这个 `false` 同步给 `PollCommitCoordinator`

也就是说：

- KBM sprint held 事实虽然在 `GameplayKbmFactTracker` 里有
- sprint probe 里也在看
- 但真正的 poll commit contributor 聚合链压根没接上

结果是：

- mixed-source sprint ownership 永远退化成“只有 gamepad contributor 真有效”
- 这条路径里的 keyboard held / handoff 设计事实上没有运行

受影响文件：

- `src/input/backend/NativeButtonCommitBackend.cpp`

当前判断：

- 这是 `P1`
- 因为这不是“调参不对”
- 而是整个 keyboard contributor 路径当前被硬编码短路

### 3. digital ownership 只认裸 `Gameplay`，没真正覆盖 gameplay domain

当前整体设计上已经明确：

- `Gameplay`
- `Sneaking`
- `Combat`
- `Riding`
- `Werewolf`
- `VampireLord`
等都属于同一 gameplay domain

但 `GameplayOwnershipCoordinator::HasMeaningfulGamepadDigitalAction(...)` 里现在写的是：

- `if (action.context != InputContext::Gameplay) continue;`

对应的 suppression 消费路径里，`NativeButtonCommitBackend` 也有同样的裸 `Gameplay` 限制。

这就导致：

- owner 架构说这些 context 是同一 gameplay domain
- digital ownership promotion / suppression 却只在 `Gameplay` 裸 context 生效

于是 Sneaking / Combat 这类 gameplay 子上下文里，digital gate 行为就和主 gameplay 不一致。

受影响文件：

- `src/input/injection/GameplayOwnershipCoordinator.cpp`
- `src/input/backend/NativeButtonCommitBackend.cpp`

当前判断：

- 这是 `P2`
- 它不是马上就会炸的 blocking bug
- 但它让 ownership 语义在 gameplay domain 内部出现了不必要分叉

## 非阻塞但值得记住的观察

### 1. 当前文档里对 `engineOwner` / `menuEntryOwner` 的分离是有意保留，不应误判

这轮没有把下面这件事当 bug：

- `MoveOwner` 进入 `menuEntryOwner`
- 但不直接进入 `engineOwner`

因为现有文档已经明确写过：

- 这是为了避免“左摇杆 + 鼠标”重新引入 gameplay camera jitter
- 属于当前有意保留的工程约束

所以这轮问题不在“为什么 left stick 不直接改 engine-facing presentation”。

### 2. 当前更大的问题是“衔接点丢状态”，不是“状态拆分过细”

也就是说：

- `GameplayPresentationState` 分成 `engineOwner` 和 `menuEntryOwner`
  这件事本身没有被这轮推翻
- 真正出问题的是：
  - 进入菜单时 reset 了事实
  - 退出菜单时又从 reset 后的事实回填 adapter

## 建议调整顺序

### 第一批：先修 menu exit 重种链路

1. 不要在 `Menu -> Gameplay` 切回时直接从已 reset 的 coordinator 状态回灌 latch
2. 明确 gameplay presentation 的恢复来源
3. 避免菜单刚关时 `IsUsingGamepad()` 短暂掉回键鼠

### 第二批：把 keyboard sprint contributor 真接进 commit FSM

1. 去掉 `kbmSprintHeld = false` 的硬编码
2. 用 `GameplayKbmFactTracker` 的 held 事实驱动 `SyncHeldContributor(...)`
3. 再验证 mixed-source sprint handoff 是否真的活起来

### 第三批：把 digital ownership 扩到 gameplay domain

1. 把裸 `InputContext::Gameplay` 判断收口成 gameplay-domain 判断
2. 保证 `Sneaking / Combat / Riding` 等子上下文的 digital gate 行为和主 gameplay 一致

## 当前建议口径

后续如果有人继续改这一层，建议默认接受这两个原则：

- gameplay presentation 的 authoritative 事实不能在菜单期被 reset 后再反向喂回 adapter
- mixed-source contributor 只要设计上存在，就不能在关键同步点被硬编码短路

换句话说：

- 当前问题不是 owner 架构概念太多
- 而是几个最关键的桥接点把本来已经存在的事实丢掉了
