# Runtime 背压与数据分类合同

本文说明 RC20 输入不流畅问题对应的数据分类、budget、overflow 和恢复合同。

## 数据类别

| 数据 | 保存策略 | 原因 |
| --- | --- | --- |
| axes / triggers / current physical mask | `LatestPadState` complete-generation publication | 连续状态只需要最新完整值，不应按 HID report 排队 |
| source evidence snapshot | `LatestSourceEvidence` | 重复 evidence latest-wins；device-family change 仍作为 boundary 入队 |
| digital press/release | bounded ordered queue | edge 不可覆盖、乱序或重复 |
| manifest/UI/device boundary | bounded ordered queue | 必须在对应后续 edge 前消费 |
| reset / overflow marker | bounded ordered queue | 建立显式 recovery barrier，不静默丢历史 |
| legacy snapshot | 仅 compat/replay/debug payload | 不回流 core kernel authority |

ordered queue 的唯一顺序权威是 `IngressEvent.seq`。该序号在 `IngressHub` 锁内分配，只有 seq 重复、倒退或不连续才能让 `FrameAssembler` 生成 `SequenceGap` transition。

`monotonicUs` 是 producer 在进入 hub 临界区前采集的观测时间。即使两个事件共享同一个粗粒度 `IngressSource`，它们也可能来自不同线程或不同时钟域；先采集的线程还可能后获得 hub 锁。因此，时间戳倒退不能证明事件丢失。`FrameAssembler` 必须保留 seq 已排序的事实，只以全局 max 推进不倒退的帧评估时间。

## 双 cutoff

一次 `IngressHub::Capture(maxEvents)` 同时返回：

1. bounded ordered events cutoff；
2. 内部完整的 latest state/source generations。

latest analog 可以领先仍在 backlog 中的同 context/device digital edge，但 normal digital reducer 只由已 drain edge 推进。`currentDownMask` 只用于 overflow 后建立 clean physical baseline，不能在正常路径猜造 press/release。

`LatestSourceEvidence` 也可能已经描述仍位于本轮 ordered cutoff 之后的 `DeviceFamilyChanged` marker。此时 assembler 必须延迟 latest generation，直到匹配 revision 的 marker 被消费；不能把“latest 快照领先 capture cutoff”当作 marker mismatch 或 `ExplicitReset`。配对完成前，latest pad 与 ordered pad facts 均不得生成新 boundary 的 `Stable` frame。真正同一 ordered pairing 内的不一致仍保持 fail-closed。

## 连续 current-state materialization

`LatestPadState` 为每个 stable owner frame 提供完整 axes/triggers 样本。interaction 输出必须区分快照与事件：

- `ResolvedActionFrame.values` 是稀疏绝对 current-state；所有非 neutral axis/trigger 值必须逐 stable frame 保留，即使本轮数值未变化；
- `ResolvedActionFrame.changes` 只记录实际 phase/value change，不因 held axis 重复产生 `Value` phase；
- neutral 可以由显式 zero change 或 values 中缺省表达，下游完整 `GameplayProjectionFrame` 在缺省时使用零值；
- 下游不得把“本轮没有新的 `Value` phase”解释成“物理轴已回零”。

该分离保证 held stick 与 held trigger 在每个 immutable `PollOutputFrame` 中保持 current-state，同时避免把连续状态误塞入 ordered edge queue。

## Budget 与调度

- pending、high-water、budget 和 drained telemetry 的单位统一为 event。
- `Drain(maxEvents)` 严格执行 hard cap；deadline 只能更早停止，不能突破 event cap。
- `active_fresh` 不启用第二 consumer。
- frame pump disabled 时，pending event 或 uncaptured latest work 可由唯一 task fallback 接管。
- frame pump enabled 时，只有 `active_stale` 且达到 high-water 才允许 task recovery。

## Overflow 与恢复

ordered queue overflow 会压缩为 typed `QueueOverflow` marker，保留 latest axes/current physical mask 与必要 boundary facts，丢弃已无法证明顺序的 volatile edge history。之后 transient digital 冻结，直到观察到全键释放建立 clean baseline；该 release 本身不泄漏 stale pulse，下一次新 press 才恢复正常 edge delivery。

该策略优先保证“不制造错误动作”和“模拟量不冻结”，而不是在历史已丢失时猜测用户意图。

## Rate matrix 与性能记录

`DualPadIngressTests` 使用 deterministic virtual scheduler 覆盖：

- producer：500 Hz、1000 Hz；
- owner：30 Hz、60 Hz、120 Hz；
- producer-first / owner-first 同 timestamp 交错。

correctness gate 断言 complete generation 不倒退、不读取 future state、pure analog ordered queue 始终为零，semantic P99 不超过一个 producer period。

同一 target 另记录 20,000 次 `PushPadSnapshot -> Capture` wall-clock P50/P95/P99。该数字用于本机趋势诊断，不设置跨机器硬阈值；release 性能结论仍以真实游戏 profiler/soak 为准。

## 已知限制

host scheduler 不能证明 HID 驱动、Windows 调度、Skyrim input pump 或第三方插件在真实运行时的尾延迟。现场仍需记录 queue high-water、overflow transition、owner cadence、Poll generation 和日志量。
