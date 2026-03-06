你是 C++ 实时系统与 DualSense 触觉链路专家。请对当前项目进行“时间戳驱动的可靠输出链路”重构，目标是彻底解决丢震动、乱震动、晚到重叠、负载下体感崩坏问题。允许大改，优先正确性、实时性、可观测性。

【总体目标】
把现有 HidOutput 的“单槽覆盖 pending”改为“分层调度输出架构”：
1) 生产侧：Mixer 产出带语义与时间戳的帧事件；
2) 传输侧：多队列/优先级队列 + 抗背压策略；
3) 消费侧：HID 专属发送线程做 deadline 调度、去抖合帧、过期丢弃、抖动统计；
4) 观测侧：完整 telemetry（队列、时延、丢弃、合帧、抖动、写失败）。

【必须实现的架构】
A. 数据结构
- 新增 OutputEnvelope（或等价结构）字段至少包含：
  - qpc_produce_us, qpc_target_us, eventType, sourceType, priority, confidence, left, right, seq
- 统一使用单调时钟（项目现有 ToQPC/NOW），禁止混用不同时间基。

B. 队列层（替换单槽 pending）
- 至少两条逻辑通道：
  - FG（战斗/命中/格挡/武器等瞬态关键事件）
  - BG（unknown/ambient/ui/music 等连续或背景事件）
- 每通道使用 SPSC/MPSC 有界队列（根据线程模型选型），实现无阻塞 TryPush。
- 背压策略：
  - FG：尽量保留最新且关键帧，禁止被 BG 挤占；
  - BG：允许激进丢弃（drop oldest / coalesce）。
- 队列满时策略必须可配置并计数（不能 silent drop）。

C. HID 发送调度器（核心）
- 独立发送循环：按 qpc_target_us 做 deadline 驱动。
- 实现 4 个处理阶段：
  1) stale filter：过期帧（now - target > stale_us）直接丢弃；
  2) merge window：在 merge_window_us 内按事件类型策略合帧；
  3) transient preservation：Hit/Block/Swing 等瞬态事件低延迟直通或极小窗口；
  4) output shaping：对 BG 类做平滑/限幅，FG 类保边沿。
- 合帧策略必须“事件感知”：
  - FG：保峰值 + 保最近时间戳，避免打击感被抹平；
  - BG：RMS/平均可接受，优先稳定不抖。
- 绝不补发过时帧（防止时间错位乱震）。

D. 控制与配置
- 在 HapticsConfig 增加并解析配置：
  - hid_tx_fg_capacity, hid_tx_bg_capacity
  - hid_stale_us
  - hid_merge_window_fg_us, hid_merge_window_bg_us
  - hid_scheduler_lookahead_us
  - hid_bg_budget, hid_fg_preempt
- 支持热重载后生效（若工程已有 hot reload 机制则接入）。

E. 观测与日志
- 扩展 HidOutput::Stats：
  - txQueuedFg, txQueuedBg
  - txDequeuedFg, txDequeuedBg
  - txDropQueueFullFg, txDropQueueFullBg
  - txDropStaleFg, txDropStaleBg
  - txMergedFg, txMergedBg
  - txSendOk, txSendFail, txNoDevice
  - txLatencyP50Us, txLatencyP95Us, txJitterP95Us
- 接入现有周期日志，输出关键链路健康指标。

【实现要求】
- 可以重构 HidOutput、HapticMixer 输出接口、相关线程模型；必要时新增 OutputScheduler 类。
- 保持 DecisionEngine / AudioOnlyScorer / Gate 语义不变（可改对接层，不改算法意图）。
- 保证线程安全：避免全局大锁导致抖动，优先无锁队列 + 局部锁。
- 代码风格保持与仓库一致，避免一次性引入过重第三方库。

【质量门槛（验收）】
1) 不再存在“pending 覆盖吞帧”路径；
2) 高负载下 BG 可控退化，FG 体感稳定；
3) 晚到帧不会补发造成重叠乱震；
4) 指标可证明改进：send fail 不升高，stale/drop 可解释，latency/jitter 分位数下降；
5) 编译通过，给出变更清单与迁移说明。

【交付内容】
- 完整代码修改；
- 新增/修改配置项说明；
- 架构图（文字版即可）；
- A/B 压测步骤（轻载、战斗高频、后台噪声三场景）；
- 风险与回滚方案。