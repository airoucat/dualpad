# Plan A 约束下的最佳长期方案说明

更新时间：2026-03-14

## 目的

这份文档用于说明：

- 在 `agents2.md` 与 `agents3.md` 的约束下
- 当前 `ButtonEvent -> Poll-owned current-state -> Skyrim producer` 主线不能退回旧 event injection
- 同时又要解决“同一 Poll 窗口内的瞬时按下/松开不能丢 first-edge”

时，什么才是**最好的长期方案**。

这份文档不是临时补丁说明，而是目标架构说明。

---

## 当前已落地状态

截至 2026-03-14，这条长期方案里已经有两步开始落地：

1. `SyntheticPadFrame` 已补入显式 transient edge 字段
   - `transientPressedMask`
   - `transientReleasedMask`
   - `SyntheticButtonState::sawPressEdge`
   - `SyntheticButtonState::sawReleaseEdge`
   - `SyntheticButtonState::sawPulse`
   - `firstPressUs / lastReleaseUs`
2. `ActionLifecycleCoordinator` 已不再只看最终 `down`
   - 现在会直接消费 `sawPressEdge`
   - 对“同一 reduced frame 内 press+release”先产出一次 `Press`
   - 再在后续帧收口 `Release`

但这仍然不是最终完成态，因为：

- lifecycle transaction 还没有完全独立成显式 transaction 类型
- executor 还没有完全收窄成纯 `PollCommitCoordinator`

所以本文以下内容仍然是长期终态说明。

---

## 一句话结论

最佳长期方案不是继续在 executor 里堆更多 `pendingPulse` / `queuedRelease` / `retry`，
而是：

> **把“Poll 窗口内的显式瞬时边沿事实”提升为 planner/lifecycle 的一等输入，再由 Poll commit 层只负责把这些语义 materialize 成每 Poll 唯一 authoritative current-state。**

换句话说：

> **lifecycle 层拥有 edge 语义，commit 层只拥有可见性语义。**

---

## 当前临时修法是什么

当前已经落下去的修法，是在
[SyntheticStateReducer.cpp](/c:/Users/xuany/Documents/dualPad/src/input/injection/SyntheticStateReducer.cpp)
里显式聚合：

- `transientPressedMask`
- `transientReleasedMask`
- `SyntheticButtonState::sawPressEdge`
- `SyntheticButtonState::sawReleaseEdge`
- `SyntheticButtonState::sawPulse`

然后由
[ActionLifecycleCoordinator.cpp](/c:/Users/xuany/Documents/dualPad/src/input/backend/ActionLifecycleCoordinator.cpp)
直接消费 `sawPressEdge`，
当某个 lifecycle-owned source bit 在同一 reduced frame 内发生了 `press + release` 时，
先保留一次 `Press`，把 `Release` 留到下一帧收口。

这个修法的优点是：

- 不是动作名特判
- 对所有 lifecycle-owned 数字动作都成立
- 能直接修复“同帧瞬时 edge 被压成 final up”的问题

但它仍然只是一个**中间态落地**，因为：

- lifecycle transaction 还没有完全独立成显式 transaction 类型
- coordinator 虽然已经吃进 `sawPressEdge`，但还没有把全部 edge facts 统一建模成显式生命周期交易
- commit 层也还没有完全收窄成纯 `PollCommitCoordinator`

所以它不是长期终态。

---

## 本文要约束的长期目标

长期方案必须同时满足 `agents2.md` 和 `agents3.md` 的核心要求：

1. `FrameActionPlan` 必须是真合同
2. 数字主线必须真正是 `Poll current-state ownership`
3. backend 不得私有化 contract 解释权
4. `Poll commit` 每次只能提交一份 authoritative synthetic current-state
5. `Repeat`、`Toggle`、`Pulse` 的语义必须留在 lifecycle 层
6. old engine-cache / old native event injection 不能重新长回默认主线
7. 不能围绕 `Jump` 或单个动作继续做专项结构

---

## 最佳长期方案：显式 Transient Edge 生命周期模型

### 核心思想

当前 reducer 输出里已经有：

- `downMask`
- `pressedMask`
- `releasedMask`
- `pulseMask`
- `tapMask`
- `holdMask`
- `comboMask`

但 lifecycle coordinator 现在还没有把这些信息完整当成“生命周期事实”来消费。

最佳长期方案是再往前走一步：

### 1. 将瞬时边沿正式建模

建议把
[SyntheticPadFrame.h](/c:/Users/xuany/Documents/dualPad/src/input/injection/SyntheticPadFrame.h)
里的按钮聚合信息，从“最终态 + 若干辅助 mask”，升级成明确的 Poll 窗口边沿事实，例如：

```cpp
struct SyntheticButtonAggregate
{
    std::uint32_t code{ 0 };
    bool finalDown{ false };
    bool sawPressEdge{ false };
    bool sawReleaseEdge{ false };
    bool sawPulse{ false };
    bool sawTap{ false };
    bool sawHoldTrigger{ false };
    bool sawComboTrigger{ false };
    float heldSeconds{ 0.0f };
    std::uint64_t firstPressUs{ 0 };
    std::uint64_t lastReleaseUs{ 0 };
};
```

