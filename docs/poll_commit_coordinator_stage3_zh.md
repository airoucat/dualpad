# Poll Commit 第三阶段说明

更新时间：2026-03-14

## 当前已落地

这一阶段已经完成两件关键事：

1. `ActionLifecycleCoordinator` 先产出显式 `LifecycleTransaction`
   - coordinator 不再一边推导生命周期，一边直接拼最终 `PlannedAction`
   - 现在会先把 edge facts 规整成 transaction，再把 transaction 落成 plan
2. `NativeDigitalActionExecutor` 已收口为 `PollCommitCoordinator`
   - `ButtonEventBackend` 现在是唯一的 `plan -> commit request` translator
   - `PollCommitCoordinator` 只消费 `PollCommitRequest`
   - commit 层只保留 Poll 可见性 materialization 职责

## 这一步解决了什么

- lifecycle 层和 Poll commit 层的责任边界更清楚了
- commit 层不再直接吃完整 `PlannedAction`
- 后续如果还要继续收窄，就不需要再围绕旧 executor 语义补丁做文章

## 这一步还没有解决什么

这一步没有把“跨按钮严格保序”问题一起解决。

当前仍然成立的边界是：

- 单个按钮的 first-edge 已经能保住
- 但两个不同按钮之间的微小时序，例如“方向键下后立刻确认”，在 current-state 主线上仍然可能被压进同一个 Poll

## 后续只剩两类长期问题

1. 是否要在 planner/commit 边界上引入“跨动作保序 contract”
2. 哪些少数 UI 场景需要改走 `direct native event`，以保留真实按钮顺序

## 已确认但本轮不处理的更新点

### 方向键下 + A（Cross）快速连按

这类问题已经确认属于“跨按钮微小时序”边界，不再归类为：

- 单键 first-edge 丢失
- `RepeatOwner` 首 edge 丢失
- `Menu.Confirm` 单独脉冲不可见

当前判断是：

- 单个按钮的 edge 保留已经工作
- 但当 `Menu.ScrollDown` 与 `Menu.Confirm` 很快连按并落入相邻或同一 Poll 窗口时
- current-state 主线只能稳定表达“这一 Poll 里哪些位是 down”
- 不能天然保留“先导航、再确认”的跨按钮顺序

因此，这一项后续只能在下面两条路线里二选一推进：

1. 在 planner / commit 边界增加很窄的“跨动作保序 contract”
2. 对少数顺序敏感 UI 场景改走 `direct native event`

本轮明确不修这项，避免为单个 UI 组合再向 commit 层回灌动作特判。

## 与 agents2 / agents3 的关系

这一阶段是符合当前约束的，因为：

- `FrameActionPlan` 仍然是真合同
- 数字主线仍然是 `Poll current-state ownership`
- lifecycle 语义没有重新私有化到 commit 层
- old event injection 没有重新长回默认主线