这里最重要的不是字段名，而是语义：

- `finalDown` 代表最终状态
- `sawPressEdge` / `sawReleaseEdge` 代表本 Poll 窗口确实出现过边沿
- `sawPulse` 代表 `up -> down -> up` 在窗口内发生过

这样 lifecycle 层就不再需要“从 final state 反推 edge”。

### 2. lifecycle coordinator 直接消费 edge facts

[ActionLifecycleCoordinator.cpp](/c:/Users/xuany/Documents/dualPad/src/input/backend/ActionLifecycleCoordinator.cpp)
的长期职责不该是：

- 看 `button.down`
- 猜 `Press / Hold / Release`

而应该是：

- 根据 `edge facts + lifecycle policy`
- 产出**显式的 lifecycle transaction**

例如：

- `Pulse/DeferredPulse`
  - 看 `sawPressEdge` 或 `sawPulse`
  - 产出一次 pulse transaction
- `HoldOwner`
  - 看 `finalDown` 与 `sawPressEdge / sawReleaseEdge`
  - 维护 logical held state
- `RepeatOwner`
  - 保留 `first-edge`
  - 同时维持 `logical held active`
- `ToggleOwner`
  - 只在 `sawPressEdge` 时翻转 logical active
  - `release` 只负责 re-arm

### 3. lifecycle 层输出 transaction，不输出“半猜半写”的状态

长期应收敛成：

```cpp
struct NativeDigitalLifecycleTxn
{
    std::string actionId;
    NativeControlCode outputCode;
    ActionOutputContract contract;
    ActionLifecyclePolicy lifecyclePolicy;
    NativeDigitalPolicyKind digitalPolicy;

    bool logicalActive{ false };
    bool startThisPoll{ false };
    bool stopThisPoll{ false };
    bool preserveFirstEdge{ false };

    std::uint64_t timestampUs{ 0 };
    std::uint32_t contextEpoch{ 0 };
    InputContext context{ InputContext::Gameplay };
};
```

注意：

- 这里的 transaction 是 lifecycle 语义对象
- 还不是游戏可见 current-state
- 还没有直接写入 `buttons` bitfield

### 4. Poll commit 层只负责可见性 materialization

在这个长期方案里，
[NativeDigitalActionExecutor.cpp](/c:/Users/xuany/Documents/dualPad/src/input/backend/NativeDigitalActionExecutor.cpp)
或者它未来重命名后的 `PollCommitCoordinator`
应该进一步收窄职责。

它只负责：

- 当前 slot 是否已有 active transaction
- 这个 transaction 本 Poll 是否要 materialize 为 `down`
- 是否已经满足 visible-one-Poll
- 是否可以 release
- 最终生成本 Poll 唯一的 `semanticDownMask`

它**不应该再解释**：

- 这个动作是不是 toggle
- 这个动作是不是 repeat
- 这个动作为什么要 first-edge preserve
- 为什么某个 action 需要特殊 min-down

这些都必须在 planner/lifecycle 层已经决定好。

---

## 长期链路长什么样

最佳长期链路应为：

```text
PadEventSnapshot
  -> SyntheticStateReducer
     输出：finalDown + explicit transient edges
  -> FrameActionPlanner
     输出：non-lifecycle planned actions
  -> ActionLifecycleCoordinator
     输出：lifecycle transactions / planned lifecycle actions
  -> PollCommitCoordinator
     只做 visible-one-Poll materialization
  -> UpstreamGamepadHook / XInput current-state write
  -> Skyrim Poll prev/cur diff
  -> Skyrim producer helper
  -> 原生 ButtonEvent
```

这里必须保证：

- `FrameActionPlan` 仍是真合同
- `PollCommitCoordinator` 不拥有语义解释权
- 最后只有一份 authoritative synthetic current-state

---

## 它为什么比当前补丁更好

### 1. 它不是“压缩后补救”，而是“保真后规划”

当前补丁：

- reducer 已经压缩了
- coordinator 再读 `pulseMask` 补一次 `Press`

长期方案：

- reducer 直接保存完整 transient edge 事实
- coordinator 直接消费这些事实

这会少一层信息损失，也更符合 `agents2.md` 里“Poll 聚合不能只保留 final state”的要求。

### 2. 它天然适用于更多动作

当前补丁主要在修：

- 快速方向键
- 同帧瞬时 `RepeatOwner`
- 某些 `Toggle/Hold` 短窗

长期方案则可以统一覆盖：

- `Pulse`
- `Hold`
- `Repeat`
- `Toggle`
- `Tap-derived helper action`
- `Combo-derived helper action`

不需要继续靠“哪个动作今天又暴露了 edge 丢失”来一点点补。

### 3. 它能把 `Repeat` 和 `Toggle` 继续收正

按 `agents3.md`，长期应避免：

- `Repeat` 偷偷退化成 backend 自己 repeat 时钟
- `Toggle` 偷偷退化成 backend 私有 latch

显式 transient edge 模型恰好能把：

- `Repeat` 的 first-edge
- `Toggle` 的 press-edge flip

留在 lifecycle 层，而不是继续落到 commit 层里。

---

## 它会不会引入风险

会，但风险比“继续在 executor 里加补丁”更可控。

### 风险 1：生命周期层会更复杂

是的，coordinator 会变重一些。

但这是正确的重：

- 语义复杂度回到 lifecycle 层
- commit 层反而会变轻

这符合 `agents3.md` 的防退化要求。

### 风险 2：如果 reducer 定义不清，会出现重复表达

例如同时保留：

- `pressedMask/releasedMask`
- `pulseMask`
- `sawPressEdge/sawReleaseEdge`

却没有统一语义，就会引入歧义。

所以长期推进时要明确：

- 哪些字段是“最终态”
- 哪些字段是“窗口边沿事实”
- 哪些字段是“派生标签”

### 风险 3：如果 transition contract 设计不好，会让 `FrameActionPlan` 变成半成品

所以必须坚持：

- plan 里要么直接是 planner action
- 要么直接是 lifecycle transaction
- 不能让 backend 再自己猜一遍

---

## 不建议的假长期方案

下面这些都不应被误判成“长期方案”：

### 1. 继续在 executor 增加更多 pending 标志

例如继续加：

- `pendingTap`
- `pendingCombo`
- `pendingFirstEdge`
- `queuedRepeatPulse`

这会把 lifecycle 语义重新私有化到 backend。

### 2. 给快按方向键加动作特判

例如：

- `Menu.ScrollDown` 快按保留
- `Menu.ScrollUp` 单独延后 release

这违反 `agents2.md` 的“不要动作硬编码修法”。

### 3. 用固定延时兜一切

例如：

- 所有 `RepeatOwner` 强制 30ms min-down
- 所有 `Toggle` 强制拖尾一帧

这不是 contract 设计，只是时间窗口补丁。

---

## 从当前代码到长期终态的迁移路径

建议按三步走。

### 第一步：保留当前补丁，先把 reducer 的边沿语义显式化

目标：

- 不先推翻现有可工作的修复
- 先把 `pulseMask`、`pressedMask`、`releasedMask` 统一成明确的 transient edge 模型

改动重点：

- [SyntheticPadFrame.h](/c:/Users/xuany/Documents/dualPad/src/input/injection/SyntheticPadFrame.h)
- [SyntheticStateReducer.cpp](/c:/Users/xuany/Documents/dualPad/src/input/injection/SyntheticStateReducer.cpp)

### 第二步：让 lifecycle coordinator 直接消费 edge facts

目标：

- 去掉“只看 final down，再用 `pulseMask` 打补丁”的中间态写法
- 让 `HoldOwner / RepeatOwner / ToggleOwner` 的规划都建立在显式边沿事实上

改动重点：

- [ActionLifecycleCoordinator.cpp](/c:/Users/xuany/Documents/dualPad/src/input/backend/ActionLifecycleCoordinator.cpp)
- [FrameActionPlan.h](/c:/Users/xuany/Documents/dualPad/src/input/backend/FrameActionPlan.h)

### 第三步：继续收窄 executor，逼近真正的 PollCommitCoordinator

目标：

- 把 `NativeDigitalActionExecutor` 从“半语义层”继续收成“纯 materializer”
- 让它只关心：
  - 何时 down
  - 何时可见
  - 何时 release

改动重点：

- [NativeDigitalActionExecutor.cpp](/c:/Users/xuany/Documents/dualPad/src/input/backend/NativeDigitalActionExecutor.cpp)
- [ButtonEventBackend.cpp](/c:/Users/xuany/Documents/dualPad/src/input/backend/ButtonEventBackend.cpp)

---

## 对当前问题的最终定性

“退出游戏日志附近，方向键很快按下又松开但没生效”这类问题，
长期不应再被看成：

- 某个菜单 consumer 特例
- 某个方向键需要特判
- executor 某个 pending 标志还不够多

它的正确定性是：

> **当前 lifecycle-owned 数字主线还没有把 Poll 窗口内的瞬时边沿事实完整提升为一等生命周期输入。**

所以长期正确修法一定是：

> **显式 transient edge 生命周期模型**

而不是：

> **更多 executor 内补丁**

---

## 当前建议

如果后续继续推进，不建议再沿“多补一个 pending flag”这条线走。

更推荐的下一步是：

1. 先显式化 reducer 的 transient edge 数据模型
2. 再让 coordinator 直接消费这些 edge facts
3. 最后把 executor 继续收成真正的 Poll commit materializer

这条路线最符合 `agents2.md` 与 `agents3.md`，也最不容易退化回旧 event injection 或 backend 私有语义。
